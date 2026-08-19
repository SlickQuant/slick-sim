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
- Every publish path read past the end of the name it was copying. `memcpy(dst, name.c_str(),
  sizeof(dst))` copies the full width of the wire field out of a buffer holding only as many
  characters as the name actually has — 32 bytes read from a 7-character `"BTC-USD"`. It appeared on
  `Order`'s four identity fields (once per field, four times per response, in all seven response
  builders), on `Symbol::symbol_` in `publishTradeSummary`, `publishTradeSubscriptionResponse`,
  `publishLevelUpdate` and `publishMDOrderUpdate`, on `OrderBook::symbol_` in both `populateL2*`
  methods, and on the subscribe and unsubscribe paths of both publishers. Names are now held at their
  wire width, so the copy is in bounds by construction.
- The over-read was not only a technical one on the publisher subscribe/unsubscribe paths, which also
  skipped the zero-fill: whatever followed the name in memory landed in `Request::symbol`, and the
  exchange thread reads that field as a C string. A single non-zero byte there changed the symbol
  being subscribed to, so it worked only because a short `std::string`'s inline buffer happens to be
  zero-padded.
- The REST gateways left wire fields unterminated. Both used
  `memcpy(dst, s.c_str(), std::min(sizeof(dst), s.size()))`, which is bounded but clamps one byte too
  late: a value that exactly fills the field overwrites the last zero the preceding `memset` wrote,
  and the reader then runs past the end of the field. It affected `symbol`, `user_id` and `order_id`
  on Coinbase and `symbol` on Hyperliquid; only Coinbase's `client_order_id` had a hand-written guard.
  All 24 such copies now go through `utils::copy_wire_field`, which truncates to N-1, zero-fills the
  remainder, and reports truncation so the existing `client_order_id` warning still fires.
- The resting side of a trade now gets an execution report. When one client's order filled another's
  resting order, only the aggressor's owner was told: `match()` publishes for the aggressor and then
  calls `book.executeOrder()`, which books the fill onto the resting `Order` but cannot publish —
  `OrderBook` holds no reference to the matching engine. The resting client saw its order silently
  disappear from the book with no `TRADE` report, no fill price and no quantity. Only the aggressor
  path was affected; the market-data replay path in the second `match()` overload always reported
  both sides, and SMP's `CANCEL_RESTING` always published its cancel to the resting owner.
- A resting order filled completely by another client's order is returned to the pool. On a full
  fill `OrderBook::executeOrder` drops it from the three lookup maps but never calls `freeOrder`, so
  every completed passive fill leaked a pooled `Order` — and `slick::ObjectPool` falls back to the
  heap once exhausted, so it degraded silently rather than failing. This is the same leak already
  fixed for rejects and IOC remainders in `Symbol::addOrder`, on the path that fills instead.
- An amendment recomputes `leaves_quantity` from `cum_quantity` instead of adjusting it by the
  quantity delta, so it cannot drift from the fill state. An order amended to at or below what it has
  already filled now settles at zero leaves and is cancelled, rather than relying on a negative-value
  guard after the fact.
- `Symbol`'s move constructor and move assignment dropped `md_level_update_cache_` and
  `md_order_update_cache_` instead of moving them. `SymbolManager::createSymbol` builds a `Symbol` on
  the stack and moves it into a vector, so every symbol in the process ran with zero reserved
  capacity and both caches reallocated from scratch as market data arrived.
- The Coinbase WebSocket `user` channel sends the subscriber its real orders. The snapshot read
  `orders_by_client_id_`, which was declared but never written, so it was always empty whatever the
  client had traded. That container and two more beside it (`orders_`, `order_fills_`) were dead and
  are gone, replaced by the shared `OrderStore` - fed from `process_all_responses()`, which already
  reads every execution report, so the gateway takes no second pass over the queue.
- Coinbase `GET /api/v3/brokerage/orders/historical/batch` answers. The authenticated path built a
  response body and returned without calling `end()`, so the request was never completed and the
  client blocked until its own timeout; only the 401 path replied. It also had nothing to report -
  the endpoint was a stub whose orders array was always empty.
- Both REST gateways' create-order handlers captured the response cursor *after* publishing the
  request. `slick::queue::initial_reading_index()` returns the queue's current write cursor, and the
  exchange thread is a spin loop, so it could consume the request and publish `PENDING_NEW` in the
  gap — leaving the handler polling from a point past its own response until it gave up and returned
  a 504 timeout for an order that had actually been accepted, and might already have filled. The
  capture now precedes the enqueue, matching the cancel and modify handlers that already did this.
- `SymbolManager` handed out `Symbol*` that went stale. It owned its instruments in a
  `std::vector` reserved for 512 while storing raw pointers to them in its lookup map, so creating
  the 513th relocated every element: the map's entries, the pointers the exchanges had stored at
  subscription time, and the return value of the create that triggered it all dangled at once.
  Storage is now a `std::deque`, which never moves an element it already holds.
- `SymbolManager`'s lookup was keyed on the instrument name alone, so the same ticker on two venues
  collided. The second `createSymbol` found the name present, and its `Symbol` was built but never
  became reachable — every lookup for it returned the other venue's instrument, routing orders and
  market data to the wrong book. The map is keyed by `(Venue, name)` now, and `getSymbol` takes the
  venue. Re-creating an existing instrument returns it rather than stranding a duplicate.
