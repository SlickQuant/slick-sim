# Known gaps

`slick-sim` is early-stage. This page lists everything found in the current source that is
unimplemented, half-wired, or behaves differently from what the surrounding code implies. It exists
so the rest of the documentation can describe the intended design without having to hedge every
paragraph, and so you do not spend an afternoon debugging something that was never finished.

Each entry says what it is, where it lives, and what it means for you.

## Venues

### CME, Eurex and ICE are not implemented

There is no `CmeExchange`, no Eurex adapter, and no ICE adapter, despite `Venue::CME` and `Venue::ICE`
existing in
[`types.hpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/common/types.hpp) and
`README.md` describing exchange-adapter namespaces for all three. A zero-byte `src/exchange/exch_cme.hpp`
used to stand in for the first of these; it has been removed, since an empty header is not a plan.

The `Venue` enumerators stay regardless — a `Venue` is an identity written into every market-data
frame, not a claim that an adapter exists, so `to_venue("cme")` still resolves.

**What it means:** only `coinbase` and `hyperliquid` are usable venue keys in the config.

### An unknown venue key is rejected at startup

This used to be a gap: `main.cpp` dispatched on the config key with an `if/else`, and **anything
else** constructed a plain `Exchange` that bound no ports, populated no publisher, and — because the
base `start()` had no loop — processed at most one request before its thread exited. Silently.

Both halves are now closed. The loop lives in
[`Exchange::start()`](https://github.com/SlickQuant/slick-sim/blob/main/src/exchange/exchange.cpp),
and keys resolve through the
[venue registry](https://github.com/SlickQuant/slick-sim/blob/main/src/exchange/exchange_registry.hpp)
rather than a hard-coded chain, with no generic fallback. An enabled key with no registered adapter
now fails immediately and names what the binary does carry:

```text
[ERROR] No venue adapter for exchanges."kraken". This build has: coinbase, hyperliquid.
        Check the spelling, or configure with -DSLICK_SIM_ENABLE_<VENUE>=ON.
```

`"enabled": false` is checked **before** the lookup, so a disabled block needs no adapter — which is
how the committed sample's `cme` entry keeps working.

**What it means:** a typo or a compiled-out venue is now a startup failure rather than an exchange
that appears to run. Note that the same message appears if a venue adapter was built but failed to
register itself, which is what the `ExchangeRegistryTest` suite guards against.

## Order and market-data plumbing

### REST order handlers block the gateway's event loop

Every REST handler that needs a reply from the matching engine waits for it by polling
`response_queue_` in a loop with a 100 microsecond sleep, up to a 5 second timeout. That loop runs
on the gateway's **only** uWebSockets event-loop thread, so while one request waits, no other
client's request on that gateway is served at all - not even one that would answer instantly.

The batch handlers are worse, because they poll once per entry rather than once per request:
[`handle_batch_cancel`](https://github.com/SlickQuant/slick-sim/blob/main/src/venues/coinbase/coinbase_rest_order_gateway.cpp#L638)
and Hyperliquid's
[`process_order_action`](https://github.com/SlickQuant/slick-sim/blob/main/src/venues/hyperliquid/hyperliquid_rest_order_gateway.cpp#L263)
wait inside the loop over the batch, so a ten-order batch can hold the loop for ten separate
timeouts.

**What it means:** REST throughput is one in-flight request per gateway, and a single order that the
engine never answers stalls every other client for five seconds. It is not a correctness problem -
requests are still served first-come-first-served, since the request and response queues are both
FIFO and the exchange thread is single-threaded - but it puts a hard ceiling on concurrency.

`order_gateway::PendingResponses` is the correlation half of the fix: a handler registers what it is
waiting for and returns, and the reply completes the HTTP response when it arrives. The uWebSockets
half - holding an `HttpResponse` past handler return, cancelling on abort, and the per-request
timeout timer - is deliberately not done yet, because nothing exercises the REST gateway over HTTP
and those are precisely the parts that fail as a use-after-free rather than a wrong answer.

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
is a **zero-byte file**. `src/md_feed/` now holds only the `MDFeed` interface — every implementation
lives with its venue under `src/venues/` — so a replay feed would be a new venue-independent
implementation of that same two-method interface.

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
launch a dedicated market-data thread. `stop()` still joins `md_thread_` — dead code, since nothing
ever starts that thread.

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

**What it means:** the code is still dead, but it is no longer a mandatory build cost. The whole
cluster sits behind `SLICK_SIM_ENABLE_TCP_GATEWAY`, which defaults to `ON` so existing builds are
unchanged. Configuring with `-DSLICK_SIM_ENABLE_TCP_GATEWAY=OFF` drops those four sources, the `sbe/`
include path, and the `find_package(quickfix CONFIG REQUIRED)` that made QuickFIX a hard dependency
for code that never runs — so QuickFIX need not be installed at all.

### The Doxygen API reference deliberately excludes the SBE headers

Not a defect — a documentation decision worth knowing. `Doxyfile.in` sets
`EXCLUDE = src/order_gateway/sbe`, because ~180 machine-generated classes would bury every
hand-written `slick::sim` type in the class index.

**What it means:** generated SBE message classes will not appear in the
[API reference](https://slickquant.github.io/slick-sim/api/). Read the headers directly, or
the schema at `src/order_gateway/sbe/schemas/cme_official/ilinkbinary.xml`.
