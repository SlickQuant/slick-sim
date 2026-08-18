# Order book

`slick::sim::OrderBook`
([`src/order_book/order_book.hpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/order_book/order_book.hpp))
wraps the external
[slick-orderbook](https://github.com/SlickQuant/slick-orderbook) L3 book and adds the one thing the
simulator needs that a plain book does not: the ability to hold **real-market liquidity and simulated
user orders in the same structure** while keeping them straight.

## Class shape

```cpp
class OrderBook : public OrderBookL3,
                  public orderbook::IOrderBookObserver,
                  public std::enable_shared_from_this<OrderBook>
```

It inherits the L3 book, observes *itself* (`createOrderBook` calls
`order_book_->addObserver(order_book_->shared_from_this())`), and is also observed by the owning
`Symbol` via a separate `OrderBookObserver`. Self-observation is how the book maintains
`feed_md_level_quantity_` without the mutation sites having to remember to update it.

`OrderBookImpl<OrderBookType>` is the concrete template, specialised for `L2` and `L3`. Both live
adapters instantiate `OrderBookImpl<OrderBookType::L2>`; the `L3` specialisation's `populate*` methods
are [empty stubs](known-gaps.md#l3-order-books-are-stubs).

## Phantom orders versus simulator orders

This is the central concept.

When the market-data feed reports that the real venue has 5 BTC bid at 100,000, the simulator cannot
know which orders make up that 5 BTC. It synthesises a **phantom order** — a single anonymous order
for the whole level quantity, with a fresh id from `utils::nextOrderId()` — and inserts it into the
L3 book. When your client sends a bid for 1 BTC at the same price, that becomes a **simulator order**
inserted into the same price level, behind the phantom order in the queue.

The book distinguishes them by a simple rule:

> An order is a **simulator order** if and only if it is in `orders_`.
> Everything else in the L3 book is phantom.

```cpp
std::unordered_map<uint64_t, Order*>                orders_;   // simulator orders only
std::unordered_map<fixed_string<37>, Order*, …>     orders_by_client_order_id_;
std::unordered_map<fixed_string<37>, Order*, …>     orders_by_order_id_;
std::unordered_map<price_t, qty_t>                  feed_md_level_quantity_;  // phantom qty per level
```

The two id indices are keyed by the order's own
[`utils::fixed_string`](https://github.com/SlickQuant/slick-sim/blob/main/src/utils/fixed_string.hpp)
type rather than `std::string`, so inserting one does not allocate a copy of the key. They carry the
transparent `StringViewHash`/`StringViewEq` functors, which take `std::string_view` — a `fixed_string`
converts to one implicitly, so both the keys and the `string_view` lookups go through the same pair.

`findOrder(id)` returning `nullptr` therefore *means* "this is a phantom order", and the code uses it
that way throughout — for example in `HyperliquidExchange::applyPhantomLevelUpdate`:

```cpp
if (symbol->findOrder(order.order_id)) {
    continue;   // belongs to the simulator's own user — never touch it
}
```

### Why `feed_md_level_quantity_` exists

The L3 book's own level totals include both kinds of order. But when the feed says "this level is now
3 BTC", that 3 BTC describes only the *real* market — it knows nothing about your resting 1 BTC. To
apply the update correctly the code needs the phantom-only total, which is what
`feed_md_level_quantity_` tracks and `getMDLevelQty(price)` exposes:

```cpp
// The level quantity excludes exchange orders
qty_t getMDLevelQty(price_t price) const;
```

It is maintained in `OrderBook::onOrderUpdate`, the self-observer callback. For any order update where
`findOrder(update.order_id)` returns null, the delta is applied to the phantom totals — handling both
the same-price quantity change and the price-move case (subtract from `old_price`, add to `price`).

The incremental-update logic in both adapters then reads:

```cpp
auto md_level_qty = book.getMDLevelQty(price);
if (target_qty > md_level_qty) {
    // real market added liquidity — append a new phantom order at the back
    book.addBookOrder(utils::nextOrderId(), book_side, price, target_qty - md_level_qty, …);
} else if (target_qty < md_level_qty) {
    // real market pulled liquidity — shrink phantom orders from the back, skipping simulator orders
}
```

Reducing **from the back** is a deliberate worst-case assumption: the simulator cannot know which
real orders were cancelled, so it assumes the ones that would have been *behind* you in the queue
survived. That makes your simulated queue position pessimistic rather than optimistic.

### `clearMDOrders()`

A full snapshot invalidates all phantom liquidity, but must not throw away your live orders.
`clearMDOrders()` handles that:

```cpp
void clearMDOrders() {
    // 1. remember every simulator order with leaves_quantity > 0
    // 2. OrderBookL3::clear();  feed_md_level_quantity_.clear();
    // 3. re-insert the remembered simulator orders at their original priority
}
```

Hyperliquid's `processL2Snapshot` calls it on every snapshot. Coinbase's `onLevel2Snapshot` calls the
plain `clear()` instead, which **does** discard resting simulator orders — an inconsistency between
the two adapters.

## Order identity and lookups

Every simulator order carries three identifiers:

| Identifier | Type | Source | Used for |
| --- | --- | --- | --- |
| `id` | `uint64_t` | `utils::nextOrderId()`, a global atomic starting at 1 | The L3 book's key; what phantom orders also use |
| `order_id` | `fixed_string<37>` | Random Boost UUID in `allocateOrder()` | The exchange order id the client sees |
| `client_order_id` | `fixed_string<37>` | Client-supplied | Client-side correlation |

Those two, plus `symbol` and `user_id`, live in an `OrderIdentity` base that `Order` derives from,
laid out to match the header of an `OrderResponse` byte for byte. That is what lets
`setOrderIdentity()` fill a response's four id fields with a single memcpy; `messages.hpp`
static_asserts the layout, so resizing a field on either side fails the build rather than quietly
shifting the ids on the wire. They behave like strings at the call site — assignment, `.empty()`,
comparison, `std::format` — with one exception: **`LOG_*` calls need `.view()`**, because
slick-logger dispatches on exact argument types and falls through to `std::to_string` otherwise. That
is a compile error, not a silent wrong value.

`Symbol::findOrderByClientOrderId` is preferred over `findOrderByOrderId` at every call site — the
modify and cancel handlers only fall back to `order_id` when `client_order_id` is empty.

The two string indices are populated in `addOrder` only when non-empty, and must be erased in lockstep
with `orders_`. Four methods do that: `deleteOrder(Order*, timestamp, …)`, `deleteOrder(Order*)`,
`modifyOrder` (when leaves reaches 0), and `executeOrder` (likewise). Any new mutation path has to
maintain all three maps together.

## Allocation

Orders come from a `slick::ObjectPool<Order>` sized to the book's `buffer_size` (65,536 by default):

```cpp
Order* allocateOrder() {
    auto* order = order_buffer_.allocate();

    static thread_local boost::uuids::random_generator uuid_gen;
    const boost::uuids::uuid u = uuid_gen();
    boost::uuids::to_chars(u, order->order_id.data());   // 36 chars, straight into the order
    order->order_id.data()[36] = '\0';

    order->id = utils::nextOrderId();
    order->created_time = utils::get_current_time_ns();
    order->last_update_time = order->created_time;
    order->resetFillAccounting();
    return order;
}
```

The generator is `thread_local` rather than constructed per call: building one seeds a Mersenne
twister from the OS entropy source, which cost far more than the id it produced. Orders are allocated
on the exchange thread, and `thread_local` keeps this correct if that ever stops holding. A rendered
UUID is exactly 36 characters, which is the capacity of `order_id`, so it is written straight into the
order's own buffer — a `static_assert` ties the two together.

The pool recycles memory, so `allocateOrder` hands back a *previously used* `Order`.
`resetFillAccounting()` clears the fill state (`cum_quantity`, `cum_value`, `avg_fill_price`,
`last_fill_*`, `num_fills`, `fee`, `filled_value`), and `Exchange::handleNewOrderRequest` then assigns
symbol, side, type, TIF, price, quantity, leaves, status, `client_order_id` and `user_id`. Fields
outside both sets — `reject_message`, `cancel_message`, `edit_history` — still carry over from
whatever order last occupied the slot.

`freeOrder` returns an order to the pool. `Symbol::addOrder` calls it for anything that did not rest —
fully filled orders, rejects and IOC remainders alike — `modifyOrder` when `leaves_quantity == 0`, and
`Symbol::cancelOrder` unconditionally.

## Queue priority

```cpp
static uint64_t nextOrderPriority() noexcept { return ++next_priority_; }
inline uint64_t OrderBook::next_priority_ = 0;
```

`next_priority_` is a `static` member, so it is **shared by every order book in the process** — all
symbols, all venues. It is also a plain `uint64_t` incremented without atomics. Today that is safe
because only the exchange thread mutates books, but a second mutating thread would race.

Priority is assigned in `addOrder` and reassigned in `modifyOrder` on a price change. Phantom orders
get priorities from the same counter, which is what makes them queue correctly against simulator
orders.

## Execution

`executeOrder` is overridden to keep the `Order` in step with the L3 book:

```cpp
bool executeOrder(OrderId order_id, Quantity executed_quantity, uint64_t timestamp, …) override {
    auto* order = findOrder(order_id);
    if (order) {
        order->leaves_quantity -= executed_quantity;
        order->cum_value       += cum_value_t(order->price) * cum_value_t(executed_quantity);
        order->cum_quantity    += executed_quantity;
        order->avg_fill_price   = order->cum_value / order->cum_quantity;   // integer, see below
        order->last_fill_price  = order->price;
        order->last_fill_qty    = executed_quantity;
        order->last_fill_time   = timestamp;
        order->num_fills       += 1;
    }
    auto rt = OrderBookL3::executeOrder(order_id, executed_quantity, timestamp, …);
    if (order && order->leaves_quantity == 0) { /* erase from all three maps */ }
    return rt;
}
```

The `if (order)` guard is what makes it safe to call for phantom orders too.

Note `order->last_fill_price = order->price` — the *order's own* price, not a trade price passed in.
That is correct here because this only ever runs against the **resting** side: `executeOrder` is
called with the book order's id, and a resting order trades at its own price. An aggressor's fill
price is the level it matched, which `FifoMatchingEngine::match` writes onto the aggressor directly;
it is never routed through this function, because an order does not enter `orders_` until after it
has finished matching.

The fill accounting here mirrors the aggressor-side arithmetic in the matching engine, including the
`cum_value` running notional that keeps `avg_fill_price` free of accumulated rounding error — see
[average fill price](matching-engine.md#average-fill-price). The two copies have to stay in step.

## Snapshot generation

Three virtual methods turn book state into `md_queue_` frames. Only the `L2` specialisations are
implemented.

| Method | Emits | Called by |
| --- | --- | --- |
| `populateL2SubscriptionResponse(queue, channel)` | `SUB_RESPONSE` + `BookSnapshot` | Adapters, on a first subscription |
| `populateL2Snapshot(queue)` | `BOOK_SNAPSHOT` + `BookSnapshot` | Adapters, on routine full-book updates |
| `populateMDBookUpdate(MDBookUpdate&)` | Top-10-per-side into a caller-owned struct | `Exchange::publishMDBookUpdate` (currently unused) |

The snapshot layout is bids first, then asks, with `num_bid` and `num_ask` counts — consumers index
`levels[0 .. num_bid)` for bids and `levels[num_bid .. num_bid+num_ask)` for asks. Both publishers
rely on exactly that convention.

`to_string()` renders a 20-level ladder for debug logging. It calls `std::advance(it, -20)` on
`rend()` without checking the book has 20 ask levels, so it is only safe on a well-populated book.

## Tests

- `tests/unit/order_book/test_orderbook_operations.cpp` — add, modify, delete, execute.
- `tests/unit/order_book/test_orderbook_lookup.cpp` — the three lookup indices and their invariants.
