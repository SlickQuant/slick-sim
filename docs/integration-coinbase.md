# Integrating a Coinbase client

`slick-sim` exposes three local endpoints that speak Coinbase Advanced Trade's own protocols — two for
**order entry** (submitting orders and receiving what happens to them) and one for **market data**
(streaming prices and trades):

| Endpoint | Sample port | Direction | Purpose |
| --- | --- | --- | --- |
| REST order entry | 4000 | client → sim | Products, and create/cancel/edit orders |
| WebSocket order entry | 4001 | sim → client | `user` channel — acks, fills, cancels |
| WebSocket market data | 5000 | sim → client | `level2`, `market_trades`, `ticker`, `heartbeats` |

The two order-entry endpoints are **not** alternatives — they are the two halves of one round trip.
You send on REST and hear back on the WebSocket.

Point your client's REST base URL and WebSocket URLs at these and it should work unchanged, subject to
the gaps noted throughout.

!!! important "Use both order-entry endpoints"
    The REST route returns only the `PENDING_NEW` acknowledgement. Fills, cancels and rejects arrive
    **exclusively** over the WebSocket `user` channel. A REST-only client will never learn that its
    order traded.

## Authentication

Both order-entry endpoints require a JWT, but **neither verifies its signature**.

REST expects an `Authorization: Bearer <jwt>` header. The gateway decodes the token, reads the `sub`
claim, and takes everything after the last `/` as the `user_id`:

```cpp
auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);
std::string sub = decoded.get_subject();
std::string user_id = sub.substr(sub.rfind('/') + 1);
```

Signature verification is commented out in the WebSocket gateway and never attempted in REST. Any
syntactically valid JWT is accepted; a malformed one gets `401 Unauthorized` (REST) or an error
message and a socket close (WebSocket).

The `user_id` matters: it is how responses are routed back. The WebSocket gateway keys its client map
by `user_id`, so **the REST and WebSocket connections must present tokens with the same `sub`** or
your fills will be published to nobody.

Market data requires no authentication.

## REST order entry

Nine routes are implemented. Everything else 404s.

### `GET /api/v3/brokerage/market/products` and `GET /api/v3/brokerage/products`

Both return the product list verbatim — either the `initial_products` string from your config, or
whatever was fetched from the real Coinbase API at startup. No authentication, no filtering, no query
parameter support.

```bash
curl http://localhost:4000/api/v3/brokerage/market/products
```

### `GET /api/v3/brokerage/products/{product_id}` and `GET /api/v3/brokerage/market/products/{product_id}`

Returns the single cached product object, or `400 Bad Request`:

```json
{"error":"INVALID_ARGUMENT","error_details":"valid product_id is required","message":"valid product_id is required"}
```

### `POST /api/v3/brokerage/orders`

Creates an order. Requires authentication.

**Request**

```json
{
  "product_id": "BTC-USD",
  "side": "BUY",
  "client_order_id": "my-order-1",
  "order_configuration": {
    "limit_limit_gtc": {
      "base_size": "0.01",
      "limit_price": "60000.00",
      "post_only": false
    }
  }
}
```

`side` is compared against the literal `"BUY"`; **anything else is treated as `SELL`**, including a
typo.

Four `order_configuration` variants are recognised:

| Variant | `OrderType` | `TimeInForce` | Fields read |
| --- | --- | --- | --- |
| `limit_limit_gtc` | `LIMIT` | `GOOD_TILL_CANCEL` | `base_size`, `limit_price`, `post_only` |
| `market_market_ioc` | `MARKET` | `IMMEDIATE_OR_CANCEL` | `base_size` |
| `sor_limit_ioc` | `LIMIT` | `IMMEDIATE_OR_CANCEL` | `base_size`, `limit_price` |
| `market_market_fok` | `LIMIT` if `limit_price` present, else `MARKET` | `FILL_OR_KILL` | `base_size`, optional `limit_price` |

Anything else returns `400` with `"Unsupported order_configuration type"`. Notably absent:
`limit_limit_gtd`, `stop_limit_stop_limit_gtc`, `trigger_bracket_gtc`, and quote-denominated sizing
(`quote_size`).

