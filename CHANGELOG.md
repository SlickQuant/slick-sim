# Unreleased

## Added

- Trade updates on both venues' market-data channels. Coinbase `market_trades` and Hyperliquid
  `trades` now deliver a recent-trades snapshot on subscribe followed by a message per executed
  trade. Previously a subscriber got an empty snapshot and then silence: the Coinbase publisher had
  no case for trades at all, and neither publisher handled `MDUpdateType::TRADE_SUMMARY`, so every
  fill the matching engine produced was written to `md_queue_` and dropped.
- A venue's own trade print is replayed through the matching engine as an incoming aggressor order
  instead of being relayed verbatim, so it takes liquidity exactly as the real trade did and fills any
  simulator order ahead of the phantom liquidity it sweeps. Only the resulting fills are published;
  anything the print cannot fill rests on the aggressor's side at the print price.
- `MDTrade::trade_id`, an independent sequence per venue, so a live print and the same trade replayed
  in a snapshot carry the same id for every client. When shadowing a live feed, Coinbase's own ids are
  preserved and the simulator numbers from above the highest seen.
- The subscribe-time trade snapshot is split against the book snapshot the subscriber receives
  (`MDTradeSnapshotResponse`): trades that book does not reflect are delivered as a normal update
  rather than as settled history, keyed on the new `UpdateFlags::F_NOT_IN_BOOK`.
  `order_book_->lastUpdateTime()` cannot draw the boundary because it tracks level updates only, and
  the flag is not monotonic over the trade history, so the response is built by partitioning into two
  contiguous blocks — copying in history order would interleave them while the counts said otherwise.
- `HyperliquidExchange` answers `trades` subscriptions. It handled only `l2Book`/`l2`, so a `trades`
  subscriber never received a SUB_RESPONSE, and therefore never the subscription confirmation that
  the publisher only emits on one.
- Self-match prevention is reachable from a running simulator. It was implemented and unit tested but
  inert: nothing outside the tests assigned `Order::client_id`, `Symbol::smp_mode_` had no
  configuration key, and `Symbol::modifyOrder` dropped the mode. A `self_match_prevention` key
  (`"none"` | `"cancel_resting"` | `"cancel_newest"`) on an exchange now applies to every symbol it
  creates and to both order paths, with `Exchange::clientIdFor` deriving the identity from the
  authenticated `user_id`. Orders with no `user_id` stay exempt, including from each other.
- `utils::fixed_string<N>` (`src/utils/fixed_string.hpp`): a null-terminated buffer stored inline, laid
  out as exactly `char[N]` so a run of them matches a run of fixed-width wire fields byte for byte.
  `utils::copy_wire_field(dst_array, src_view)` replaces every hand-written wire-field copy in the
  tree. **When logging one, pass `.view()`** — slick-logger dispatches on exact argument types and
  falls through to `std::to_string`, so passing a `fixed_string` is a compile error. `std::format` and
  `operator<<` work directly.
- `order_gateway::PendingResponses` (`src/order_gateway/pending_responses.hpp`): correlates an
  in-flight HTTP request with the execution report that answers it, so a REST handler can register
  what it waits for and return instead of polling — the first half of removing the blocking waits in
  [known gaps](docs/known-gaps.md). Nothing calls it yet: the uWebSockets side (holding an
  `HttpResponse` past handler return, the abort path, the per-request timer) is its own change with an
  integration test. Waiters match by predicate rather than a keyed lookup because handlers correlate
  on different things — a create knows its `client_order_id`, a cancel only the timestamp it stamped.
  A timer that fires after its report arrived is a no-op and an abandoned waiter is never completed,
  so neither can answer a request twice or write to a closed socket.
- `order_gateway::OrderStore` (`src/order_gateway/order_store.hpp`): a gateway-side projection of
  order state folded from the execution-report stream, so a gateway can answer "list this user's
  orders" without touching the order books the exchange thread owns. One instance per gateway with its
  own queue cursor, so it never disturbs handlers polling the same queue, and no locking, since a
  gateway touches it only from its own event loop. A store constructed with a queue drains it on
  demand (the REST gateway, which has no continuous reader); a default-constructed one is fed through
  `apply()` by an owner already reading the stream (the WebSocket gateway).

## Fixed

