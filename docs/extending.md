# Adding an exchange

This is a checklist for implementing a new venue adapter, derived from the two that work. Read
[Architecture](architecture.md) first — the threading model determines most of the design.

Concretely you need five pieces: a `Venue` enum entry, an `Exchange` subclass, an `MDFeed`, an
`OrderGateway`, and a `MarketDataPublisher`. Plus CMake wiring, a `main.cpp` dispatch entry, and tests.

## 1. Register the venue

[`src/common/types.hpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/common/types.hpp):

```cpp
enum Venue : uint8_t {
    UNKNOWN_VENUE, CME, ICE, COINBASE, HYPERLIQUID, STOCK,
    KRAKEN,        // ← add before __COUNT__
    __COUNT__,
};
```

Add matching arms to `to_string(Venue)` and `to_venue(std::string_view)`. `to_venue` accepts both
upper and lower case; the config key must match one of the strings it recognises.

Insert **before** `__COUNT__` but be aware `Venue` is written into every `MarketDataUpdate` frame, so
inserting in the middle renumbers the existing venues. If any shared-memory queue or capture file is
being read by another process, append instead.

## 2. Subclass `Exchange`

```cpp
class KrakenExchange : public Exchange, public md_feed::KrakenFeedCallbacks {
public:
    explicit KrakenExchange(const nlohmann::json& config);
    void start() override;
protected:
    void handleMdSubscription(const Request& request) override;
    void handleMdUnsubscription(const Request& request) override;
private:
    Symbol* addSymbol(std::string_view symbol);
};
```

### Constructor

Call `Exchange(Venue::KRAKEN, config)` first — the base constructor builds the three queues and throws
if `order_gateway` or `md_publisher` is missing. Then construct your publisher, gateways and feeds:

```cpp
KrakenExchange::KrakenExchange(const nlohmann::json& config)
    : Exchange(Venue::KRAKEN, config)
{
    md_publisher_ = std::make_unique<KrakenPublisher>(config["md_publisher"], request_queue_, md_queue_);
    order_gateways_.emplace_back(
        std::make_unique<KrakenRestOrderGateway>(config["order_gateway"], request_queue_, response_queue_));

    for (const auto& feed_cfg : config.value("md_feeds", nlohmann::json::array())) {
        if (feed_cfg.value("type", "") == "kraken_live_ws") { /* … */ }
    }
}
```

### `start()` — the one thing you must not get wrong

```cpp
void KrakenExchange::start() {
    run_.store(true, std::memory_order_release);
    for (auto& og : order_gateways_) og->start();
    md_publisher_->start();
    for (auto& feed : md_feeds_) feed->start();

    thread_ = std::thread([this]() {
        while (run_.load(std::memory_order_relaxed)) {   // ← the loop is mandatory
            drainEventQueue();
            processRequest();
        }
    });
}
```

!!! danger "Do not inherit the base `start()`"
    `Exchange::start()` calls `processRequest()` **once**, with no loop. An adapter that forgets to
    override it will start its gateways, process a single order, and then go silent with no error.
    It also null-checks `md_publisher_` rather than requiring one, so forgetting to construct a
    publisher fails just as quietly. See
    [Known gaps](known-gaps.md#an-enabled-venue-key-with-no-adapter-starts-but-does-nothing).

You do not need to override `stop()`; the base implementation stops gateways and publisher and joins
the thread.

### Symbol creation

```cpp
Symbol* KrakenExchange::addSymbol(std::string_view symbol) {
    auto* sym = SymbolManager::instance().createSymbol(symbol, Venue::KRAKEN);
    if (!matching_engines_[engine::MatchingEngine::Type::FIFO]) {
        matching_engines_[engine::MatchingEngine::Type::FIFO] =
            std::make_unique<engine::FifoMatchingEngine>(response_queue_);
    }
    sym->matching_engine_ = matching_engines_[engine::MatchingEngine::Type::FIFO].get();
    sym->createOrderBook<OrderBookType::L2>();
    return sym;
}
```

Use `L2`, not `L3` — the [L3 specialisation's `populate*` methods are stubs](known-gaps.md#l3-order-books-are-stubs).
The engine is created lazily and shared by every symbol on the venue.

Remember `SymbolManager` is global and keyed by symbol name across all venues. If your venue's symbols
could collide with an existing venue's, namespace them.

### Subscription handling

`handleMdSubscription` runs on the exchange thread. The pattern both adapters follow:

1. Look up the symbol; `addSymbol` it if new.
2. Increment `num_subscriptions_`.
3. If using a live feed and the symbol is new, attach it to a feed and insert it into
   `pending_md_subscription_[channel]` — the snapshot will come from upstream.
4. If the symbol already has a book and is **not** pending, call
   `populateL2SubscriptionResponse(md_queue_, channel)` immediately.

Track pending state per channel, not per symbol: a client can subscribe to a second channel for a
symbol that already has an active subscription on the first.

Also implement `handleMdUnsubscription` for symmetry, but know that
[the request loop never calls it](known-gaps.md#market-data-unsubscribe-requests-are-dropped).

### Feed callbacks

**Configure the venue SDK for user-thread dispatch, and pump it from the exchange loop.** Both
existing adapters do this, and it is what keeps the whole market-data path single-threaded and
lock-free:

| Venue | Mechanism | Pumped by |
| --- | --- | --- |
| Coinbase | Implement `coinbase::UserThreadWebsocketCallbacks`; the WS client fills a `slick::stream_buffer_multiplexer` | `processData()` in the exchange loop |
| Hyperliquid | Construct `hyperliquid::Info` with `user_thread_dispatch = true` | `getInfo()->dispatch(100)` in `drainEventQueue()` |

With that in place the SDK's network thread only buffers bytes, and your callbacks fire **on the
exchange thread** — so they may touch the book directly. If your SDK offers no such mode and insists
on calling back from its own thread, then you must enqueue and drain, because the book has no locking
whatsoever.

The existing adapters still queue events, but for **sequencing**, not thread safety:

```cpp
void KrakenExchange::onBookUpdate(const nlohmann::json& msg) {
    event_queue_.push_back({FeedEvent::Type::BOOK, msg});   // deferred, not cross-thread
}
```

Hyperliquid uses a plain `std::vector<FeedEvent>` drained immediately after the `dispatch()` call
that filled it, which keeps book mutation out of the SDK's callback re-entrancy. Coinbase uses a
per-symbol `std::priority_queue` to re-order by exchange timestamp, because its channels can deliver
out of order — see [Market data](market-data.md#coinbase-time-ordered-event-sequencing). Pick based on
what your venue actually needs; a venue that delivers in order needs neither.

### Applying book updates

For a **new price level**, match against the opposite side before resting the phantom order, so a
quote that crosses a resting simulator order fills it:

```cpp
qty_t qty = level_qty;
auto order_id = utils::nextOrderId();
symbol->matching_engine_->match(side, order_id, price, qty, *symbol->order_book_, event_time_ns, seq);
if (qty > 0) {
    symbol->order_book_->addOrder(order_id, to_book_side(side), price, qty, event_time_ns, seq, true);
}
```

For an **existing level**, adjust phantom quantity only, using `getMDLevelQty()` as the baseline and
skipping any order that `Symbol::findOrder()` resolves. `HyperliquidExchange::applyPhantomLevelUpdate`
is the cleanest reference implementation.

For a **full snapshot**, prefer `clearMDOrders()` over `clear()` — it preserves resting simulator
orders.

## 3. Implement the feed (optional)

```cpp
class KrakenLiveWSFeed : public md_feed::MDFeed {
public:
    void start() override;   // subscribe upstream
    void stop() override;    // unsubscribe
};
```

If the SDK supports user-thread dispatch, use it (as `HyperliquidLiveWSFeed` does with
`user_thread_dispatch = true`) and pump it from the exchange loop — it removes a whole class of race.

A feed is genuinely optional: an adapter with none still works
[self-contained](architecture.md#self-contained-a-conventional-exchange-simulator), matching clients
against each other. Gate the feed-specific paths on a `use_live_feed_`-style flag the way both
existing adapters do, so the same adapter serves both modes.

A **historical replay** feed implements this same interface — `start()` begins replaying recorded
data and `stop()` halts it — and drives the book through exactly the same phantom-order calls
described below. Nothing downstream needs to know the data is not live. None exists yet; see
[Known gaps](known-gaps.md#the-historical-data-feed-does-not-exist).

## 4. Implement the order gateway

For an HTTP/WebSocket venue, subclass
[`RestWsOrderGateway`](https://github.com/SlickQuant/slick-sim/blob/main/src/order_gateway/rest_ws_order_gateway.hpp),
which handles the uWS thread, listen socket, and clean shutdown. You implement two methods:

```cpp
std::string_view type() const noexcept override { return "REST"; }
void setup_routes(uWS::App& app) override;
```

Inside a route handler, the request side is:

```cpp
auto index = request_queue_.reserve();
auto* request = request_queue_[index];
request->time_stamp = utils::get_current_time_ns();
std::memset(request->symbol, 0, sizeof(request->symbol));
std::memcpy(request->symbol, sym.c_str(), std::min(sizeof(request->symbol), sym.size()));
request->msg_type = MessageType::NEW_ORDER_SINGLE;
// … fill request->add_order …
request_queue_.publish(index);
```

Always `memset` the fixed-size char arrays first and clamp with `std::min` — `Request` is recycled
memory, and an unclamped `memcpy` overruns into the next union member.

For the response side you have two options:

- **Synchronous** (Coinbase REST, Hyperliquid REST): capture `response_queue_.initial_reading_index()`
  *before* publishing, then poll `read()` until a response matches. Correlate on
  `user_id` + `client_order_id` for new orders, or on `request_time` for modify/cancel where the
  reject helpers [do not populate the identifiers correctly](order-lifecycle.md#response-path).
  Note this blocks the uWS loop thread.
- **Asynchronous** (Coinbase WS): schedule a recurring drain on the loop with `loop_->defer`, look up
  the client socket by `user_id`, and push the report.

## 5. Implement the market-data publisher

Subclass
[`WebsocketMarketDataPublisher`](https://github.com/SlickQuant/slick-sim/blob/main/src/market_data_publisher/ws_md_publisher.hpp)
and implement three methods:

```cpp
void setup_routes(uWS::App& app) override;                          // ws<PerSocketData>("/*", {...})
void check_heartbeats() override;                                    // called on a timer
void publish_market_data_update(MarketDataUpdate* update) override;  // switch on update->type
```

Handle at least `SUB_RESPONSE` (first snapshot after subscribe) and `BOOK_SNAPSHOT` (routine updates).
Handle `TRADE` too if your venue has a trades channel — the Coinbase publisher's omission of it is a
[known gap](known-gaps.md#the-coinbase-publisher-never-republishes-trades), not a pattern to copy.

On a client `subscribe` message, write an `MD_SUBSCRIPTION` request:

```cpp
auto index = request_queue_.reserve();
auto* request = request_queue_[index];
memcpy(request->symbol, coin.c_str(), sizeof(request->symbol));
request->msg_type = MessageType::MD_SUBSCRIPTION;
request->md_subscription.channel = channel_index;
request->md_subscription.client  = (void*)ws;
request_queue_.publish(index);
```

Track subscriptions in both directions — symbol → set of sockets (for fan-out) and per-socket
pending/active sets (to know when to send the subscription confirmation).

## 6. Wire up the build

Add sources to the relevant `src/*/CMakeLists.txt`. The dependency direction is
`exchange → {md_feed, matching_engine, order_gateway, market_data_publisher}`, so a new venue
typically touches all four plus `src/exchange/CMakeLists.txt`.

## 7. Register in `main.cpp`

```cpp
else if (it.key() == "kraken") {
    if (it.value().value("enabled", true)) {
        exchanges.emplace_back(std::make_unique<slick::sim::exch::KrakenExchange>(it.value()));
        exchanges.back()->start();
    }
}
```

Without this the key falls through to the generic `Exchange` branch and the resulting exchange is dead.

## 8. Tests

Add to `tests/CMakeLists.txt` and mirror the existing suites:

| Reference | What to copy |
| --- | --- |
| `tests/unit/exchange/test_coinbase_exchange.cpp` | Adapter-level: subscription handling, snapshot application, phantom-level deltas |
| `tests/unit/matching_engine/test_coinbase_market_data_matching.cpp` | Feed liquidity crossing a resting simulator order |
| `tests/unit/common/test_hyperliquid_info_proxy.cpp` | Pure request/response translation, tested without a network |

`tests/unit/test_helpers.hpp` has the shared fixtures. The project convention (from `CLAUDE.md`) is
that every bug fix gets a regression test.

## Checklist

- [ ] `Venue` entry plus `to_string` / `to_venue` arms
- [ ] `Exchange` subclass with an **overridden `start()` containing a loop**
- [ ] `addSymbol()` creating an `L2` book and attaching the shared FIFO engine
- [ ] `handleMdSubscription` / `handleMdUnsubscription` with per-channel pending tracking
- [ ] Feed callbacks that enqueue only — no book mutation off the exchange thread
- [ ] `MDFeed` implementation (optional — omit for a self-contained venue)
- [ ] `OrderGateway` (`RestWsOrderGateway` subclass) with `setup_routes`
- [ ] `MarketDataPublisher` (`WebsocketMarketDataPublisher` subclass) handling `SUB_RESPONSE`, `BOOK_SNAPSHOT`, `TRADE`
- [ ] CMake sources
- [ ] `main.cpp` dispatch entry
- [ ] Config block documented in [Configuration](configuration.md)
- [ ] Unit tests