`post_only` is parsed and echoed back but [never enforced](matching-engine.md#what-the-engine-does-not-do).

**Validation.** Before queueing, the gateway checks the order against the cached product spec:

| Check | Error type |
| --- | --- |
| Product not in the cached list | `INVALID_PRODUCT_ID` |
| Non-market price not a multiple of `price_increment` | `INVALID_LIMIT_PRICE_POST_ONLY` |
| Size below `base_min_size` | `INVALID_ORDER_CONFIG` |
| Size not a multiple of `base_increment` | `INVALID_ORDER_CONFIG` |

An empty product cache therefore fails **every** order with `INVALID_PRODUCT_ID` — see
[`initial_products`](configuration.md#order_gateway).

**Success response** (HTTP 200)

```json
{
  "success": true,
  "success_response": {
    "order_id": "9f0c1a4e-…",
    "product_id": "BTC-USD",
    "side": "BUY",
    "client_order_id": "my-order-1"
  },
  "order_configuration": { "limit_limit_gtc": { … } }
}
```

The `order_id` is the simulator's UUID; keep it for cancel and edit.

**Rejection response** (HTTP 400)

```json
{
  "success": false,
  "error_response": {
    "message": "CLIENT_ORDER_ID_ALREADY_EXISTS",
    "error_details": "CLIENT_ORDER_ID_ALREADY_EXISTS",
    "new_order_failure_reason": "CLIENT_ORDER_ID_ALREADY_EXISTS"
  }
}
```

The strings are `slick-sim`'s [`OrdRejectReason`](order-lifecycle.md#ordrejectreason) names, not
Coinbase's failure-reason enum, so a client that switches on Coinbase's documented values will not
recognise them.

**Timeout** (HTTP 504) after 5 s with no matching response:

```json
{"error":"TIMEOUT","message":"Order processing timeout"}
```

### `POST /api/v3/brokerage/orders/batch_cancel`

```json
{ "order_ids": ["9f0c1a4e-…", "3b7d2f10-…"] }
```

Cancels are submitted one at a time, each followed by a 200 ms wait for a rejection. No rejection
within the window is reported as success.

```json
{ "results": [
    { "success": true,  "failure_reason": "",              "order_id": "9f0c1a4e-…" },
    { "success": false, "failure_reason": "UNKNOWN_ORDER", "order_id": "3b7d2f10-…" }
]}
```

`UNKNOWN_ORDER` with `submitted = false` means the gateway has no record of that `order_id` at all —
it only tracks orders created through *this* gateway instance, in an in-memory
`order_id_to_symbol_` map that is not shared with the WebSocket gateway and does not survive a restart.

Serialised cancels mean a batch of 20 unknown-to-the-engine orders can take up to 4 seconds.

### `POST /api/v3/brokerage/orders/edit`

```json
{ "order_id": "9f0c1a4e-…", "price": "60500.00", "size": "0.02" }
```

Both `price` and `size` are optional but at least one should be present; the modified values are
validated against the product spec exactly as on create.

```json
{ "success": true,  "errors": [] }
{ "success": false, "errors": [ { "edit_failure_reason": "UNKNOWN_ORDER" } ] }
```

An edit that makes the order marketable will trade immediately — see
[Matching engine](matching-engine.md#modifies-re-enter-the-matching-loop). Changing the price loses
queue priority; changing only the size keeps it.

### `GET /api/v3/brokerage/orders/historical/batch`

Authenticates, then **returns nothing**. The handler builds an empty `{"orders": []}` object and never
writes it to the response — the HTTP request hangs until your client times out. Treat this route as
unimplemented.

### `GET /api/v3/brokerage/transaction_summary`

Proxied straight to the **real** `https://api.coinbase.com` (hard-coded) using a JWT the simulator
generates itself, and relayed back verbatim. This requires working Coinbase API credentials in the
environment `coinbase::generate_coinbase_jwt` reads.

## Order-entry WebSocket

Connect to `ws://localhost:4001/`. Every message is a subscribe request:

```json
{ "type": "subscribe", "channel": "user", "jwt": "<token>" }
```

`type` must be `"subscribe"`; anything else returns
``{"type":"error","message":"type `…` is not supported"}``.
A missing `jwt` field returns an error. There is no unsubscribe.

| Channel | Behaviour |
| --- | --- |
| `user` | Registers the socket for execution reports. Immediately sends an order snapshot then a `subscriptions` confirmation |
| `heartbeats` | Enables periodic heartbeat messages; confirms via `subscriptions` |
| `futures_balance_summary` | Returns a **hard-coded static balance snapshot** — the same fabricated numbers for every user |
| anything else | ``{"type":"error","message":"channel `…` is not supported"}`` |

### Execution reports

Every `OrderResponse` other than `PENDING_NEW` is pushed to the socket registered for that `user_id`:

```json
{
  "channel": "user",
  "client_id": "",
  "timestamp": "2026-08-11T12:34:56.789012345Z",
  "sequence_num": 7,
  "events": [{
    "type": "update",
    "orders": [{
      "avg_price": "60000.000000",
      "client_order_id": "my-order-1",
      "completion_percentage": "100.000000",
      "cumulative_quantity": "0.010000",
      "filled_value": "600.000000",
      "leaves_quantity": "0.000000",
      "limit_price": "60000.000000",
      "number_of_fills": "1",
      "order_id": "9f0c1a4e-…",
      "order_side": "BUY",
      "order_type": "LIMIT",
      "post_only": "false",
      "product_id": "BTC-USD",
      "reject_reason": "NONE",
      "status": "FILLED",
      "time_in_force": "GOOD_TILL_CANCEL",
      "total_fees": "0.720000",
      "total_value_after_fees": "599.280000",
      "trigger_status": "INVALID_ORDER_TYPE",
      "creation_time": "…",
      "end_time": "0001-01-01T00:00:00Z",
      "start_time": "0001-01-01T00:00:00Z"
    }]
  }]
}
```

Differences from the real Coinbase feed worth planning around:

- `PENDING_NEW` reports are filtered out, so the first thing you see for an order is its `NEW` ack.
- Numeric fields use `std::to_string`, which gives fixed six-decimal formatting (`"60000.000000"`),
  not Coinbase's variable precision.
- `total_fees` is display-only — no fee is ever applied to the order. The rate comes from a single
  unguarded `CoinbaseRestClient::get_taker_fee_rate()` call in the gateway constructor, which
  **unconditionally overwrites** the `fee_rate_ = 0.0012` member initialiser — so that value is a
  declaration default, not a fallback, and whatever the call returns on failure is what you get.
  (The source comment beside it reads "1.2%"; 0.0012 is 0.12%.)
- `completion_percentage` divides by the raw fixed-point `qty`, not the scaled value, so the number is
  wrong by a factor of 1e8.
- `number_of_fills` is derived as `cum_qty / last_qty` rather than the tracked `num_fills`.

### Heartbeats

Every 30 s (`ping_interval`) the gateway sends a WebSocket PING to all clients and, to those
subscribed to `heartbeats`, a message:

```json
{ "channel": "heartbeats", "client_id": "", "timestamp": "…", "sequence_num": 12,
  "events": [{ "current_time": "…", "heartbeat_counter": 4 }] }
```

A client that does not PONG within `pong_timeout` (60 s) is disconnected.

## Market-data WebSocket

Connect to `ws://localhost:5000/`. No authentication.

```json
{ "type": "subscribe", "channel": "level2", "product_ids": ["BTC-USD"] }
```

| Channel | Status |
| --- | --- |
| `level2` | Works — snapshot then incremental updates on `l2_data` |
| `market_trades` | Works — recent-trades snapshot then an `"update"` per executed trade |
| `ticker` | Subscribes and confirms; only an empty snapshot is ever sent |
| `heartbeats` | Works |
| anything else | ``{"type":"error","message":"channel `…` is not supported"}`` |

Subscribing to a symbol for the first time creates it inside the simulator and opens an upstream
Coinbase feed for it, so the first snapshot arrives only once real data has been received.

### `l2_data`

Snapshot:

```json
{ "channel": "l2_data", "client_id": "", "timestamp": "…", "sequence_num": 1,
  "events": [{
    "type": "snapshot",
    "product_id": "BTC-USD",
    "updates": [
      { "side": "bid",   "price_level": "60000.000000", "new_quantity": "1.500000" },
      { "side": "offer", "price_level": "60001.000000", "new_quantity": "0.800000" }
    ]
  }] }
```

Incremental update — same shape with `"type": "update"`, and each entry carries an extra `event_time`:

```json
{ "side": "bid", "price_level": "60000.000000", "new_quantity": "2.000000",
  "event_time": "2026-08-11T12:34:56.789012345Z" }
```

Bids come first in a snapshot, then offers. `new_quantity` is the absolute new level quantity, and it
**includes your own resting orders** at that price — the book is shared, so your liquidity is visible
in the market data you receive.

!!! note "One-second latency"
    Coinbase level updates are held in a time-ordering buffer for at least one second before being
    applied and republished. See
    [Market data](market-data.md#coinbase-time-ordered-event-sequencing). Snapshots bypass this.

### `market_trades`

Snapshot of the instrument's recent trades, newest first, as the venue orders them:

```json
{ "channel": "market_trades", "client_id": "", "timestamp": "…", "sequence_num": 1,
  "events": [{
    "type": "snapshot",
    "trades": [
      { "trade_id": "7", "product_id": "BTC-USD", "price": "60000.000000",
        "size": "0.250000", "side": "BUY", "time": "…" }
    ]
  }] }
```

One `"update"` event per executed trade:

```json
{ "channel": "market_trades", "client_id": "", "timestamp": "…", "sequence_num": 8,
  "events": [{
    "type": "update",
    "trades": [
      { "trade_id": "8", "product_id": "BTC-USD", "price": "60001.000000",
        "size": "0.100000", "side": "SELL", "time": "…" }
    ]
  }] }
```

`side` is the **aggressor's** side. `price`, `size` and `trade_id` are strings; per-trade `time` is
microsecond-precision ISO 8601, while the envelope `timestamp` is nanosecond precision.

`trade_id` is a per-venue sequence generated by the simulator, so it is stable across clients: two
clients see the same id for the same trade, whether it arrives live or is replayed in a snapshot.
When shadowing a live feed, ids seeded from the upstream trades snapshot keep Coinbase's own values
and the simulator continues numbering from above the highest one seen.

!!! note "Trades are matched, not relayed"
    Every message on this channel is a fill produced by the matching engine. An upstream Coinbase
    print is replayed into the book as an aggressor order rather than forwarded, so what a client
    sees here reflects the simulated book — including fills against its own resting orders. A print
    that finds no crossable liquidity produces
    [no message at all](known-gaps.md#a-trade-print-only-reaches-the-tape-if-it-finds-liquidity).

If the recent-trade history contains a trade stamped **after** the book snapshot the subscriber
receives, that trade is not settled history for this client — it arrives in a second `"update"`
message immediately after the snapshot, before the subscription confirmation.

### Subscription confirmation

After the first snapshot for each pending symbol:

```json
{ "channel": "subscriptions", "client_id": "", "timestamp": "…", "sequence_num": 2,
  "events": [{ "subscriptions": { "level2": ["BTC-USD"], "market_trades": ["BTC-USD"] } }] }
```

All of the client's active subscriptions are listed, not just the one that just completed.

### `sequence_num`

Per-socket and monotonically increasing across every message the publisher sends to that socket,
including confirmations and heartbeats. It is not the exchange sequence number and does not
correspond to anything upstream.

## End-to-end example

```bash
# 1. start the simulator (the logs/ directory must exist)
mkdir -p logs
slick-sim config/slick_sim.json

# 2. confirm products loaded
curl -s http://localhost:4000/api/v3/brokerage/market/products | head -c 200

# 3. subscribe to market data (creates the symbol and opens the upstream feed)
websocat ws://localhost:5000/ <<'EOF'
{"type":"subscribe","channel":"level2","product_ids":["BTC-USD"]}
EOF

# 4. in another terminal, subscribe to the user channel so you can see fills
websocat ws://localhost:4001/ <<'EOF'
{"type":"subscribe","channel":"user","jwt":"<your-jwt>"}
EOF

# 5. send a marketable order
curl -s -X POST http://localhost:4000/api/v3/brokerage/orders \
  -H "Authorization: Bearer <your-jwt>" \
  -H "Content-Type: application/json" \
  -d '{
        "product_id": "BTC-USD",
        "side": "BUY",
        "client_order_id": "demo-1",
        "order_configuration": {
          "market_market_ioc": { "base_size": "0.001" }
        }
      }'
```

Step 3 is not optional: an order for a product nobody has subscribed to is rejected with
`UNKNOWN_CONTRACT`, because the `Symbol` does not exist until a market-data subscription creates it.