- Self-match prevention under `CANCEL_NEWEST` is decided before the order is acked, mutated or
  reported on, so a rejected order leaves no trace. The check ran inside the matching loop, after a
  new order was acked and after an amendment had written its new price and quantity onto the stored
  order and reported `REPLACED` — leaving a rejected amendment half-applied against a book that never
  moved, and the client holding `REPLACED`, `CANCELED` and `REJECTED` for one request. There is now a
  single terminal response, and a fill-or-kill order blocked by SMP reports `SMP` rather than
  `FOK_CANNOT_FILL`, since the liquidity existed.
- The `CANCEL_NEWEST` pre-check scans only as deep as the order will actually trade. It used total
  quantity, which for a partially filled amendment exceeds its remaining leaves, so it could reach one
  of the owner's resting orders the amendment would never touch and reject a fill that should have
  happened.
- `CANCEL_RESTING` publishes `CANCELED` to the resting order's owner and returns the `Order` to the
  pool, instead of only removing it from the book — which left the client believing its order was live
  and leaked the allocation.
- Orders that do not rest are returned to the pool. `Symbol::addOrder` freed only fully filled orders,
  leaking every reject (SMP, FOK) and every IOC remainder, and `OrderBook::executeOrder` dropped a
  fully filled resting order from its three lookup maps without calling `freeOrder`, leaking every
  completed passive fill. `slick::ObjectPool` falls back to heap allocation once exhausted, so both
  leaks degraded silently instead of failing.
- The resting side of a trade gets an execution report. `match()` published for the aggressor and then
  called `book.executeOrder()`, which books the fill but cannot publish — `OrderBook` holds no
  reference to the matching engine — so the resting client saw its order disappear from the book with
  no `TRADE` report, fill price or quantity. Only the aggressor path was affected; the market-data
  replay overload always reported both sides, as did `CANCEL_RESTING`.
- An amendment recomputes `leaves_quantity` from `cum_quantity` instead of adjusting it by the
  quantity delta, so it cannot drift from the fill state. An order amended to at or below what it has
  already filled settles at zero leaves and is cancelled, rather than relying on a negative-value
  guard after the fact.
- `rejectModifyOrderRequest` and `rejectCancelOrderRequest` read the correct union member of
  `Request`. Both read `add_order`, so the reject carried the *order id* in its `client_order_id`
  field and read price and quantity from the wrong offsets — neither of which a cancel even has.
- The market-data replay overload of `match()` marks the end of a batch. `is_last_in_batch` was passed
  as `qty == 0` but read *before* the fill was subtracted, and the loop only runs while `qty > 0`, so
  it was permanently false: the level update carrying the final fill of a replayed print never set
  `UpdateFlags::F_END_EVENT`, and a client batching until that marker never saw its batch close. The
  order-driven overload already subtracted first; the two now agree.
- A venue trade and the level update reflecting it no longer reduce the same price level twice. The
  quantity a trade removes from phantom liquidity is credited per price level and per trade timestamp,
  so a level update landing between two trades at the same price offsets by the later one alone.
  `CoinbaseExchange::EventCompare` also breaks timestamp ties by applying trades ahead of level
  updates.
- Coinbase `l2_data` deltas carry the level's `total_quantity` as the book actually holds it, not the
  venue's raw `event.qty`. The book may decline an update at face value (a stale one whose target
  `reconcilePhantomQty` lowered, or a new level that rested only what survived matching), and the raw
  figure omitted the simulator's own resting orders, which a real venue's L2 would show. Deltas now
  share one basis with `Symbol::onPriceLevelUpdate` and `populateL2Snapshot`, so a re-read snapshot is
  consistent with the deltas a client has been applying.
- `HyperliquidExchange` no longer discards the trade summaries returned by `match()` in
  `processL2Snapshot` and `applyPhantomLevelUpdate`. Fills caused by feed-driven level crossings were
  invisible on the public tape, even though the client's own fill report was sent.
- `CoinbaseExchange::onMarketTradesSnapshot` no longer writes past the end of its queue reservation: a
  second loop wrote another `snapshots.size()` trades through the already-advanced pointer. Reached
  whenever a `market_trades` subscription was pending and a trades snapshot arrived.
- `TradeSummary::security_id` is populated. It was never written, so consumers read uninitialised
  queue memory.
- `MDTrade::event_time`, `seq_num` and `flags` are set on every trade written to a subscription
  snapshot. Two of the three Coinbase producers left them as whatever the recycled queue slot held.
