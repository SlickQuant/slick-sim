# ExchangeSimulator

A market simulator that shadows real exchanges for risk-free strategy testing. For each configured venue, it ingests the exchange's *live* public market data, mirrors it into a local order book, and exposes that exchange's own REST/WebSocket order-entry API — so a trading client can connect and trade against the simulator exactly as it would against the real exchange, with fills produced by an in-process FIFO matching engine instead of real capital.

Built in C++23. The executable is `slick-sim` (CMake project name `ExchangeSimulator`, currently `v0.1.0`).

## Status

Early-stage, under active development. Two exchanges are implemented today:

| Exchange | Status | Order entry | Market data |
|---|---|---|---|
| Coinbase | Implemented | REST + WebSocket | WebSocket |
| Hyperliquid | Implemented | REST | WebSocket |
| CME | Not implemented — adapter file exists but is empty | — | — |
| Eurex / ICE | Not implemented | — | — |

## Architecture

Each enabled exchange runs its own instance of the pipeline below:

```
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
                                                                       │ book / trade updates
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
| `market_data_publisher` | Republishes book/trade updates in each venue's own WebSocket wire format |
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
```

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
                    "base_url": "https://api.hyperliquid.xyz",
                }
            ],
            "order_gateway": {
                "port": 4002,
                "base_url": "https://api.hyperliquid.xyz",
                "default_wallet": "0x0000000000000000000000000000000000000001"
            },
            "md_publisher": {
                "port": 5001,
                "base_url": "https://api.hyperliquid.xyz"
            }
        }
    }
}
```

- `log_level` — one of the `slick::logger` levels (`trace`, `debug`, `info`, `warn`, `error`, `fatal`); defaults to `info` if omitted.
- `exchanges` — required. Each key is a venue name (`coinbase`, `hyperliquid`); a venue is skipped unless `enabled` is `true` (defaults to `true` if omitted).
  - `md_feeds` — live market data feeds to subscribe to on the real exchange.
  - `order_gateway` — the venue-native order-entry endpoint(s) `slick-sim` serves. Coinbase exposes separate REST and WS ports; Hyperliquid exposes a single REST port plus the upstream `base_url` used for request signing/proxying and a placeholder `default_wallet`.
  - `md_publisher` — the port `slick-sim` serves its own venue-formatted market data WebSocket on.
  - Coinbase additionally sets `request_queue_size` / `response_queue_size` / `md_queue_size`, the capacities of its internal lock-free queues.

> **Note:** if no config path is given on the command line, `slick-sim` looks for `slick-sim.json` in the current directory — which does not match the committed sample's name or location (`config/slick_sim.json`). Always pass the path explicitly.

## Running

```bash
slick-sim config/slick_sim.json
```

(The built binary's exact path depends on your CMake generator/config, e.g. `build/src/sim/slick-sim` or `build/src/sim/Debug/slick-sim.exe`.)

Press `Ctrl+C` to stop — `slick-sim` catches `SIGINT` and shuts every enabled exchange down gracefully before exiting.

Logs:
- `logs/slick-sim.log` — always written.
- Console — Debug builds only.
- `logs/coinbase_data/` — raw Coinbase WebSocket capture, written whenever the Coinbase feed is active.

## Testing

Unit tests are built by default (`BUILD_EXCH_SIMULATOR_TESTING=ON`) using GoogleTest, covering the matching engine (FIFO matching, self-match prevention, time-in-force), order book operations, message/type conversions, and the Coinbase/Hyperliquid exchange adapters (`tests/unit/`).

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

## Project structure

```
slick-sim/
├── CMakeLists.txt
├── config/
│   └── slick_sim.json          # sample runtime configuration
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
│   └── utils/                   # order id / timestamp / price helpers (header-only)
├── tests/
│   └── unit/                    # GoogleTest suite (slick_sim_tests)
├── LICENSE
└── README.md
```

## License

[MIT](LICENSE)
