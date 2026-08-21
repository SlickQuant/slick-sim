# Adding an exchange

This is a checklist for implementing a new venue adapter, derived from the two that work. Read
[Architecture](architecture.md) first — the threading model determines most of the design.

Concretely you need five pieces: a `Venue` enum entry, an `Exchange` subclass, an `MDFeed`, an
`OrderGateway`, and a `MarketDataPublisher`. Everything venue-specific lives in **one directory**,
`src/venues/<name>/`, built as one optional target — so beyond the enum line, the only wiring is that
directory's own `CMakeLists.txt`.

Nothing in the core, and no existing venue, has to be edited to add a new one.

## 1. Add the `Venue` enumerator

[`src/common/types.hpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/common/types.hpp)
generates the enum, `to_string` and `to_venue` from one X-macro list, so a venue is a single line:

```cpp
#define SLICK_SIM_VENUE_LIST(X)   \
    X(CME,         "CME")         \
    X(ICE,         "ICE")         \
    X(COINBASE,    "COINBASE")    \
    X(HYPERLIQUID, "HYPERLIQUID") \
    X(STOCK,       "STOCK")       \
    X(KRAKEN,      "KRAKEN")      // ← append here
```

`to_venue` matches case-insensitively, so `kraken`, `KRAKEN` and `Kraken` all resolve.

!!! danger "Append only"
    `Venue` is written into every `MarketDataUpdate` frame and into the shared-memory md queue.
    Inserting in the middle renumbers every venue after the insertion point, which silently
    reinterprets any queue or capture file another process is reading.

This list is deliberately **not** conditional on the build options. A `Venue` is an identity on the
wire, not a claim that an adapter was compiled in: `to_venue("kraken")` still resolves in a build with
the Kraken adapter switched off, which is what lets a capture recorded elsewhere be read here.

## 2. Subclass `Exchange`

```cpp
class KrakenExchange : public Exchange, public md_feed::KrakenFeedCallbacks {
public:
    explicit KrakenExchange(const nlohmann::json& config);
protected:
    void poll() override;                                       // the per-tick pump
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

### `poll()` — the per-tick pump

Do **not** override `start()`. The base class owns the thread and the loop; you supply only what runs
each iteration:

```cpp
void KrakenExchange::poll() {
    drainEventQueue();      // release whatever the feed callbacks queued
}
```

`Exchange::start()` starts the gateways, the publisher and every feed in `md_feeds_`, then spins
`poll(); processRequest();` until `stop()`. `poll()` defaults to doing nothing, which is exactly right
for a venue with no live feed.

You do not need to override `stop()` either. The base stops the feeds, gateways and publisher and
joins the exchange thread — in that order, joining the thread **before** touching `md_feeds_`, because
`poll()` and `handleMdUnsubscription` both run on it. It is also idempotent: `main()` stops each
exchange and `~Exchange()` stops it again, so the second pass returns immediately rather than
unsubscribing a feed twice.

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

Also implement `handleMdUnsubscription` — the request loop dispatches `MD_UNSUBSCRIPTION` to it, so
an adapter that omits it leaks upstream subscriptions.

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
utils::copy_wire_field(request->symbol, sym);
request->msg_type = MessageType::NEW_ORDER_SINGLE;
// … fill request->add_order …
request_queue_.publish(index);
```

Always fill the fixed-size char arrays with
[`utils::copy_wire_field`](https://github.com/SlickQuant/slick-sim/blob/main/src/utils/fixed_string.hpp).
`Request` is recycled memory and the reader parses these fields as C strings, so a hand-written copy
has three things to get right and the obvious spellings each miss one:

- `memcpy(dst, s.c_str(), sizeof(dst))` reads past the end of any name shorter than the field, and
  drops whatever followed it in memory into a field the reader treats as a C string.
- `memset` + `memcpy(dst, s.c_str(), std::min(sizeof(dst), s.size()))` is bounded but clamps one byte
  too late: a value that exactly fills the field overwrites the terminator the `memset` left.
- Neither clears the tail on a *shorter* value written over a longer one in a recycled slot.

`copy_wire_field` truncates to N-1, zero-fills the remainder, and returns `true` when the value did
not fit, for callers that want to warn.

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
Handle `TRADE_SUMMARY` too if your venue has a trades channel: it carries one executed trade, and it
is the only source of the public tape. Do not expect a raw relay of the venue's own prints — those
are replayed through the matching engine and reach you as fills.

On a client `subscribe` message, write an `MD_SUBSCRIPTION` request:

```cpp
auto index = request_queue_.reserve();
auto* request = request_queue_[index];
utils::copy_wire_field(request->symbol, coin);
request->msg_type = MessageType::MD_SUBSCRIPTION;
request->md_subscription.channel = channel_index;
request->md_subscription.client  = (void*)ws;
request_queue_.publish(index);
```

Track subscriptions in both directions — symbol → set of sockets (for fan-out) and per-socket
pending/active sets (to know when to send the subscription confirmation).

## 6. Register the venue

One small translation unit in your venue directory, `src/venues/kraken/kraken_venue.cpp`:

```cpp
#include "kraken_exchange.hpp"
#include <exchange/exchange_registry.hpp>
#include <nlohmann/json.hpp>

using namespace slick::sim;
using namespace slick::sim::exch;

SLICK_SIM_REGISTER_VENUE("kraken", Venue::KRAKEN, KrakenExchange);
```

The string is the config key under `exchanges`, matched case-insensitively. `main.cpp` names no venue
and needs no edit; an enabled key with no registered adapter is a fatal startup error that lists what
the binary does carry.

## 7. Wire up the build

Create `src/venues/kraken/CMakeLists.txt`:

```cmake
slick_sim_add_venue(kraken
    SOURCES
        kraken_exchange.cpp
        kraken_live_ws_feed.cpp
        kraken_rest_order_gateway.cpp
        kraken_publisher.cpp
        kraken_venue.cpp
    DEPENDS
        kraken-sdk          # whatever your venue SDK's imported target is called
        slick::net          # your networking stack — see below
        slick_sim_rest_ws   # the uWebSockets bases, if you serve HTTP/WebSocket
)
```

`DEPENDS` is where the adapter names everything it links that the core does not, **including its
networking stack**. Coinbase and Hyperliquid speak HTTP/WebSocket and name `slick::net`; an adapter for
a binary session protocol — CME iLink, ICE — names `slick::socket` and must not name slick-net. No core
target links either, and no venue inherits another's, so an adapter gets exactly what it declares.

Resolve your dependencies in **that same file**, above the `slick_sim_add_venue()` call — not in the
top-level `CMakeLists.txt`. The directory is added only when your option is on, so nothing here re-tests
it:

```cmake
find_package(kraken-sdk CONFIG QUIET GLOBAL)      # or FetchContent
if (NOT kraken-sdk_FOUND)
    FetchContent_Declare(kraken-sdk GIT_REPOSITORY ... GIT_TAG ...)
    FetchContent_MakeAvailable(kraken-sdk)
endif()

# Your networking stack. A no-op if the SDK above already brought it in as a
# build-tree target, which find_package cannot see.
slick_sim_find_venue_dependency(slick-net slick::net)

# Only if your gateway derives from RestWsOrderGateway or your publisher from
# WebsocketMarketDataPublisher: creates the shared target that compiles them, and
# resolves uWebSockets. Nothing in the core does either, so a venue that serves a
# binary session protocol never pulls uWebSockets into the build.
slick_sim_require_rest_ws()
```

!!! warning "`find_package` in a venue directory needs `GLOBAL`"
    An imported target is visible only in the directory that created it and below, while `slick-sim`
    (`src/`) and `slick_sim_tests` (`tests/`) link your object library from sibling scopes and have to
    resolve every name in its interface. Without `GLOBAL`, generate fails with *"links to target
    `kraken-sdk` but the target was not found"*. `slick_sim_find_venue_dependency()` passes it for you;
    a `find_package` you write yourself must pass it too. Targets created by `FetchContent` are real,
    not imported, so they need nothing.

Two lines then remain outside your directory — the option, in the top-level `CMakeLists.txt`, and the
subdirectory, in `src/venues/CMakeLists.txt`:

```cmake
option(SLICK_SIM_ENABLE_KRAKEN "Build the Kraken venue adapter" ON)
```

```cmake
if(SLICK_SIM_ENABLE_KRAKEN)
    add_subdirectory(kraken)
endif()
```

That is the whole build change. `slick_sim_add_venue` links your target against `exchange` (which
carries every core header transitively), defines `SLICK_SIM_HAS_KRAKEN` for consumers, and adds the
venue to the list that `slick_sim_link_venues()` feeds to both `slick-sim` and `slick_sim_tests`.
Because `DEPENDS` is linked `PUBLIC`, your SDK and your networking stack reach the executable and the
test binary without either of them naming a venue library.

If your stack is `slick::net`, `main.cpp`'s bridge from its log output into slick-logger switches on by
itself: `slick_sim_link_venues()` defines `SLICK_SIM_HAS_SLICK_NET` when any enabled venue's `DEPENDS`
names it. A different stack with its own log handler gets its own flag the same way — one `if` in
`slick_sim_link_venues()` keyed on the library, never on "a venue is enabled".

!!! danger "Venue targets must be OBJECT libraries"
    `slick_sim_add_venue` declares one, deliberately. `SLICK_SIM_REGISTER_VENUE` registers from a
    namespace-scope initialiser that nothing in the core ever references by name — out of a *static*
    library the linker discards the whole object as unreferenced, and the venue vanishes from the
    registry with no build error at all. `ExchangeRegistryTest.EnabledVenuesAreRegistered` exists to
    catch exactly that regression.

## 8. Tests

Put the suites in `tests/unit/venues/kraken/` and add a gated block to `tests/CMakeLists.txt` — keyed
on the **target**, so the suite disappears with the venue:

```cmake
if(TARGET slick_sim_venue_kraken)
    list(APPEND SLICK_SIM_TEST_SOURCES
        unit/venues/kraken/test_kraken_exchange.cpp
    )
endif()
```

Mirror the existing suites:

| Reference | What to copy |
| --- | --- |
| `tests/unit/venues/coinbase/test_coinbase_exchange.cpp` | Adapter-level: subscription handling, snapshot application, phantom-level deltas |
| `tests/unit/matching_engine/test_feed_liquidity_matching.cpp` | Feed liquidity crossing a resting simulator order (venue-agnostic) |
| `tests/unit/venues/hyperliquid/test_hyperliquid_info_proxy.cpp` | Pure request/response translation, tested without a network |

`tests/unit/test_helpers.hpp` has the shared fixtures — from a venue directory that is
`#include "../../test_helpers.hpp"`. The project convention (from `CLAUDE.md`) is that every bug fix
gets a regression test.

## 9. Check it builds without the others

The point of the split is that venues are independent. Verify that:

```bash
cmake -S . -B build-kraken -DSLICK_SIM_ENABLE_COINBASE=OFF -DSLICK_SIM_ENABLE_HYPERLIQUID=OFF
cmake --build build-kraken
ctest --test-dir build-kraken
```

Configuring with every venue off must still build and link the core — that is the configuration that
catches a core target having quietly picked up a dependency through a venue SDK.

## Checklist

- [ ] One line appended to `SLICK_SIM_VENUE_LIST` in `types.hpp`
- [ ] Everything venue-specific under `src/venues/<name>/` — nothing in the core directories
- [ ] `Exchange` subclass overriding **`poll()`, not `start()`**
- [ ] `addSymbol()` creating an `L2` book and attaching the shared FIFO engine
- [ ] `handleMdSubscription` / `handleMdUnsubscription` with per-channel pending tracking
- [ ] Feed callbacks that enqueue only — no book mutation off the exchange thread
- [ ] `MDFeed` implementation (optional — omit for a self-contained venue)
- [ ] `OrderGateway` (`RestWsOrderGateway` subclass) with `setup_routes`
- [ ] `MarketDataPublisher` (`WebsocketMarketDataPublisher` subclass) handling `SUB_RESPONSE`, `BOOK_SNAPSHOT`, `TRADE_SUMMARY`
- [ ] `SLICK_SIM_REGISTER_VENUE` in a `<name>_venue.cpp`
- [ ] `slick_sim_add_venue()` CMakeLists naming your networking stack in `DEPENDS`, your SDK lookup in
      the same file (with `GLOBAL`), and the `SLICK_SIM_ENABLE_<VENUE>` option
- [ ] Config block documented in [Configuration](configuration.md)
- [ ] Unit tests under `tests/unit/venues/<name>/`, gated on `if(TARGET slick_sim_venue_<name>)`
- [ ] Builds and tests pass with every *other* venue disabled
