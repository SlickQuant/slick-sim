# Known gaps

`slick-sim` is early-stage. This page lists everything found in the current source that is
unimplemented, half-wired, or behaves differently from what the surrounding code implies. It exists
so the rest of the documentation can describe the intended design without having to hedge every
paragraph, and so you do not spend an afternoon debugging something that was never finished.

Each entry says what it is, where it lives, and what it means for you.

## Venues

### CME, Eurex and ICE are not implemented

[`src/exchange/exch_cme.hpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/exchange/exch_cme.hpp)
is a **zero-byte file**. There is no `CmeExchange`, no Eurex adapter, and no ICE adapter, despite
`Venue::CME` and `Venue::ICE` existing in
[`types.hpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/common/types.hpp) and
`README.md` describing exchange-adapter namespaces for all three.

**What it means:** only `coinbase` and `hyperliquid` are usable venue keys in the config.

### An enabled venue key with no adapter starts but does nothing

[`main.cpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/sim/main.cpp#L199-L218)
dispatches on the config key: `coinbase` builds a `CoinbaseExchange`, `hyperliquid` builds a
`HyperliquidExchange`, and **anything else** constructs a plain `Exchange`. Only the two concrete
adapters populate `md_publisher_` and `order_gateways_`; the base constructor does not.

`Exchange::start()` now tolerates that — `md_publisher_` is null-checked, so a generic exchange no
longer crashes. What is left is that it does nothing useful:

```cpp
thread_ = std::thread([this]() { processRequest(); });
```

[`Exchange::processRequest()`](https://github.com/SlickQuant/slick-sim/blob/main/src/exchange/exchange.cpp#L68-L93)
reads **one** request from the queue and returns. There is no loop. Both working adapters override
`start()` with a real spin loop; the base class does not. So a generic exchange binds no ports,
serves no clients, processes at most one queued request, and its thread exits.

The committed sample config carries such a block — a `cme` entry — but it is `"enabled": false`, so
it is constructed and then skipped with a warning:

```text
[WARN] Exchange CME is disabled, skipping start
```

**What it means:** leave the `cme` block disabled, or delete it. Enabling it gets you an inert
exchange, not a working one. If you add a venue, populating `md_publisher_` **and** overriding
`start()` with a loop are both mandatory — see [Adding an exchange](extending.md).

!!! note "Previously a crash"
    Until recently `Exchange::start()` dereferenced `md_publisher_` unconditionally and the `cme`
    block segfaulted the process on startup — before either working venue was constructed, since
    `nlohmann::json` iterates object keys in sorted order and `"cme"` sorts before `"coinbase"`.
    Both that and the `enabled` flag being ignored for non-adapter venues are now fixed.

### Startup aborts if `logs/` does not exist

`main.cpp` calls `logger.add_file_sink("logs/slick-sim.log")` with a path relative to the current
working directory, and nothing creates the directory first. Run `slick-sim` from a directory with no
`logs/` subdirectory and the process aborts with exit code 3, writing nothing at all — no console
message, no log, no clue.

The repository ships a `logs/` directory, so this never bites anyone running from the repo root. It
bites immediately when running a packaged release binary from a fresh directory.

**What it means:** `mkdir logs` before the first run in any new working directory.

## Order and market-data plumbing

### Market-data unsubscribe requests are dropped

Both publishers send `MessageType::MD_UNSUBSCRIPTION` requests when a client unsubscribes or
disconnects, but the `switch` in
[`Exchange::processRequest()`](https://github.com/SlickQuant/slick-sim/blob/main/src/exchange/exchange.cpp#L74-L91)
handles only `NEW_ORDER_SINGLE`, `ORDER_REPLACE_REQUEST`, `ORDER_CANCEL_REQUEST` and
`MD_SUBSCRIPTION`. There is no `MD_UNSUBSCRIPTION` case, so `handleMdUnsubscription()` — implemented
on both adapters — is never called from the request loop.

**What it means:** `Symbol::num_subscriptions_` only ever increases, and upstream feeds are never
torn down when the last client goes away. Harmless for a long-running sim with a stable symbol set;
a slow leak otherwise.

### A trade print only reaches the tape if it finds liquidity

The public trade tape is fill-driven: a venue print is replayed through the matching engine as an
aggressor order, and only the resulting fills are published. A print that finds nothing crossable in
the mirrored book therefore produces no output at all, and its unfilled remainder simply rests.

**What it means:** tape fidelity tracks book fidelity. While the mirrored book is thin or stale —
right after startup, or after a feed gap — prints can be dropped from the tape rather than relayed
verbatim. This is the intended design, not a defect, but it is worth knowing when reconciling the
simulator's tape against the venue's.

### `MDUpdateType::ORDER` is never published

`Symbol::onOrderUpdate` fills `md_order_update_cache_`, but every call site clears the cache behind a
`// TODO: publish MD order update` comment rather than calling `publishMDOrderUpdate`. The function
exists and is correct; nothing invokes it.

**What it means:** no order-by-order (L3) market data reaches clients. Only level data does.

### L3 order books are stubs

`OrderBookImpl<OrderBookType::L3>::populateL2SubscriptionResponse`, `populateMDBookUpdate` and
`populateL2Snapshot` are
[empty function bodies with "stub implementation for testing" comments](https://github.com/SlickQuant/slick-sim/blob/main/src/order_book/order_book.hpp#L470-L484).
Only the `L2` specialisations do real work — and both adapters call `createOrderBook<OrderBookType::L2>()`.

**What it means:** `OrderBookType::L3` is only useful in tests. Do not select it for a live venue.

### Pro-rata matching does not exist

`MatchingEngine::Type` declares `ProRata`, and `matching_engines_` is sized by
`MatchingEngine::Type::__count__`, but
[`pro_rata_matching_engine.hpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/matching_engine/pro_rata_matching_engine.hpp)
is a **zero-byte file**.

**What it means:** every symbol on every venue uses `FifoMatchingEngine`.

### The historical-data feed does not exist

[`alpaca_historical_data_feed.hpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/md_feed/alpaca_historical_data_feed.hpp)
is a **zero-byte file**, and `md_feed`'s
[`CMakeLists.txt`](https://github.com/SlickQuant/slick-sim/blob/main/src/md_feed/CMakeLists.txt)
compiles only `hyperliquid_live_ws_feed.cpp`.

**What it means:** historical replay is an intended
[operating mode](architecture.md#historical-replay-planned) — matching client orders against
recorded market data — but no feed implements it. Live WebSocket feeds are the only source of market
data today.

A `md_feeds` entry whose `type` is neither `coinbase_live_ws` nor `hyperliquid_live_ws` is silently
ignored, which leaves the venue running
[self-contained](architecture.md#self-contained-a-conventional-exchange-simulator). That is a valid
mode, so a typo in `type` degrades quietly rather than erroring — worth checking the startup log if
you expected live data and got an empty book.

### `Exchange::processMdData()` and the market-data thread are commented out

`Exchange` declares `md_thread_` and `md_feed_`, and `start()` has a commented-out block that would
launch a dedicated market-data thread. `stop()` still contains `if (md_feed_ && md_thread_.joinable())
md_thread_.join();` — dead code, since nothing ever starts that thread.

**What it means:** market data is processed on the same thread as order requests. See the threading
model in [Architecture](architecture.md).

## Bugs found while documenting

These are latent defects, not design decisions. They are recorded here because the documentation
would otherwise describe behaviour the code does not deliver.

### `avg_fill_price` is recomputed in floating point on every fill

Both
[`OrderBook::executeOrder`](https://github.com/SlickQuant/slick-sim/blob/main/src/order_book/order_book.hpp#L187)
and the
[matching loop](https://github.com/SlickQuant/slick-sim/blob/main/src/matching_engine/fifo_matching_engine.cpp#L189)
round-trip the running average through `double` (`to_price_double` → arithmetic → `to_price_t`) on
every partial fill, rather than accumulating notional in fixed point. Errors compound across fills.

Worse, when a resting simulator order is filled by an incoming aggressor, **both** functions update
the same order: `FifoMatchingEngine::match` updates the aggressor's fields, then calls
`book.executeOrder`, which updates the *resting* order's fields. The commented-out block at
[`fifo_matching_engine.cpp:272-276`](https://github.com/SlickQuant/slick-sim/blob/main/src/matching_engine/fifo_matching_engine.cpp#L272-L276)
shows this duplication was noticed for the market-data path.

**What it means:** treat `avg_fill_price` on a heavily partially-filled order as approximate.

## Configuration and packaging

### The default config filename does not match the committed sample

With no command-line argument, `main.cpp` looks for **`slick-sim.json`** in the current directory
([`main.cpp:126`](https://github.com/SlickQuant/slick-sim/blob/main/src/sim/main.cpp#L126)).
The sample that ships with the repo is **`config/slick_sim.json`** — different directory, and an
underscore rather than a hyphen.

**What it means:** always pass the config path explicitly: `slick-sim config/slick_sim.json`.

### QuickFIX, SBE and the TCP gateway are built but never used

[`tcp_order_gateway.cpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/order_gateway/tcp_order_gateway.cpp),
[`fix_parser.cpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/order_gateway/fix_parser.cpp),
[`sbe_parser.cpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/order_gateway/sbe_parser.cpp),
[`json_parser.cpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/order_gateway/json_parser.cpp)
and the ~180 generated CME iLink3 SBE headers under `src/order_gateway/sbe/` all compile into the
`order_gateway` target, but no exchange ever constructs a `TcpOrderGateway`. `main.cpp` still carries
roughly 90 lines of commented-out scaffolding (`print_protocol_info`, per-client protocol selection)
from when that path was live.

**What it means:** `find_package(quickfix CONFIG REQUIRED)` in the top-level `CMakeLists.txt` is a
hard build dependency for code that never runs. You still need QuickFIX installed to build.

### The Doxygen API reference deliberately excludes the SBE headers

Not a defect — a documentation decision worth knowing. `Doxyfile.in` sets
`EXCLUDE = src/order_gateway/sbe`, because ~180 machine-generated classes would bury every
hand-written `slick::sim` type in the class index.

**What it means:** generated SBE message classes will not appear in the
[API reference](https://slickquant.github.io/slick-sim/api/). Read the headers directly, or
the schema at `src/order_gateway/sbe/schemas/cme_official/ilinkbinary.xml`.
