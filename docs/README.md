# Slick Sim

`slick-sim` is an exchange simulator that re-exposes a real venue's *own* order-entry and market-data
APIs on local ports. A trading client connects to it exactly as it would to the real exchange — same
REST routes, same WebSocket channels, same message shapes — but orders are matched by an in-process
FIFO matching engine, so no capital is ever at risk.

What your orders match against depends on how you configure the market-data feed:

- **Shadowing** — connect a live feed and `slick-sim` mirrors the real venue's book, so your orders
  compete against real, currently-quoted liquidity.
- **Self-contained** — omit the feed and it behaves like a conventional exchange simulator: books
  start empty and clients trade only against each other.
- **Historical replay** — planned, not yet implemented.

See [Operating modes](architecture.md#operating-modes). Two venues are implemented today:
**Coinbase** (REST + WebSocket order entry, WebSocket market data) and **Hyperliquid** (REST order
entry, WebSocket market data).

## Understanding the system

Read in this order if you are new to the codebase.

| Page | What it covers |
| --- | --- |
| [Architecture](architecture.md) | The shadowing model, component map, threading model, and the lock-free queues that connect everything |
| [Order lifecycle](order-lifecycle.md) | One order traced end to end, from HTTP request to execution report, plus every status and reject-reason enum |
| [Matching engine](matching-engine.md) | FIFO price/time priority, time-in-force handling, self-match prevention, trade summaries, fixed-point arithmetic |
| [Order book](order-book.md) | The single L3 book holding both real-market phantom liquidity and simulated user orders, and how the two are kept apart |
| [Market data](market-data.md) | Feed ingestion, per-venue event sequencing, and the internal binary market-data wire format |
| [Adding an exchange](extending.md) | A checklist for implementing a new venue adapter, grounded in the two that work |

## Integrating a client

| Page | What it covers |
| --- | --- |
| [Configuration](configuration.md) | Every configuration key the code actually reads, with defaults and source references |
| [Coinbase](integration-coinbase.md) | Implemented REST routes, order-entry WebSocket channels, market-data channels, worked examples |
| [Hyperliquid](integration-hyperliquid.md) | `/exchange` actions, the `/info` upstream proxy, market-data channels, worked examples |

## Before you rely on any of this

`slick-sim` is early-stage and under active development. Several code paths are stubs, and a few do
something other than what their surroundings suggest. Everything known is collected in
**[Known gaps](known-gaps.md)** — read it before debugging behaviour that surprises you.

## Elsewhere

- [API reference](https://slicktech.github.io/exchange_simulator/api/) — Doxygen-generated C++ reference for `src/`.
- [Repository README](https://github.com/SlickTech/exchange_simulator/blob/main/README.md) — prerequisites, build instructions, CI and release process.
