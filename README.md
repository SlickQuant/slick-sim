# ExchangeSimulator

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![CI](https://github.com/SlickQuant/slick-sim/actions/workflows/ci.yml/badge.svg)](https://github.com/SlickQuant/slick-sim/actions/workflows/ci.yml)
[![Documentation](https://github.com/SlickQuant/slick-sim/actions/workflows/documentation.yml/badge.svg)](https://slickquant.github.io/slick-sim/)
[![Sanitizers](https://github.com/SlickQuant/slick-sim/actions/workflows/asan.yml/badge.svg)](https://github.com/SlickQuant/slick-sim/actions/workflows/asan.yml)
[![GitHub release](https://img.shields.io/github/v/release/SlickQuant/slick-sim)](https://github.com/SlickQuant/slick-sim/releases)

An exchange simulator for testing trading strategies without risking capital. For each configured
venue, it mirrors live public market data into a local order book and serves the venue's order-entry
endpoints and market-data channels on local ports. Clients use the same APIs as they would on the
real exchange, with fills produced by an in-process FIFO matching engine.

A live feed lets your orders compete against real quoted liquidity. Without one, the books contain
only client orders. Historical replay against recorded market data is planned but not implemented. See [Operating modes](https://slickquant.github.io/slick-sim/architecture/#operating-modes).

Built in C++23. The executable is `slick-sim` (CMake project name `ExchangeSimulator`, currently `v0.3.0`).

## Documentation

Full documentation is published at **<https://slickquant.github.io/slick-sim/>**:

- **Internals** — [architecture and threading model](https://slickquant.github.io/slick-sim/architecture/), [order lifecycle](https://slickquant.github.io/slick-sim/order-lifecycle/), [matching engine](https://slickquant.github.io/slick-sim/matching-engine/), [order book](https://slickquant.github.io/slick-sim/order-book/), [market data](https://slickquant.github.io/slick-sim/market-data/), [adding an exchange](https://slickquant.github.io/slick-sim/extending/)
- **Integration** — [configuration reference](https://slickquant.github.io/slick-sim/configuration/), [Coinbase](https://slickquant.github.io/slick-sim/integration-coinbase/), [Hyperliquid](https://slickquant.github.io/slick-sim/integration-hyperliquid/)
- **[Known gaps](https://slickquant.github.io/slick-sim/known-gaps/)** — unimplemented and half-wired code paths, worth reading before you debug anything surprising
- **[API reference](https://slickquant.github.io/slick-sim/api/)** — Doxygen

The sources live in [`docs/`](docs/) and render on GitHub too.

## Status

Early-stage, under active development. Two exchanges are implemented today:

| Exchange | Status | Submit orders | Receive fills | Market data |
|---|---|---|---|---|
| Coinbase | Implemented | REST | WebSocket (`user` channel) | WebSocket |
| Hyperliquid | Implemented | REST | in the same HTTP response — no push channel | WebSocket |
| CME / Eurex / ICE | Not implemented | — | — | — |

## Architecture

Each enabled exchange runs its own instance of the pipeline below. The market-data feed at the top is optional — without it the same pipeline runs with an initially empty book:

```text
   Real exchange              live public market data (WS)
 (Coinbase, Hyperliquid) ────────────────────────────────────────▶  md_feed
                                                                       │
                                                                       │ mirrors the book
                                                                       ▼
                        orders (REST/WS)                            Exchange
  Trading   ───────────────────────────────▶  order_gateway  ───▶  (CoinbaseExchange /
  client    ◀───────────────────────────────                 ◀───  HyperliquidExchange)
 (orders)      order acks / fills / cancels                            │
                                                                       │ per instrument
                                                                       ▼
                                                                     Symbol
                                                         OrderBook (L3) + FifoMatchingEngine
                                                                       │
                                                                       │ book updates / executed trades
                                                                       ▼
                                                           market_data_publisher
                                                                       │
                                                                       │ venue-native market data (WS)
                                                                       ▼
                                                               Trading client
                                                               (market data)
```

`order_gateway` is bidirectional: it receives orders from the client and, via the `response_queue` fed by `MatchingEngine::publishOrderAck`/`publishOrderExecution`/`publishOrderModify`/`publishOrderCancel`, sends order acks, fills, modifies, cancels, and rejects back to the same client — independently of the market data path. A gateway can set `Request::request_tag` and gets it back on every response that request produces.

The published market data is level data and trades. The core can also publish order-by-order updates (`MDUpdateType::ORDER`), for an adapter that opts in; neither in-tree adapter does. One venue config entry can also create several exchanges, for a venue split into independent market segments.

Library targets:

| Target | Responsibility |
|---|---|
| `exchange` | The `Exchange` base, the venue registry, and `Symbol`, which binds one instrument's order book to a matching engine |
| `slick_sim_venue_<venue>` | One per enabled venue under `src/venues/`: its `Exchange` subclass, gateways, publisher and encoders |
| `matching_engine` | FIFO price/time-priority matching (`FifoMatchingEngine`) and order lifecycle notifications (ack, fill, modify, cancel) |
| `order_gateway` | Order-entry bases: `RestWsOrderGateway` for HTTP/WebSocket venues and `TcpOrderGateway`, the TCP transport base for venues on a binary session protocol |
| `market_data_publisher` | Publishes book updates and executed trades in each venue's own WebSocket wire format |
| `md_feed` | Ingests each venue's live public market data feed |
| `common`, `order_book`, `utils` | Header-only: shared types (`Order`, `Request`, `Venue`, fixed-point price/qty helpers), `OrderBook` (wraps the external `slick-orderbook` L3 book), and misc helpers |

## Prerequisites

- CMake 3.25+
- A C++23 compiler: MSVC 2022 (17.7+ if you want AddressSanitizer), GCC, or Clang
- No `vcpkg.json` manifest is committed, so the following must already be resolvable via `CMAKE_PREFIX_PATH` or a vcpkg toolchain file:
  - [QuickFIX](https://github.com/quickfix/quickfix) — only with `SLICK_SIM_ENABLE_FIX_JSON_PARSERS` (off by default)
  - uWebSockets (`unofficial-uwebsockets`) — only with a venue adapter that serves HTTP/WebSocket
  - [nlohmann_json](https://github.com/nlohmann/json)
  - [jwt-cpp](https://github.com/Thalhammer/jwt-cpp) — only with the Coinbase adapter enabled
  - [foonathan_memory](https://github.com/foonathan/memory)
  - Boost (`uuid` and `multiprecision` components)
  - GoogleTest (only needed if building tests, which is on by default)
  - [slick-net](https://github.com/SlickQuant/slick-net) — only for venue adapters that speak HTTP/WebSocket (Coinbase, Hyperliquid); not needed if both are switched off

Everything else — `slick-queue`, `slick-logger`, `slick-socket`, `slick-object-pool`, `slick-orderbook`, `coinbase-advanced-cpp`, `hyperliquid-cpp` — is fetched automatically via CMake `FetchContent` if not already installed locally. `slick-socket` (1.2.0 or later) is looked up only with the TCP gateway enabled, and the two venue SDKs only with their adapter enabled.

## Building

```bash
cmake -S . -B build
cmake --build build
```

If `find_package` can't locate the packages listed above, point CMake at a vcpkg installation:

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
```

This builds the `slick-sim` executable and, by default, the `slick_sim_tests` test binary.

### Developer options

```bash
cmake -S . -B build -DENABLE_ASAN=ON                     # AddressSanitizer (MSVC needs VS2022 17.7+; auto-disabled otherwise)
cmake -S . -B build -DBUILD_EXCH_SIMULATOR_TESTING=OFF    # skip building unit tests (default: ON)
cmake -S . -B build -DENABLE_NATIVE_ARCH=ON               # -march=native in Release (default: OFF)
```

### Choosing which venues to build

Each venue adapter lives in `src/venues/<venue>/` and has its own build target. Disabling an
adapter excludes its source files, tests and SDK. A Hyperliquid-only build needs no Coinbase SDK.

An adapter also declares its own networking stack: Coinbase and Hyperliquid speak HTTP/WebSocket and
name `slick-net`, plus the uWebSockets-based gateway and publisher bases. A venue on a binary session
protocol would name `slick-socket` instead — no venue inherits another's dependencies. With every
venue and the TCP gateway off, `slick-net`, `slick-socket`, `jwt-cpp`, `uWebSockets` and `QuickFIX` are
all unnecessary: nothing in the core links a wire protocol.

```bash
cmake -S . -B build -DSLICK_SIM_ENABLE_HYPERLIQUID=OFF    # Coinbase only
cmake -S . -B build -DSLICK_SIM_ENABLE_COINBASE=OFF       # Hyperliquid only
cmake -S . -B build -DSLICK_SIM_ENABLE_TCP_GATEWAY=OFF    # no TCP gateway, no slick-socket
cmake -S . -B build -DSLICK_SIM_ENABLE_FIX_JSON_PARSERS=ON  # also build the unused FIX/JSON parsers (needs QuickFIX)
```

All venue options default to `ON`, so an unqualified build is unchanged. A config key naming a venue
that was not compiled in is a fatal startup error listing the adapters the binary does carry, rather
than an exchange that silently does nothing. See
[Adding an exchange](https://slickquant.github.io/slick-sim/extending/).

A venue kept in its own repository is built in the same way, by pointing the configure step at its
directory: `-DSLICK_SIM_EXTERNAL_VENUES=/path/to/venue` (`;`-separated for several). See
[Out-of-tree venues](https://slickquant.github.io/slick-sim/extending/#out-of-tree-venues).

`ENABLE_NATIVE_ARCH` is off by default because CI's Release binaries are what `release.yml` attaches to GitHub Releases — a binary built with `-march=native` on a runner with AVX-512 crashes with `SIGILL` on any older CPU. Turn it on for local or self-hosted production builds, where the target CPU is the build CPU and the extra vectorisation is worth having. When `slick-orderbook` is fetched rather than found installed, its own `-march=native` option follows this one, because its static library is linked into the same binary.

## Configuration

`slick-sim` reads a JSON config file passed on the command line. A sample is checked in at [`config/slick_sim.json`](config/slick_sim.json):

```json
{
    "log_level": "trace",
    "exchanges": {
        "coinbase": {
            "enabled": true,
            "request_queue_size": 16777216,
            "response_queue_size": 16777216,
            "md_queue_size": 16777216,
            "md_feeds": [
                {
                    "type": "coinbase_live_ws"
                }
            ],
            "order_gateway": {
                "rest": {
                    "port": 4000
                },
                "ws": {
                    "port": 4001
                }
            },
            "md_publisher": {
                "port": 5000
            }
        },
        "hyperliquid": {
            "enabled": true,
            "md_feeds": [
                {
                    "type": "hyperliquid_live_ws",
                    "base_url": "https://api.hyperliquid.xyz"
                }
            ],
            "order_gateway": {
                "port": 4002,
                "base_url": "https://api.hyperliquid.xyz",
                "default_wallet": "0x0000000000000000000000000000000000000001"
            },
            "md_publisher": {
                "port": 5001,
                "upstream_base_url": "https://api.hyperliquid.xyz"
            }
        }
    }
}
```

- `log_level` — one of the `slick::logger` levels (`trace`, `debug`, `info`, `warn`, `error`, `fatal`); defaults to `info` if omitted.
- `exchanges` — required. Each key is a venue name (`coinbase`, `hyperliquid`); a venue is skipped unless `enabled` is `true` (defaults to `true` if omitted).
  - `md_feeds` — market data feeds to subscribe to on the real exchange. Hyperliquid also takes `coins`, the list subscribed at startup. **Optional**: omit it (or use an unrecognised `type`) to run the venue self-contained, with books driven only by client orders.
  - `order_gateway` — the venue-native order-entry endpoint(s) `slick-sim` serves. Coinbase exposes separate REST and WS ports; Hyperliquid exposes a single REST port plus the upstream `base_url` used for proxying `/info` and a placeholder `default_wallet`.
  - `md_publisher` — the port `slick-sim` serves its own venue-formatted market data WebSocket on. Hyperliquid also takes `upstream_base_url` (note: not `base_url`) for its `/info` proxy.
  - Coinbase additionally sets `request_queue_size` / `response_queue_size` / `md_queue_size`, the capacities of its internal lock-free queues.

The checked-in sample disables the Coinbase live feed (`"enabled": false` on the feed entry) and
Hyperliquid, so it runs Coinbase with only client orders by default. Enable the feeds to shadow the
live venues.

See the [configuration reference](https://slickquant.github.io/slick-sim/configuration/) for every key the code actually reads, with defaults.

> **Note:** if no config path is given on the command line, `slick-sim` looks for `slick-sim.json` in the current directory — which does not match the committed sample's name or location (`config/slick_sim.json`). Always pass the path explicitly.

## Running

```bash
mkdir -p logs                       # startup aborts if this directory is missing
slick-sim config/slick_sim.json
```

(The built binary's exact path depends on your CMake generator/config, e.g. `build/src/slick-sim` or `build/src/Debug/slick-sim.exe`.)

> **Note:** an enabled venue key with no adapter compiled in — a typo, or a venue switched off at configure time — is a fatal startup error that lists the adapters the binary does carry. See [Known gaps](https://slickquant.github.io/slick-sim/known-gaps/).

Press `Ctrl+C` to stop — `slick-sim` catches `SIGINT` and shuts every enabled exchange down gracefully before exiting.

Logs:

- `logs/slick-sim.log` — always written. The `logs/` directory must already exist.
- Console — Debug builds only.
- `logs/coinbase_data/` — raw Coinbase WebSocket capture, written whenever the Coinbase feed is active.

## Testing

GoogleTest builds by default (`BUILD_EXCH_SIMULATOR_TESTING=ON`). The suite covers matching, order
books, wire fields and messages, request tags, market data, both venue adapters and their encoders
(`tests/unit/`). Out-of-tree venues can pass their suites to `slick_sim_add_venue(... TESTS ...)`
so they run in the same test binary.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Building the documentation locally

The published site is MkDocs Material for the narrative docs plus Doxygen for the API reference.

```bash
# narrative docs — live preview on http://127.0.0.1:8000
pip install -r requirements-docs.txt
mkdocs serve

# API reference (needs doxygen + graphviz on PATH)
cmake -S . -B build
cmake --build build --target docs      # output in doxygen-out/html
```

The `docs` CMake target only appears when `find_package(Doxygen)` succeeds; a normal build is
unaffected if Doxygen is absent.

## Continuous integration & releases

[`ci.yml`](.github/workflows/ci.yml) builds and tests Debug and Release on Windows, Ubuntu and
macOS for pushes and pull requests to `main`. The Apple Silicon job omits the FIX/JSON parsers
because vcpkg's `quickfix` port excludes `arm64-osx`; those parsers are off by default. It sets
`MACOSX_DEPLOYMENT_TARGET=14.0` for macOS 14 compatibility. Without a `vcpkg.json` manifest, each
job bootstraps vcpkg and installs the required ports in classic mode, with a per-OS cache. Release
jobs check for `-march=native`; macOS also checks system-only dylib links and the minimum OS version.

[`asan.yml`](.github/workflows/asan.yml) runs the Ubuntu tests with AddressSanitizer and
UndefinedBehaviorSanitizer. It uses Debug because `ENABLE_ASAN` adds sanitizer flags only to
`*_DEBUG` variables, and verifies that the binary contains `__asan_*` symbols. Leak detection
remains off until intentional live singletons and pooled objects have suppressions. The test binary
also disables UBSan's `alignment` check: packed wire fields such as `OrderResponse::price` at
offset 404 are misaligned, and gtest's `EXPECT_EQ` binds them by `const T&`. The check stays enabled
for `src/`. The sanitized suite takes roughly three times as long as a normal run.

[`documentation.yml`](.github/workflows/documentation.yml) combines MkDocs at `/` with the Doxygen
API reference at `/api`, then publishes to `gh-pages` on pushes to `main`. Doxygen parses sources
directly, so the job needs only `doxygen`, `graphviz` and Python and takes about a minute. Pull
requests upload a site artifact without deploying. Other jobs lint Markdown and check links.

[`release.yml`](.github/workflows/release.yml) runs on `v*` tags and reuses the Windows, Linux and
macOS Release binaries that `ci.yml` built and tested for that commit. It packages the runtime
libraries and sample config, then drafts a release with `windows-x64`, `linux-x64` and `macos-arm64`
archives. Tag a commit on `main` after its CI run succeeds. Release notes come from the matching
`# vX.Y.Z` section of [`CHANGELOG.md`](CHANGELOG.md), if present. The macOS binary is unsigned;
a browser download may need `xattr -d com.apple.quarantine slick-sim` before the first run.

## Project structure

```text
slick-sim/
├── .github/
│   └── workflows/
│       ├── ci.yml               # build + test on push/PR (Windows, Ubuntu, macOS)
│       ├── asan.yml             # ASan + UBSan test run (Ubuntu, Debug)
│       ├── documentation.yml    # MkDocs + Doxygen → GitHub Pages
│       └── release.yml          # tag-triggered GitHub Release with Windows, Linux and macOS binaries
├── CHANGELOG.md                 # per-version notes, consumed by release.yml
├── CMakeLists.txt
├── Doxyfile.in                  # API reference config (`--target docs` locally)
├── mkdocs.yml                   # documentation site config
├── requirements-docs.txt        # mkdocs-material toolchain
├── cmake/
│   └── slick_sim_venue.cmake   # slick_sim_add_venue() and the other venue helpers
├── config/
│   └── slick_sim.json          # sample runtime configuration
├── docs/                        # published documentation sources
├── src/
│   ├── common/                 # shared types: Order, Request, Venue, price/qty helpers (header-only)
│   ├── exchange/                # Exchange base, venue registry, Symbol
│   ├── market_data_publisher/  # market data publisher bases
│   ├── matching_engine/        # FifoMatchingEngine (FIFO price/time priority)
│   ├── md_feed/                 # the MDFeed interface
│   ├── order_book/              # OrderBook wrapper around the slick-orderbook L3 book (header-only)
│   ├── order_gateway/           # order entry bases: REST/WS, and the TCP transport
│   ├── venues/                  # one directory per venue: coinbase/, hyperliquid/
│   ├── sim/
│   │   └── main.cpp             # slick-sim entry point
│   └── utils/                   # order id / timestamp / price / fixed-width string helpers (header-only)
├── tests/
│   └── unit/                    # GoogleTest suite (slick_sim_tests)
├── LICENSE
└── README.md
```

## License

[MIT](LICENSE)
