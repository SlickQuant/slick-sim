# v0.1.1 - 08-12-2026

## Fixed

- The Ubuntu Release CI job no longer fails intermittently with `SIGILL`. The vcpkg cache is now
  keyed on the runner's CPU feature set, not just the OS. The SlickQuant dependency libraries
  (`slick-net`, `hyperliquid-cpp`, …) build their Release config with `-march=native`, so vcpkg
  produced binaries tuned to whichever runner compiled them; restoring that cache onto a runner with
  a narrower instruction set made the tests die on an AVX-512 opcode (`vmovdqu8`) inside
  `slick::net::Websocket`'s constructor, reached from `HyperliquidRestOrderGateway`. That is why a
  re-run sometimes passed — it depended on the CPU the cache came from.
- Release builds on GCC/Clang no longer pass `-march=native`. It is now behind a new
  `ENABLE_NATIVE_ARCH` option, off by default. Baking the build machine's instruction set into the
  binary broke two things: the Ubuntu Release CI job started failing with `SIGILL` in every test
  that exercises the exchange and order-book paths, and — more seriously — `ci.yml` uploads those
  Release binaries for `release.yml` to attach to GitHub Releases, so published builds would fault
  on any user CPU older than the runner that produced them. The flag is still available for local
  and self-hosted production builds via `-DENABLE_NATIVE_ARCH=ON`.

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
  <https://slickquant.github.io/slick-sim/> — MkDocs Material for the narrative
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
