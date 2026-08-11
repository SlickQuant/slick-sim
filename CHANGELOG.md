# Unreleased

## Fixed

- `Exchange::start()` no longer dereferences a null `md_publisher_`. Only `CoinbaseExchange` and
  `HyperliquidExchange` construct a publisher, so any other venue key — which falls through to the
  generic `Exchange` — segfaulted the process on startup. Because `nlohmann::json` iterates object
  keys in sorted order, the `cme` block in the committed sample config crashed `slick-sim` before
  either working venue was even constructed.
- `enabled` is now honoured for every venue. It is read once in the `Exchange` base constructor and
  checked by all three `start()` implementations, which return early with a warning when it is
  false. Previously the check lived only in the `coinbase` and `hyperliquid` branches of `main.cpp`,
  so `"enabled": false` on any other venue was silently ignored.

## Changed

- Sample config (`config/slick_sim.json`):
  - The `cme` block is now `"enabled": false`, uses an `mbo_live_feed` placeholder feed instead of a
    copied Hyperliquid one, and no longer collides with Hyperliquid's ports — its order gateway
    moves to 4003 and its market data publisher to 5002 (it previously shared Hyperliquid's 5001).
  - Hyperliquid's order gateway port moves from 4003 to 4002.
  - Dropped `coins` from the Hyperliquid feed entry.

## Added

- Documentation site published to GitHub Pages at
  <https://slicktech.github.io/exchange_simulator/> — MkDocs Material for the narrative
  documentation at the root, Doxygen for the C++ API reference at `/api`.
- Eleven documentation pages under `docs/`, in two tracks:
  - **Internals** — architecture and threading model, order lifecycle, matching engine, order book,
    market data, and a guide to adding an exchange.
  - **Integration** — full configuration reference, plus Coinbase and Hyperliquid client guides
    covering the REST routes, WebSocket channels and message shapes actually implemented.
  - A **Known gaps** page recording unimplemented and half-wired code paths.
- `documentation.yml` workflow: builds both halves of the site, deploys to `gh-pages` on pushes to
  `main`, uploads a preview artifact for pull requests, and lints Markdown and checks its links.
  It runs Doxygen directly rather than configuring CMake, so it does not need the vcpkg dependency
  chain.
- `Doxyfile.in` and a CMake `docs` target for generating the API reference locally
  (`cmake --build build --target docs`). The target is skipped when Doxygen is not installed.
- `.markdownlint.json` and `.github/markdown-link-check-config.json`.

# v0.1.0 - 08-09-2026

- Initial exchange simulator: Coinbase and Hyperliquid adapters with live market data mirroring, FIFO matching engine, and venue-native order entry/market data gateways