- `MDSubscriptionResponse::reject_reason` is written on every path and checked before the payload
  behind it is read. Three gaps lined up: `rejectMdSubscription` marked the reason `[[maybe_unused]]`,
  the L2 accept path set only `channel`, and both publishers cast `response->data` to a `BookSnapshot`
  without consulting the field — but a reject reserves room for the response header alone, so a
  publisher reaching past it reads `num_bid`/`num_ask` from beyond the frame and walks that many
  levels (Hyperliquid also `reserve()`s on those counts). Latent rather than live: nothing calls
  `rejectMdSubscription` yet. Publishers now report the rejection to the channel's subscribers via the
  venue's own error message, and `to_string(MDSubscriptionRejectReason)` names it.
- Every publish path read past the end of the name it was copying. `memcpy(dst, name.c_str(),
  sizeof(dst))` copies the full width of the wire field out of a buffer holding only the name's own
  characters — 32 bytes read from a 7-character `"BTC-USD"`. It appeared on `Order`'s four identity
  fields (once per field, four times per response, in all seven response builders), on
  `Symbol::symbol_` in `publishTradeSummary`, `publishTradeSubscriptionResponse`, `publishLevelUpdate`
  and `publishMDOrderUpdate`, on `OrderBook::symbol_` in both `populateL2*` methods, and on both
  publishers' subscribe and unsubscribe paths. Names are now held at their wire width, so the copy is
  in bounds by construction.
- On the publishers' subscribe/unsubscribe paths that over-read also skipped the zero-fill: whatever
  followed the name in memory landed in `Request::symbol`, which the exchange thread reads as a C
  string, so one non-zero byte there changed the symbol being subscribed to. It worked only because a
  short `std::string`'s inline buffer happens to be zero-padded.
- The REST gateways left wire fields unterminated. Both used
  `memcpy(dst, s.c_str(), std::min(sizeof(dst), s.size()))`, which is bounded but clamps one byte too
  late: a value that exactly fills the field overwrites the last zero the preceding `memset` wrote,
  and the reader runs past the end. It affected `symbol`, `user_id` and `order_id` on Coinbase and
  `symbol` on Hyperliquid; only Coinbase's `client_order_id` had a hand-written guard. All 24 such
  copies now go through `utils::copy_wire_field`, which truncates to N-1, zero-fills the remainder and
  reports truncation so the existing `client_order_id` warning still fires.
- `to_price_t`/`to_qty_t` round to the nearest tick instead of truncating. `value * 1e8` is a binary
  floating-point multiply and most decimal fractions have no exact binary form, so the product landed
  just below the intended integer and the cast threw the remainder away: 8.2 became 819999999, a price
  of 8.19999999. 0.29 and 4.35 went the same way, on prices and quantities alike, for every value
  parsed off a venue feed or a client order.
- Fixed-point values render onto the wire straight from the integer, through the new
  `to_price_string`/`to_qty_string`, instead of `std::to_string(double)` — which formats with `%f`,
  exactly six fractional digits against a scale of eight, so a size of 0.00011628 was published as
  "0.000116" and a single tick as "0.000000", which a receiver reads as zero. All 41 outbound sites
  across the publishers, encoders and REST/WS gateways use the exact form; genuine doubles (fees,
  notionals, completion percentages) are unchanged. Whole numbers render without a decimal point and
  trailing fractional zeros are trimmed, so 99.0 is "99" and 8.2 is "8.2".
- The Coinbase WebSocket `user` channel sends the subscriber its real orders. The snapshot read
  `orders_by_client_id_`, which was declared but never written, so it was always empty whatever the
  client had traded. That container and two more beside it (`orders_`, `order_fills_`) were dead and
  are gone, replaced by the shared `OrderStore` — fed from `process_all_responses()`, which already
  reads every execution report, so the gateway takes no second pass over the queue.
- Coinbase `GET /api/v3/brokerage/orders/historical/batch` answers. The authenticated path built a
  response body and returned without calling `end()`, so the request was never completed and the
  client blocked until its own timeout; only the 401 path replied. It also had nothing to report — the
  endpoint was a stub whose orders array was always empty.
