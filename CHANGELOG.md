# Unreleased

## Added

- Trade updates on both venues' market-data channels. Coinbase `market_trades` and Hyperliquid
  `trades` now deliver a recent-trades snapshot on subscribe followed by a message per executed
  trade. Previously a subscriber got an empty snapshot and then silence: the Coinbase publisher had
  no case for trades at all, and neither publisher handled `MDUpdateType::TRADE_SUMMARY`, so every
  fill the matching engine ever produced was written to `md_queue_` and dropped.
- A venue's own trade print is now replayed through the matching engine as an incoming aggressor
  order instead of being relayed verbatim, so it takes liquidity exactly as the real trade did and
  fills any simulator order ahead of the phantom liquidity it sweeps. Only the resulting fills are
  published. Anything the print cannot fill rests on the aggressor's side at the print price.
- `MDTrade` carries a `trade_id`, generated as an independent sequence per venue, so a live print and
  the same trade replayed in a snapshot carry the same id for every client. When shadowing a live
  feed, Coinbase's own trade ids are preserved and the simulator numbers from above the highest seen.
- The subscribe-time trade snapshot is split against the book snapshot the subscriber receives
  (`MDTradeSnapshotResponse`): trades that book does not reflect are delivered as a normal update
  rather than presented as settled history. The split keys on the new `UpdateFlags::F_NOT_IN_BOOK`,
  set only on prints seeded from a venue's own trade snapshot. `order_book_->lastUpdateTime()` cannot
  serve as the boundary on its own — it tracks level updates only, so an already-applied trade would
  be announced as still to come while the subscriber's book snapshot already reflected it. Since that
  test is not a timestamp it is not monotonic over the trade history, so the response is built by
  partitioning into two contiguous blocks rather than copying in history order — otherwise the blocks
  interleave while the counts say otherwise, and the consumer reads one block's trades as the other's.
- `HyperliquidExchange` answers `trades` subscriptions. It previously handled only `l2Book`/`l2`, so
  a `trades` subscriber never received a SUB_RESPONSE — and therefore never got the subscription
  confirmation either, which the publisher only emits on one.
- Self-match prevention is reachable from a running simulator. It was fully implemented and unit
  tested but inert: nothing outside the tests ever assigned `Order::client_id`, so `isSelfMatch`
  always returned false; `Symbol::smp_mode_` had no configuration key; and `Symbol::modifyOrder`
  dropped the mode, so amendments matched under `NONE` regardless. Now a `self_match_prevention` key
  (`"none"` | `"cancel_resting"` | `"cancel_newest"`) on an exchange applies to every symbol it
  creates and to both order paths, and `Exchange::clientIdFor` derives the identity from the
  authenticated `user_id`. Orders with no `user_id` stay exempt, including from each other.
- `CANCEL_NEWEST` is decided before the order is acked, mutated or reported on, so a rejected order
  leaves no trace. Previously the check ran inside the matching loop, after a new order had been
  acked and after an amendment had already written its new price and quantity onto the stored order
  and reported `REPLACED` — so a rejected amendment left the order half-changed against a book that
  never moved, and the client received `REPLACED`, `CANCELED` and `REJECTED` for one request. There
  is now a single terminal response. A fill-or-kill order blocked by SMP reports `SMP` rather than
  `FOK_CANNOT_FILL`, since the liquidity existed and SMP is why it could not trade.
- `CANCEL_RESTING` now cancels the resting order properly: it publishes `CANCELED` to the order's
  owner and returns the `Order` to the pool, instead of only removing it from the book — which left
  the client believing its order was still live and leaked the pooled allocation.
- The `CANCEL_NEWEST` pre-check scans only as deep as the order will actually trade. It used the
  order's total quantity, which for a partially filled amendment exceeds its remaining leaves, so it
  could reach one of the owner's resting orders the amendment would never touch and reject a fill
  that should have happened.
- `Symbol::addOrder` returns any order that does not rest to the pool, not just fully filled ones.
  Every reject (SMP, FOK) and every IOC remainder leaked its `Order`, and because
  `slick::ObjectPool` falls back to heap allocation once exhausted, the leak degraded silently
  instead of failing — a client repeating a rejected order would drain the pool and then leak heap.
- `rejectModifyOrderRequest` and `rejectCancelOrderRequest` read the correct union member of
  `Request`. Both read `add_order`, so the reject carried the *order id* in its `client_order_id`
  field and read price/quantity from the wrong offsets; a cancel reject also reported a price and
  quantity that do not exist on a cancel.

## Fixed

- A venue trade and the level update reflecting it no longer reduce the same price level twice. The
  quantity a trade removes from phantom liquidity is credited per price level, and per trade
  timestamp, so a level update landing between two trades at the same price offsets by the later one
  alone rather than by both. `CoinbaseExchange::EventCompare` also breaks timestamp ties by applying
  trades ahead of level updates.
- Coinbase `l2_data` deltas now carry the level's `total_quantity` as the book actually holds it,
  rather than the venue's raw `event.qty`. That fixes two divergences: the book may decline to take
  an update at face value (a stale update whose target `reconcilePhantomQty` lowered, or a new level
  that rested only what survived matching), and the raw figure omitted the simulator's own resting
  orders — which a real venue's L2 would show. Deltas now share one basis with
  `Symbol::onPriceLevelUpdate` and `populateL2Snapshot`, so a client can re-read a snapshot and find
  it consistent with the deltas it has been applying.
- `HyperliquidExchange` no longer discards the trade summaries returned by `match()` in
  `processL2Snapshot` and `applyPhantomLevelUpdate`. Fills caused by feed-driven level crossings were
  invisible on the public tape, even though the client's own fill report was sent.
- `CoinbaseExchange::onMarketTradesSnapshot` no longer writes past the end of its queue reservation.
  A second loop wrote another `snapshots.size()` trades through the already-advanced pointer, into a
  slot reserved for `snapshots.size()` entries. Reached whenever a `market_trades` subscription was
  pending and a trades snapshot arrived.
- `TradeSummary::security_id` is now populated. It was never written, so consumers read uninitialised
  queue memory.
- `MDTrade::event_time`, `seq_num` and `flags` are now set on every trade written to a subscription
  snapshot. Two of the three Coinbase producers left them as whatever the recycled queue slot held.

## Changed

- `Exchange::publishTradeSummary` takes a `Symbol*` rather than a symbol name, so it can stamp the
  security id and record the trade in the symbol's history.
- Level reconciliation is shared. `Exchange::reconcilePhantomQty` replaces the near-identical
  increase/decrease bodies in `CoinbaseExchange::dispatchEvent` and
  `HyperliquidExchange::applyPhantomLevelUpdate`, and is where the double-reduction guard lives.
- `Exchange::publishMDTrades` and `HyperliquidPublisher::publish_trade_update` are removed along with
  both `trade_update_buffer_` members and `CoinbaseExchange::trade_snapshots_`. Their recent-trades
  role is served by the bounded per-symbol `Symbol::trade_history_`, which is fed by every published
  trade rather than only by relayed venue prints. `MDUpdateType::TRADE` is retained as a reserved
  enumerator so the values after it keep their numbering.

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
