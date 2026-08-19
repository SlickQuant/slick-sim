# ExchangeSimulator

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![CI](https://github.com/SlickQuant/slick-sim/actions/workflows/ci.yml/badge.svg)](https://github.com/SlickQuant/slick-sim/actions/workflows/ci.yml)
[![Documentation](https://github.com/SlickQuant/slick-sim/actions/workflows/documentation.yml/badge.svg)](https://slickquant.github.io/slick-sim/)
[![Sanitizers](https://github.com/SlickQuant/slick-sim/actions/workflows/asan.yml/badge.svg)](https://github.com/SlickQuant/slick-sim/actions/workflows/asan.yml)
[![GitHub release](https://img.shields.io/github/v/release/SlickQuant/slick-sim)](https://github.com/SlickQuant/slick-sim/releases)

A exchange simulator that shadows real exchanges for risk-free strategy testing. For each configured venue, it ingests the exchange's *live* public market data, mirrors it into a local order book, and serves that exchange's own APIs on local ports — the same **order-entry** endpoints you would send orders to, and the same **market-data** channels you would subscribe to for prices. A trading client connects and trades against the simulator exactly as it would against the real exchange, with fills produced by an in-process FIFO matching engine instead of real capital.

The live feed is optional. Configure one and your orders compete against real quoted liquidity; omit it and `slick-sim` runs as a conventional exchange simulator, with books driven purely by the orders clients send. A historical-replay feed — matching against recorded market data — is planned but not yet implemented. See [Operating modes](https://slickquant.github.io/slick-sim/architecture/#operating-modes).

Built in C++23. The executable is `slick-sim` (CMake project name `ExchangeSimulator`, currently `v0.1.0`).

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
| CME | Not implemented — adapter file exists but is empty | — | — | — |
| Eurex / ICE | Not implemented | — | — | — |

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

`order_gateway` is bidirectional: it receives orders from the client and, via the `response_queue` fed by `MatchingEngine::publishOrderAck`/`publishOrderExecution`/`publishOrderModify`/`publishOrderCancel`, sends order acks, fills, modifies, cancels, and rejects back to the same client — independently of the market data path.

Library targets:

| Target | Responsibility |
|---|---|
| `exchange` | Per-venue orchestration (`CoinbaseExchange`, `HyperliquidExchange`) and `Symbol`, which binds one instrument's order book to a matching engine |
| `matching_engine` | FIFO price/time-priority matching (`FifoMatchingEngine`) and order lifecycle notifications (ack, fill, modify, cancel) |
| `order_gateway` | Venue-native REST/WS order-entry endpoints — receives orders and returns order acks/fills/cancels/rejects to the client — plus an unused generic FIX/SBE/JSON TCP gateway |
| `market_data_publisher` | Publishes book updates and executed trades in each venue's own WebSocket wire format |
| `md_feed` | Ingests each venue's live public market data feed |
| `common`, `order_book`, `utils` | Header-only: shared types (`Order`, `Request`, `Venue`, fixed-point price/qty helpers), `OrderBook` (wraps the external `slick-orderbook` L3 book), and misc helpers |

## Prerequisites

- CMake 3.25+
- A C++23 compiler: MSVC 2022 (17.7+ if you want AddressSanitizer), GCC, or Clang
- No `vcpkg.json` manifest is committed, so the following must already be resolvable via `CMAKE_PREFIX_PATH` or a vcpkg toolchain file:
  - [QuickFIX](https://github.com/quickfix/quickfix)
  - uWebSockets (`unofficial-uwebsockets`)
  - [nlohmann_json](https://github.com/nlohmann/json)
  - [jwt-cpp](https://github.com/Thalhammer/jwt-cpp)
  - [foonathan_memory](https://github.com/foonathan/memory)
  - Boost (`uuid` component)
  - GoogleTest (only needed if building tests, which is on by default)

Everything else — `slick-logger`, `slick-socket`, `slick-object-pool`, `slick-orderbook`, `coinbase-advanced-cpp`, `hyperliquid-cpp`, `slick-net` — is fetched automatically via CMake `FetchContent` if not already installed locally.

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

`ENABLE_NATIVE_ARCH` is off by default because CI's Release binaries are what `release.yml` attaches to GitHub Releases — a binary built with `-march=native` on a runner with AVX-512 crashes with `SIGILL` on any older CPU. Turn it on for local or self-hosted production builds, where the target CPU is the build CPU and the extra vectorisation is worth having.

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

See the [configuration reference](https://slickquant.github.io/slick-sim/configuration/) for every key the code actually reads, with defaults.

> **Note:** if no config path is given on the command line, `slick-sim` looks for `slick-sim.json` in the current directory — which does not match the committed sample's name or location (`config/slick_sim.json`). Always pass the path explicitly.

## Running

```bash
mkdir -p logs                       # startup aborts if this directory is missing
slick-sim config/slick_sim.json
```

(The built binary's exact path depends on your CMake generator/config, e.g. `build/src/slick-sim` or `build/src/Debug/slick-sim.exe`.)

> **Note:** any venue key other than `coinbase`/`hyperliquid` builds a generic `Exchange` that binds no ports and processes at most one request. The sample config's `cme` block is `"enabled": false` for that reason, and is skipped at startup with a warning. See [Known gaps](https://slickquant.github.io/slick-sim/known-gaps/).

Press `Ctrl+C` to stop — `slick-sim` catches `SIGINT` and shuts every enabled exchange down gracefully before exiting.

Logs:

- `logs/slick-sim.log` — always written. The `logs/` directory must already exist.
- Console — Debug builds only.
- `logs/coinbase_data/` — raw Coinbase WebSocket capture, written whenever the Coinbase feed is active.

## Testing

Unit tests are built by default (`BUILD_EXCH_SIMULATOR_TESTING=ON`) using GoogleTest, covering the matching engine (FIFO matching, self-match prevention, time-in-force), order book operations, message/type conversions, the fixed-width wire-field helpers, the Coinbase/Hyperliquid exchange adapters, and the market-data wire encoders (`tests/unit/`).

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

[`ci.yml`](.github/workflows/ci.yml) builds and tests every push/PR to `main` across Windows and Ubuntu (Debug + Release). macOS is currently excluded — GitHub's Intel (`macos-13`) runners have very limited hosted capacity and queue indefinitely, and the ARM `macos-latest` runners can't build `quickfix` (its vcpkg port excludes `arm64-osx`). Since there's no `vcpkg.json` manifest, each job clones and bootstraps vcpkg itself and installs the required ports in classic mode (cached per-OS) before configuring CMake.

[`asan.yml`](.github/workflows/asan.yml) runs the same test suite on Ubuntu under AddressSanitizer and UndefinedBehaviorSanitizer. It must configure `Debug`: `ENABLE_ASAN` only injects `-fsanitize=address,undefined` into the `*_DEBUG` flag variables, so a Release configuration would produce an unsanitized binary and a green run that checked nothing — the job asserts the built binary actually carries `__asan_*` symbols rather than trusting that. Leak detection is off, because the process deliberately exits with live singletons and pooled objects that LeakSanitizer would report; enabling it needs a suppressions file first. Expect roughly 3x the normal test runtime.

[`documentation.yml`](.github/workflows/documentation.yml) builds the MkDocs site and the Doxygen API reference, merges them into one tree (`/` and `/api` respectively), and publishes to the `gh-pages` branch on pushes to `main`. It deliberately does *not* configure CMake — Doxygen parses the sources directly, so the job needs only `doxygen`, `graphviz` and Python, and runs in about a minute instead of inheriting `ci.yml`'s vcpkg dependency chain. Pull requests build and upload the site as an artifact without deploying. Two further jobs lint the Markdown and check its links.

[`release.yml`](.github/workflows/release.yml) triggers on `v*` tags. Rather than rebuilding, it reuses the Release-config Windows and Linux binaries `ci.yml` already built and tested for that exact commit (downloaded from that CI run's artifacts), packages each with its runtime libraries and the sample config, and drafts a GitHub Release with both archives attached. This means the tagged commit must already have a successful `ci.yml` run on `main` — tag what you've already merged, not an untested commit. Release notes are pulled from the matching `# vX.Y.Z` section of the [`CHANGELOG.md`](CHANGELOG.md) file, if present.

## Project structure

```text
slick-sim/
├── .github/
│   └── workflows/
│       ├── ci.yml               # build + test on push/PR (Windows, Ubuntu)
│       ├── asan.yml             # ASan + UBSan test run (Ubuntu, Debug)
│       ├── documentation.yml    # MkDocs + Doxygen → GitHub Pages
│       └── release.yml          # tag-triggered GitHub Release with Windows binary
├── CHANGELOG.md                 # per-version notes, consumed by release.yml
├── CMakeLists.txt
├── Doxyfile.in                  # API reference config (`--target docs` locally)
├── mkdocs.yml                   # documentation site config
├── requirements-docs.txt        # mkdocs-material toolchain
├── config/
│   └── slick_sim.json          # sample runtime configuration
├── docs/                        # published documentation sources
├── scripts/
│   ├── download_cme_schemas.py # fetches CME SBE schemas (not currently used by the build)
│   └── create_fallback_schema.py
├── src/
│   ├── common/                 # shared types: Order, Request, Venue, price/qty helpers (header-only)
│   ├── exchange/                # Exchange base + CoinbaseExchange/HyperliquidExchange, Symbol
│   ├── market_data_publisher/  # venue-native WS market data publishers
│   ├── matching_engine/        # FifoMatchingEngine (FIFO price/time priority)
│   ├── md_feed/                 # live market data ingestion (Coinbase, Hyperliquid)
│   ├── order_book/              # OrderBook wrapper around the slick-orderbook L3 book (header-only)
│   ├── order_gateway/           # venue-native REST/WS order entry gateways
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