- Both heartbeat timers wrote their `this` pointer past the end of the timer allocation.
  `us_create_timer(loop, fallthrough, ext_size)` takes the extension size as its **third** argument,
  and both call sites passed `0` before storing a pointer at `us_timer_ext(timer)` — an
  out-of-bounds heap write of `sizeof(void*)` bytes, read back on every ping. It affected
  `WebsocketMarketDataPublisher` (so every venue's market-data publisher) and
  `CoinbaseWebsocketOrderGateway`. Both now request `sizeof(T*)`.

- `order_gateway::OrderStore` (`src/order_gateway/order_store.hpp`): a gateway-side projection of
  order state, folded from the execution-report stream. A gateway cannot answer "list this user's
  orders" from the order books - those belong to the exchange thread - but every response carries the
  order's identity and its state at that moment, so replaying the stream rebuilds each order without
  touching anything the exchange owns. One instance per gateway with its own queue cursor, so it
  never disturbs the handlers polling the same queue for their own responses, and no locking is
  needed because a gateway only touches it from its own event loop. Two feeding modes: a store
  constructed with a queue drains it on demand (the REST gateway, which has no continuous reader
  of its own), while a default-constructed one is fed through `apply()` by an owner already
  reading the stream (the WebSocket gateway).

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
- New `utils::fixed_string<N>` (`src/utils/fixed_string.hpp`): a null-terminated buffer stored inline,
  laid out as exactly `char[N]` so a run of them matches a run of fixed-width wire fields byte for
  byte. Alongside it, `utils::copy_wire_field(dst_array, src_view)` replaces every
  `memcpy(dst, s.c_str(), sizeof(dst))` and `memset` + clamped-`memcpy` pair in the tree.
  **When logging one, pass `.view()`** — slick-logger dispatches on exact argument types and falls
  through to `std::to_string` for anything else, so passing a `fixed_string` is a compile error rather
  than a wrong value. `std::format` and `operator<<` work directly.
- `Order`'s `symbol`, `user_id`, `client_order_id` and `order_id` are `utils::fixed_string` instead of
  `std::string`, grouped into an `OrderIdentity` base that `Order` derives from. Assignment, `.empty()`,
  comparison and formatting are unchanged at the call site. Two consequences: the order path no longer
  allocates for them — the 36-character `order_id` always exceeded the small-string buffer — and a
  response header is filled by one `setOrderIdentity()` memcpy of the whole block rather than four
  copies out of four unrelated heap blocks. `messages.hpp` static_asserts the layout against
  `OrderResponse`, so resizing a field on either side fails the build.
- `Symbol::symbol_` and `OrderBook::symbol_` are `symbol_name_t`, declared in `market_data.hpp` as
  `utils::fixed_string<sizeof(MarketDataUpdate::symbol)>`. Deliberately derived from the wire field
  rather than spelled out, so widening that field widens the storage with it instead of silently
  reintroducing the over-read at every publish site. `OrderBook::symbolName()` returns it, and
  `Exchange::publishLevelUpdate`/`publishMDOrderUpdate` take it in place of a bare `const char*`, so
  the width guarantee survives the call.
- `OrderBook`'s `orders_by_client_order_id_` and `orders_by_order_id_` are keyed by
  `utils::fixed_string<37>` rather than `std::string`, removing a second heap allocation per resting
  order. The existing transparent `StringViewHash`/`StringViewEq` functors take `std::string_view`, so
  lookups by `string_view` still find a `fixed_string` key unchanged.
- `OrderBook::allocateOrder` no longer constructs a `boost::uuids::random_generator` per call, which
  seeded a Mersenne twister from the OS entropy source for every order. It is now a
  `static thread_local`, and the UUID is rendered with `to_chars` straight into the order's own buffer
  instead of through a `boost::lexical_cast<std::string>` temporary.
- `Symbol`'s market-data caches reserve 256 levels and 1024 orders, up from 64 and 256. Every call
  site only ever `clear()`s them, so the reserved capacity is what they keep for the process's
  lifetime.
- `canFillCompletely` no longer resolves each resting order to its full `Order` when self-match
  prevention is off — that lookup exists only to compare client ids, and it was a hash lookup per
  order on every fill-or-kill pre-check. Its redundant `available_qty` accumulator and the unreachable
  post-loop check are gone; the early return already covers every path that finds enough liquidity.
- `WebsocketMarketDataPublisher::publish_processing` drains up to 1000 updates per pass instead of
  100, so a market-data flood costs one `loop_->defer` per 1000 updates rather than per 100. The
  reschedule stays unconditional and is now commented to say why: that defer is the only thing that
  runs the function again, since `md_queue_` is lock-free with no wakeup and the exchange thread never
  touches the loop, so skipping it on an empty queue would stop the publisher permanently the first
  time it caught up.
- `Exchange::clientIdFor` looks the user up by `std::string_view` and only materialises a key when
  the user is new. It ran once per new order and built a `std::string` every time purely to probe the
  map — an allocation for any `user_id` past the small-string buffer, on a path whose whole point is
  not to allocate. `client_ids_` is keyed by `utils::fixed_string<sizeof(AddOrderMessage::user_id)>`
  so the stored key is inline too.
- `StringViewHash`/`StringViewEq` moved from private members of `OrderBook` to `utils`, since
  `client_ids_` now needs the same transparent lookup. `OrderBook` keeps its old names as aliases.
- `TradeSummaryInfo` reserves four legs in `Trades`. A summary covers one price level, so even the
  common two-leg fill (one aggressor, one resting) reallocated twice growing 1 → 2.

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
