# Matching engine

`FifoMatchingEngine` implements price/time priority matching. It is the only engine that exists —
`MatchingEngine::Type::ProRata` is declared but
[unimplemented](known-gaps.md#pro-rata-matching-does-not-exist).

One engine instance is shared by every symbol on a venue; it holds no per-symbol state, taking the
`OrderBook` as a parameter on each call. Per-symbol configuration (currently just the SMP mode) is
passed in from `Symbol`.

## Two entry points

The interface has two `match` overloads, and they exist for genuinely different reasons.

```cpp
// 1. A client order arrives.
std::tuple<OrdRejectReason, std::vector<TradeSummaryInfo>>
match(Order* order, price_t order_price, qty_t order_qty, OrderBook& book,
      time_t request_time, time_t event_time, uint64_t seq_num = 0,
      SelfMatchPreventionMode smp_mode = SelfMatchPreventionMode::NONE);

// 2. Phantom liquidity arrives from the market-data feed.
std::vector<TradeSummaryInfo>
match(Side side, uint64_t order_id, price_t price, qty_t& qty, OrderBook& book,
      time_t event_time, uint64_t seq_num = 0);
```

**Overload 1** is the client path, called from `Symbol::addOrder` and `Symbol::modifyOrder`. It owns
a real `Order`, publishes acks and execution reports, and honours time-in-force and SMP.

**Overload 2** is the market-data path, called whenever the feed introduces a new price level. Before
a phantom order can rest at a price, the engine checks whether that price crosses any *simulator*
order resting on the opposite side — if the real market just quoted 100.00 bid and you have a resting
offer at 99.99, you would have been filled on the real venue, so you get filled here. It takes `qty`
**by reference** and decrements it: whatever is left after sweeping your orders is what actually rests
as phantom liquidity. No ack is published (there is no client order to ack), and fully-filled
simulator orders are deleted from the book inside the loop.

Both dispatch on side into a shared `template<Side SIDE>` implementation, where `SIDE` is the
**resting** side being swept — so the aggressor is `opposite_side<SIDE>()`. A buy order matches
against `match<Side::SELL>`.

## The client matching path, step by step

### Step 0 — ack

If `order->status == PENDING_NEW`, `publishOrderAck` fires immediately, before any matching. The
client's `NEW` execution report therefore always precedes its fills.

### Step 1 — fill-or-kill pre-check

For `TimeInForce::FILL_OR_KILL`, `canFillCompletely<SIDE>()` walks the opposite side of the book
accumulating available quantity until it either covers the order or hits a price level worse than the
limit. The scan is SMP-aware: under `CANCEL_NEWEST` any self-match aborts the scan and returns false;
under `CANCEL_RESTING` self-matched orders are skipped, because they would be cancelled rather than
filled.

If the scan fails, `publishOrderCancel` fires and the engine returns `FOK_CANNOT_FILL` with no trades.

### Step 2 — apply a pending replace

If `order->status == PENDING_REPLACE`, the new quantity and price are applied before matching:

```cpp
order->leaves_quantity += order_qty - order->quantity;  // restore any earlier reduction
if (order->leaves_quantity < 0) order->leaves_quantity = 0;
order->quantity = order_qty;
order->price    = order_price;
publishOrderModify(order, order_price, order_qty, request_time);
```

The delta arithmetic preserves fills: an order for 10 that has filled 4 and is amended to 7 ends up
with `leaves_quantity == 3`. If the amendment drives leaves to zero, the order is cancelled and the
engine returns immediately.

### Step 3 — the matching loop

```cpp
while (order_qty > 0) {
    auto best_level = book.getBestLevel<SIDE>();
    if (!best_level ||
        (order_price != NULL_PRICE && !isPriceBetterOrEqual<SIDE>(best_level->price, order_price)))
        break;

    auto& book_order = *best_level->orders.begin();   // FIFO: front of the level
    // … SMP check …
    qty_t trade_qty = std::min(order_qty, book_order.quantity);
    // … update aggressor fields, publishOrderExecution(order) …
    book.executeOrder(book_order_id, trade_qty, event_time, seq_num, order_qty == 0);
    // … accumulate trade summary …
}
```

Time priority comes from always taking `orders.begin()` — the front of the price level, ordered by
the `priority` counter. Price priority comes from `getBestLevel<SIDE>()` walking levels best-first.

`NULL_PRICE` doubles as the market-order sentinel: when `order_price == NULL_PRICE` the price test is
skipped entirely and the order sweeps until filled or the book is empty.

The `is_last_in_batch` argument to `executeOrder` is `order_qty == 0`, so observers see the batch
close on the final fill.

### Step 4 — immediate-or-cancel remainder

After the loop, if the TIF is `IMMEDIATE_OR_CANCEL` and quantity remains, `publishOrderCancel` fires.
The order is not removed from anything here — it was never added to the book. `Symbol::addOrder`
handles that:

```cpp
bool should_rest = (tif == DAY || tif == GOOD_TILL_CANCEL || tif == GOOD_TILL_DATE);
```

Only those three rest. IOC and FOK remainders are dropped, and — because the test is a positive
allow-list — so are `AT_THE_OPENING` and `GOOD_TILL_CROSSING`.

Finally, `Symbol::addOrder` returns the `Order` to the object pool if `leaves_quantity == 0`.

## Self-match prevention

SMP compares `Order::client_id` between the incoming order and each resting order:

```cpp
if (incoming->client_id == -1 || book_order->client_id == -1) return false;
return incoming->client_id == book_order->client_id;
```

| Mode | Behaviour on a self-match |
| --- | --- |
| `NONE` | Allow the match |
| `CANCEL_RESTING` | Cancel the resting order — removed from the book, `CANCELED` sent to its owner, `Order` returned to the pool — then carry on matching against the next one |
| `CANCEL_NEWEST` | Reject the incoming order with `OrdRejectReason::SMP`, no trades |

### `CANCEL_NEWEST` decides before it touches anything

The check runs at the top of `match()`, ahead of the order ack, ahead of the
`PENDING_REPLACE` mutation, and ahead of the FOK pre-validation. `wouldSelfMatch` walks the opposing
side as far as the incoming quantity could actually reach and asks whether any of it is the order's
own.

That ordering is the whole point. The matching loop mutates before it matches — an amendment writes
the new price and quantity onto the stored `Order` and reports `REPLACED`, and a new order is acked —
so deciding mid-loop would mean telling the client its order was replaced or working and then
rejecting it, with the amendment half-applied to an order that never moved on the book. Deciding
first means a rejected order leaves no trace: the client sees its pending, then one `REJECTED`, and
the stored order keeps its original price and quantity.

Running before the mutation does mean the pre-check has to derive the quantity the loop is *about*
to use rather than reading it afterwards. The `order_qty` argument is the order's total quantity; for
a partially filled amendment the loop will match only `leaves_quantity + (order_qty - quantity)`.
Scanning by the total would reach past what the order can actually trade and reject against a
resting order it would never have touched, so STEP 0 mirrors that arithmetic without applying it.

It also means the reject reason is `SMP` rather than `FOK_CANNOT_FILL` for a fill-or-kill order that
could have filled but for the self-match — the liquidity was there, and SMP is why it could not
trade.

The mode is per-`Symbol` (`smp_mode_`), passed into `match()` by both `Symbol::addOrder` and
`Symbol::modifyOrder` — an amendment re-enters the matching loop, so it is prevented on the same
terms as a new order.

### Enabling it

SMP is **off by default**. Set `self_match_prevention` on an exchange to `"cancel_resting"` or
`"cancel_newest"` and every symbol that venue creates inherits it — see
[Configuration](configuration.md#self_match_prevention). An unrecognised value logs a warning and
leaves SMP disabled rather than aborting startup.

### What counts as "self"

`Order::client_id` is the identity SMP compares, and `Exchange::clientIdFor` assigns it from the
order's **`user_id`** — the authenticated account, which is how real venues scope SMP. Ids are
handed out on first sight of a `user_id` and stay stable for the life of the process, so the hot
matching loop compares an `int` rather than a string.

An order with an empty `user_id` gets `-1`, which `isSelfMatch` treats as "no identity" and never
matches — including against another `-1`. An unauthenticated client is therefore left out of SMP
rather than lumped together with every other unauthenticated one.

## Modifies re-enter the matching loop

`Symbol::modifyOrder` does not adjust the book in place and stop. It calls the full `match()` path
with the new price and quantity, which means an amendment that makes the order marketable trades
immediately. Only afterwards does it call `order_book_->modifyOrder(...)` to reposition whatever is
left.

Queue priority is handled by the book, not the engine:

```cpp
// OrderBook::modifyOrder
else if (new_price != order->price) {
    order->price = new_price;
    order->priority = nextOrderPriority();   // price change ⇒ lose queue position
}
order->quantity = new_qty;
```

A pure quantity change keeps its place in the queue; a price change goes to the back of the new
level. That matches real venue behaviour. Note the asymmetry: quantity *increases* also keep
priority, which most real venues would not allow.

## Trade summaries

Each `match()` call returns `std::vector<TradeSummaryInfo>` — the public print of what happened, which
the exchange forwards to `md_queue_` via `publishTradeSummary`.

Summaries are aggregated **per price level**. While consecutive fills happen at the same price the
engine merges them into the current summary:

```cpp
if (last_fill_price != price) {
    // new summary: num_orders = 2, Trades = [aggressor, resting]
} else {
    last_summary.num_orders += 1;
    last_summary.qty        += trade_qty;
    last_summary.Trades.front().qty += trade_qty;   // grow the aggressor's leg
    last_summary.Trades.emplace_back(Trade{book_order_id, trade_qty});
}
```

So `Trades[0]` is always the aggressor with its running total, and `Trades[1..]` are the resting
orders it hit. `num_orders` counts participants, not trades — it starts at 2 and increments by one per
additional resting order.

`TradeSummaryInfo::trade_id` comes from `MatchingEngine::nextTradeId()`, a process-global atomic
shared across all venues — so it is an internal id only. The **public** trade id is stamped by
`Exchange::publishTradeSummary` from a counter the venue owns, which is what makes each venue's ids
an independent sequence.

`phantom_qty` records how much of a summary's `qty` was matched against phantom liquidity rather than
the simulator's own resting orders (`our_order == nullptr` in the match loop). The exchange uses it
to avoid reducing a price level twice — see
[venue trade prints](market-data.md#venue-trade-prints-are-matched-not-relayed).

!!! note
    `num_orders` is used by `Exchange::publishTradeSummary` to size the queue reservation
    (`sizeof(TradeSummary) + num_orders * sizeof(Trade)`), while the copy loop iterates
    `trade_summary.Trades.size()`. The two agree because `num_orders` is incremented exactly once per
    `Trades` entry after the initial pair — but they are independent counters, so any future change to
    one must update the other.

## Fixed-point arithmetic

Prices and quantities are `int_fast64_t` scaled by `DOUBLE_MULTIPLIER = 1e8`
([`types.hpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/common/types.hpp)):

```cpp
using price_t = int_fast64_t;
using qty_t   = int_fast64_t;
constexpr int32_t DOUBLE_MULTIPLIER = static_cast<int32_t>(1e8);
constexpr price_t NULL_PRICE = std::numeric_limits<price_t>::max();

to_price_t(1.23)      // → 123000000
to_price_double(…)    // → back to double
```

Eight decimal places suits crypto; a price of 123.456 is exact, and quantities down to one satoshi
are representable. All comparisons in the matching loop are integer comparisons via
[`utils::isPriceBetterOrEqual`](https://github.com/SlickQuant/slick-sim/blob/main/src/utils/price.hpp),
which flips direction on side.

`NULL_PRICE` is `INT64_MAX`. Because it is also a valid `price_t`, code must test for it explicitly
before doing arithmetic — the matching loop does, but be careful when adding new paths.

!!! warning "Average fill price drifts"
    `avg_fill_price` is recomputed by converting to `double`, doing the weighted average, and
    converting back, on *every* fill — in both the engine and `OrderBook::executeOrder`. Errors
    compound across partial fills. See
    [Known gaps](known-gaps.md#avg_fill_price-is-recomputed-in-floating-point-on-every-fill).

## What the engine does not do

- **`post_only` is not enforced.** The flag is parsed from Coinbase's `limit_limit_gtc.post_only`,
  stored on the `Order`, and echoed in execution reports, but the matching loop never checks it. A
  post-only order that crosses will trade.
- **Stop and trigger orders do not exist.** `AddOrderMessage` carries `take_profit_price` and
  `stop_loss_price`; both gateways always set them to `NULL_PRICE`, and nothing reads them. The
  Hyperliquid gateway explicitly downgrades `trigger` orders to plain limits.
- **`GOOD_TILL_DATE` never expires.** `AddOrderMessage::expire_time` is always written as 0 and never
  evaluated.
- **There is no market state.** No open/close/halt, so `MARKET_CLOSED` and `MARKET_HALTED` are
  unreachable.
- **There are no fees in the engine.** `Order::fee` stays 0; the Coinbase WS gateway computes a
  display-only fee from a `fee_rate_` when building execution reports.

## Tests

The engine is the best-covered part of the codebase:

| File | Covers |
| --- | --- |
| `tests/unit/matching_engine/test_fifo_matching_engine.cpp` | Core price/time priority, partial fills |
| `tests/unit/matching_engine/test_matching_engine_tif.cpp` | DAY/GTC resting, IOC remainder cancel, FOK pre-check |
| `tests/unit/matching_engine/test_matching_engine_smp.cpp` | All three SMP modes, including the FOK interaction |
| `tests/unit/matching_engine/test_coinbase_market_data_matching.cpp` | The overload-2 market-data path |