- Both REST gateways' create-order handlers capture the response cursor *before* publishing the
  request. `slick::queue::initial_reading_index()` returns the queue's current write cursor, and the
  exchange thread is a spin loop, so it could consume the request and publish `PENDING_NEW` in the gap
  — leaving the handler polling from past its own response until it returned a 504 for an order that
  had actually been accepted, and might already have filled. The cancel and modify handlers already
  did this.
- Both heartbeat timers wrote their `this` pointer past the end of the timer allocation.
  `us_create_timer(loop, fallthrough, ext_size)` takes the extension size as its **third** argument,
  and both call sites passed `0` before storing a pointer at `us_timer_ext(timer)` — an out-of-bounds
  heap write of `sizeof(void*)` bytes, read back on every ping. It affected
  `WebsocketMarketDataPublisher` (so every venue's market-data publisher) and
  `CoinbaseWebsocketOrderGateway`; both now request `sizeof(T*)`.
- `HyperliquidPublisher` re-declared nine members that already exist in `WebsocketMarketDataPublisher`
  (`ws_thread_`, `listen_socket_`, `loop_`, `heartbeat_timer_`, `port_`, `running_`, `data_cursor_`,
  `ping_interval_ms_`, `pong_timeout_`), shadowing the base copies that the inherited `start()`/`stop()`
  actually drive; `check_heartbeats()` read the shadowed `pong_timeout_`. `publish_market_data_update`
  also gained a missing `override`.
- `SymbolManager` handed out `Symbol*` that went stale. It owned its instruments in a `std::vector`
  reserved for 512 while storing raw pointers to them in its lookup map, so creating the 513th
  relocated every element and dangled the map's entries, the pointers exchanges had stored at
  subscription time, and the return value of the create that triggered it, all at once. Storage is now
  a `std::deque`, which never moves an element it already holds.
- `SymbolManager`'s lookup was keyed on the instrument name alone, so the same ticker on two venues
  collided: the second `createSymbol` found the name present and its `Symbol` never became reachable,
  so every lookup returned the other venue's instrument, routing orders and market data to the wrong
  book. The map is keyed by `(Venue, name)` now and `getSymbol` takes the venue; re-creating an
  existing instrument returns it rather than stranding a duplicate.
- `Symbol`'s move constructor and move assignment move `md_level_update_cache_` and
  `md_order_update_cache_` instead of dropping them. `SymbolManager::createSymbol` builds a `Symbol` on
  the stack and moves it into its container, so every symbol in the process ran with zero reserved
  capacity and both caches reallocated from scratch as market data arrived.
- `Exchange::stop()` stops the market-data feeds — the base class knew only the singular `md_feed_`
  while both adapters kept their own `md_feeds_` vector — and joins the exchange thread *before*
  touching them, since `poll()` and `handleMdUnsubscription` both access that vector on it. It is also
  idempotent now, because `main()` stops each exchange and `~Exchange()` stops it again, which was
  harmless only while every step was individually guarded, and stopping the feeds is not:
  `CoinbaseLiveWSFeed::stop()` unsubscribes upstream. It no longer dereferences `md_publisher_`
  unconditionally either, despite `start()` null-checking it.
- `Boost::multiprecision` is linked rather than relying on Boost being on the default include path.
- `order_gateway`'s `PUBLIC` include directories, which every other target silently depended on to
  resolve `<common/...>`, are replaced by a deliberate `slick_sim_core` INTERFACE target.
- `BUILD_COINBASE_ADVANCED_TESTS=OFF` moved from the top-level slick-orderbook block, where it ran
  *after* the Coinbase SDK had already been populated and so never reached it, into the Coinbase
  FetchContent path it names.

## Changed

- `Exchange::publishTradeSummary` takes a `Symbol*` rather than a symbol name, so it can stamp the
  security id and record the trade in the symbol's history.
- Level reconciliation is shared: `Exchange::reconcilePhantomQty` replaces the near-identical
  increase/decrease bodies in `CoinbaseExchange::dispatchEvent` and
  `HyperliquidExchange::applyPhantomLevelUpdate`, and is where the double-reduction guard lives.
- `Exchange::publishMDTrades` and `HyperliquidPublisher::publish_trade_update` are gone, along with
  both `trade_update_buffer_` members and `CoinbaseExchange::trade_snapshots_`. Their recent-trades
  role is served by the bounded per-symbol `Symbol::trade_history_`, fed by every published trade
  rather than only by relayed venue prints. `MDUpdateType::TRADE` is retained as a reserved enumerator
  so the values after it keep their numbering.
- `Order`'s `symbol`, `user_id`, `client_order_id` and `order_id` are `utils::fixed_string` instead of
  `std::string`, grouped into an `OrderIdentity` base. Assignment, `.empty()`, comparison and
  formatting are unchanged at the call site. Two consequences: the order path no longer allocates for
  them — the 36-character `order_id` always exceeded the small-string buffer — and a response header
  is filled by one `setOrderIdentity()` memcpy of the whole block rather than four copies out of four
  unrelated heap blocks. `messages.hpp` static_asserts the layout against `OrderResponse`, so resizing
  a field on either side fails the build.
- `Symbol::symbol_` and `OrderBook::symbol_` are `symbol_name_t`, declared in `market_data.hpp` as
  `utils::fixed_string<sizeof(MarketDataUpdate::symbol)>` — derived from the wire field rather than
  spelled out, so widening that field widens the storage with it instead of silently reintroducing the
  over-read at every publish site. `OrderBook::symbolName()` returns it, and
  `Exchange::publishLevelUpdate`/`publishMDOrderUpdate` take it in place of a bare `const char*`, so
  the width guarantee survives the call.
- `OrderBook`'s `orders_by_client_order_id_` and `orders_by_order_id_` are keyed by
  `utils::fixed_string<37>` rather than `std::string`, removing a second heap allocation per resting
  order; the transparent `StringViewHash`/`StringViewEq` functors take `std::string_view`, so lookups
  by `string_view` still find a `fixed_string` key unchanged. Both functors moved from private members
  of `OrderBook` to `utils`, since `client_ids_` now needs the same transparent lookup; `OrderBook`
  keeps its old names as aliases.
- The Coinbase publisher serialises each market-data message once instead of once per subscriber.
  Every subscriber gets its own `sequence_num`, so the payload differed by a single integer — but
  `dump()` sat inside the fan-out loop, walking the whole document and building a fresh string per
  socket. `md_publisher::SequencedMessage` dumps once and splices the number in with `std::to_chars`
  into a reused buffer: ~47x faster on a ten-level `l2_data` update with 50 subscribers. Applies to
  level updates, book snapshots, subscription snapshots and trade prints; the remaining `dump()` calls
  are genuinely per-socket content. Hyperliquid already sent one shared payload and is unchanged.
- `OrderBook::allocateOrder` no longer constructs a `boost::uuids::random_generator` per call, which
  seeded a Mersenne twister from the OS entropy source for every order. It is a `static thread_local`
  now, and the UUID is rendered with `to_chars` straight into the order's own buffer instead of
  through a `boost::lexical_cast<std::string>` temporary.
- `Exchange::clientIdFor` looks the user up by `std::string_view` and materialises a key only when the
  user is new. It ran once per new order and built a `std::string` every time purely to probe the map
  — an allocation for any `user_id` past the small-string buffer, on a path whose whole point is not
  to allocate. `client_ids_` is keyed by `utils::fixed_string<sizeof(AddOrderMessage::user_id)>` so
  the stored key is inline too.
- `canFillCompletely` no longer resolves each resting order to its full `Order` when self-match
  prevention is off — that lookup exists only to compare client ids, and it was a hash lookup per
  order on every fill-or-kill pre-check. Its redundant `available_qty` accumulator and unreachable
  post-loop check are gone; the early return already covers every path that finds enough liquidity.
- `Symbol`'s market-data caches reserve 256 levels and 1024 orders, up from 64 and 256, and
  `TradeSummaryInfo` reserves four legs in `Trades` — a summary covers one price level, so even the
  common two-leg fill reallocated twice growing 1 → 2. Every call site only ever `clear()`s these, so
  the reserved capacity is what they keep for the process's lifetime.
- `WebsocketMarketDataPublisher::publish_processing` drains up to 1000 updates per pass instead of
  100, so a market-data flood costs one `loop_->defer` per 1000 updates rather than per 100. The
  reschedule stays unconditional and is now commented to say why: that defer is the only thing that
  runs the function again, since `md_queue_` is lock-free with no wakeup and the exchange thread never
  touches the loop, so skipping it on an empty queue would stop the publisher permanently the first
  time it caught up.
- **Venue code is separated from the core and each venue builds optionally.** Everything specific to a
  venue — its `Exchange` subclass, market-data feed, order gateways, publisher and wire encoders —
  moved out of the five component directories into a single `src/venues/<venue>/` directory built as
  one target. `SLICK_SIM_ENABLE_COINBASE` and `SLICK_SIM_ENABLE_HYPERLIQUID` (both default `ON`) gate
  that target, its unit tests and the venue's vendor SDK, so a Hyperliquid-only build neither fetches
  nor links the Coinbase SDK. No core target contains venue code or links a venue SDK. Adding a venue
  is a new directory plus one line in `SLICK_SIM_VENUE_LIST`; see `docs/extending.md`.
- **A venue resolves its own dependencies in its own `CMakeLists.txt`** — its vendor SDK and its
  networking stack, the latter declared in the `DEPENDS` of its `slick_sim_add_venue()` call. The
  top-level file names no venue library at all, and `SLICK_SIM_ENABLE_<VENUE>` is tested in exactly
  one place: `src/venues/CMakeLists.txt`, which adds the directory. Those lookups pass `GLOBAL`,
  without which the imported targets would be invisible to `slick-sim` and `slick_sim_tests` in their
  sibling scopes. `slick::net` belongs to Coinbase and Hyperliquid, whose gateways speak HTTP, instead
  of being linked on "any venue is enabled" — which would have forced it onto a CME or ICE adapter
  that speaks a binary session protocol over `slick::socket` and never calls it. It previously arrived
  only as a transitive requirement of the venue SDKs, so a build with both venues off failed to link.
  `main.cpp`'s slick-net log bridge is gated on `SLICK_SIM_HAS_SLICK_NET`, derived from the venues'
  `DEPENDS` so `main.cpp` still names no venue, in place of `SLICK_SIM_HAS_ANY_VENUE`. `jwt-cpp` moved
  to Coinbase with the rest: only its two gateways sign with it.
- **uWebSockets left the core too.** `rest_ws_order_gateway.cpp` and `ws_md_publisher.cpp` are the
  project's only uWebSockets translation units, and only a venue serving HTTP/WebSocket derives from
  `RestWsOrderGateway`/`WebsocketMarketDataPublisher`, so they compile into `slick_sim_rest_ws` — a
  target created on demand by `slick_sim_require_rest_ws()` from the venue that asks for it.
  `market_data_publisher` becomes an INTERFACE target (that source was its only one), and
  `order_gateway` carries just the TCP/FIX/SBE subsystem and its three parsers, becoming INTERFACE
  when the new `SLICK_SIM_ENABLE_TCP_GATEWAY` (default `ON`) is off — which also stops it linking `slick::socket`,
  now resolved behind that option rather than for every build, and drops QuickFIX from the required
  dependencies entirely. A core-only build needs none of uWebSockets, jwt-cpp, QuickFIX, slick-socket
  or slick-net: nothing in the core links a wire protocol.
- Venues resolve through a new `ExchangeRegistry` instead of a hard-coded `if/else` in `main.cpp`,
  which now names no venue. Each adapter self-registers via `SLICK_SIM_REGISTER_VENUE`, and config
  keys are matched case-insensitively. **An enabled `exchanges` key with no matching adapter is now a
  fatal startup error** naming the adapters the binary carries, instead of silently constructing an
  inert `Exchange`; `"enabled": false` is checked *before* the lookup, so a disabled block still needs
  no adapter.
- **`Exchange` subclasses override `poll()` rather than `start()`.** The spin loop moved into the base
  `Exchange::start()`, which calls `poll(); processRequest();` each tick; `poll()` defaults to a
  no-op. Both adapters' near-duplicate `start()` overrides were deleted, and the `md_feeds_` vector
  each declared separately is now a single base member.
- The `Venue` enum, `to_string` and `to_venue` are generated from one `SLICK_SIM_VENUE_LIST` X-macro.
  Enumerator values are unchanged. `to_venue` is genuinely case-insensitive now — it previously
  matched only the exact all-upper or all-lower spelling, so `"Coinbase"` resolved to `UNKNOWN_VENUE`
  despite the docs promising otherwise.

## Removed

- `src/market_data_publisher/market_data_publisher.cpp` — commented out of the build, with a ctor
  signature that no longer matched its header and a stale `switch(venue)` factory the registry replaces.
- `src/exchange/exch_cme.hpp` and `src/md_feed/alpaca_historical_data_feed.hpp` — empty placeholders.

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
