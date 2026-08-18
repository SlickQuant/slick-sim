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

`Exchange::start()` does nothing useful:

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

## Order and market-data plumbing

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
