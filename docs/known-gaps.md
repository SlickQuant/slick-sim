# Known gaps

`slick-sim` is under active development. The limitations below include unfinished code paths and
behaviour that differs from the intended design, with source references and consequences for clients.

## Venues

### CME and ICE are out of tree, and Eurex is not implemented

This repository has no CME or ICE adapter. Both are built from separate repositories with
`-DSLICK_SIM_EXTERNAL_VENUES`, so a default build does not carry them. There is no Eurex adapter at
all. `Venue::CME` and `Venue::ICE` exist in
[`types.hpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/common/types.hpp) either way. A
`Venue` is an identity written into every market-data frame, not a claim that an adapter exists, so
`to_venue("cme")` still resolves.

**What it means:** in a default build, only `coinbase` and `hyperliquid` are usable venue keys in the
config.

### An unknown venue key is rejected at startup

The processing loop lives in
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

### REST order handlers no longer block the event loop

REST handlers that needed a matching-engine reply used to poll `response_queue_` with a
100 microsecond sleep on the gateway's only uWebSockets event-loop thread. Batch handlers polled
once per entry: a five-order batch could hold the loop for a second, and a single unanswered order
stalled other clients for five seconds. The integration suite made this behavior testable.

Handlers now publish the request, register a waiter with
[`PendingResponses`](https://github.com/SlickQuant/slick-sim/blob/main/src/order_gateway/pending_responses.hpp)
and return. A repeating timer (`order_gateway.rest.response_poll_ms`, default 1 ms) drains the report
stream, completes the matching `HttpResponse`, and expires waiters after their timeout. An aborted
connection abandons its waiters instead of leaving them to write to a dead socket.

The report queue has no notification mechanism, so the poll interval sets the minimum completion
latency. Adjust `response_poll_ms` to change it; `1` is a uWS timer's granularity. The integration
suite checks that a five-id batch completes well inside 500 ms, which the old per-order poll could
not meet.

### `MDUpdateType::ORDER` is published only on opt-in

`Symbol::onOrderUpdate` fills `md_order_update_cache_` for every venue, and
`Exchange::flushMarketDataCaches` publishes it as an `ORDER` frame - but only for an adapter that sets
`publish_order_updates_`, and no in-tree adapter does. This used to be a gap for every venue: the
cache was cleared behind a `// TODO: publish MD order update` and nothing reached `md_queue`.

**What it means:** Coinbase and Hyperliquid clients still receive level data only. Their adapters
clear the cache on their own feed paths too, so turning the flag on for them would need those paths
to publish rather than clear.

### L3 order books are stubs

`OrderBookImpl<OrderBookType::L3>::populateL2SubscriptionResponse`, `populateMDBookUpdate` and
`populateL2Snapshot` are
[empty function bodies with "stub implementation for testing" comments](https://github.com/SlickQuant/slick-sim/blob/main/src/order_book/order_book.hpp#L470-L484).
Only the `L2` specialisations do real work — and both adapters call `createOrderBook<OrderBookType::L2>()`.

**What it means:** `OrderBookType::L3` is only useful in tests. Do not select it for a live venue.

### Pro-rata matching covers the core algorithm only

[`ProRataMatchingEngine`](matching-engine.md#pro-rata) implements the size split, lot rounding,
minimum allocation, FIFO percentage and FIFO leftover. It does not implement top-order priority,
leveling or LMM allocations, and only CME and ICE can select it.

**What it means:** CME's Allocation, Threshold Pro-Rata and LMM algorithms are approximated by plain
pro-rata.

### Only CME and ICE can replay history

[PCAP replay](pcap-replay.md) drives CME and ICE from recorded multicast captures. Coinbase and
Hyperliquid have no replay feed: their live WebSocket feeds are their only source of market data.

An unrecognised `md_feeds[].type` is silently ignored by Coinbase and Hyperliquid, leaving the venue
[self-contained](architecture.md#self-contained-a-conventional-exchange-simulator). If you expected
live data but see an empty book, check the feed type and startup log.

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

### The FIX and JSON message parsers are built only on request, and never used

[`fix_parser.cpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/order_gateway/fix_parser.cpp),
[`json_parser.cpp`](https://github.com/SlickQuant/slick-sim/blob/main/src/order_gateway/json_parser.cpp)
and the `MessageParser` factory are an older generic parsing layer that nothing constructs at
runtime. They sit behind `SLICK_SIM_ENABLE_FIX_JSON_PARSERS`, **off by default**, in their own
`slick_sim_message_parsers` target - the only thing in the project that needs QuickFIX. `main.cpp`
still carries roughly 90 lines of commented-out scaffolding (`print_protocol_info`, per-client protocol
selection) from when that path was live.

`TcpOrderGateway` itself is no longer part of this: it was rebuilt as a transport base for venues on
a binary session protocol, and needs only slick-socket. The generated SBE headers and schema that
used to live under `src/order_gateway/sbe/` were removed.

**What it means:** QuickFIX need not be installed unless you turn the parsers on.
