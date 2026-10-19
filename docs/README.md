# Slick Sim

`slick-sim` serves a venue's order-entry and market-data APIs on local ports. Clients use the same
REST routes, WebSocket channels and message formats as on the real exchange. An in-process FIFO
matching engine fills their orders without risking capital.

What your orders match against depends on how you configure the market-data feed:

- **Shadowing** — connect a live feed and `slick-sim` mirrors the real venue's book, so your orders
  compete against real, currently-quoted liquidity.
- **Self-contained** — omit the feed and it behaves like a conventional exchange simulator: books
  start empty and clients trade only against each other.
- **Historical replay** — planned, not yet implemented.

See [Operating modes](architecture.md#operating-modes).

Two venues are implemented today, and they differ in how order results get back to you:

- **[Coinbase](integration-coinbase.md)** — you submit orders over **REST**, but acks, fills and
  cancels arrive on a **separate WebSocket** (`user` channel). The REST call returns only a
  `PENDING_NEW` acknowledgement, so a REST-only client never learns that its order traded. Market
  data is a third endpoint, its own WebSocket.
- **[Hyperliquid](integration-hyperliquid.md)** — you submit orders over **REST** and the result
  comes back in that same HTTP response. There is no push channel for fills at all. Market data is a
  separate WebSocket.

## Understanding the system

Read in this order if you are new to the codebase.

| Page | What it covers |
| --- | --- |
| [Architecture](architecture.md) | The shadowing model, component map, threading model, and the lock-free queues that connect everything |
| [Order lifecycle](order-lifecycle.md) | One order traced end to end, from HTTP request to execution report, plus every status and reject-reason enum |
| [Matching engine](matching-engine.md) | FIFO price/time priority, time-in-force handling, self-match prevention, trade summaries, fixed-point arithmetic |
| [Order book](order-book.md) | The single L3 book holding both real-market phantom liquidity and simulated user orders, and how the two are kept apart |
| [Market data](market-data.md) | Feed ingestion, per-venue event sequencing, how venue trade prints are matched rather than relayed, and the internal binary market-data wire format |
| [Adding an exchange](extending.md) | A checklist for implementing a new venue adapter, grounded in the two that work |

## Integrating a client

| Page | What it covers |
| --- | --- |
| [Configuration](configuration.md) | Every configuration key the code actually reads, with defaults and source references |
| [Coinbase](integration-coinbase.md) | Implemented REST routes, order-entry WebSocket channels, market-data channels, worked examples |
| [Hyperliquid](integration-hyperliquid.md) | `/exchange` actions, the `/info` upstream proxy, market-data channels, worked examples |

## Before you rely on any of this

`slick-sim` is under active development. Some code paths are stubs or behave differently from the
intended design. Check [Known gaps](known-gaps.md) when investigating unexpected behaviour.

## Elsewhere

- [API reference](https://slickquant.github.io/slick-sim/api/) — Doxygen-generated C++ reference for `src/`.
- [Repository README](https://github.com/SlickQuant/slick-sim/blob/main/README.md) — prerequisites, build instructions, CI and release process.
