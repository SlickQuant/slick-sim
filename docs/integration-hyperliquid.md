# Integrating a Hyperliquid client

`slick-sim` exposes two local endpoints that speak Hyperliquid's own protocol:

| Endpoint | Sample port | Purpose |
| --- | --- | --- |
| REST order entry | 4002 | `POST /exchange`, `POST` and `GET /info` |
| WebSocket market data | 5001 | `l2Book`, `l2`, `trades`, `heartbeat` — and also `POST /info` |

There is no order-entry WebSocket, and therefore **no push channel for fills**. Order responses are
returned synchronously in the `/exchange` HTTP reply, or not at all.

## The `/info` proxy

Both ports serve `POST /info`, and neither answers it locally. Requests are relayed **verbatim** to
the real Hyperliquid API and the response is passed straight back
([`hyperliquid_info_proxy.hpp`](https://github.com/SlickTech/exchange_simulator/blob/main/src/common/hyperliquid_info_proxy.hpp)).

This exists so a `hyperliquid-cpp` client can point its `base_url` at either simulator port and still
have `load_meta()` and `canonical_coin()` return real production metadata — asset universe, tick
sizes, decimals. Without it, every client would have to be special-cased for the simulator.

Consequences worth planning around:

- **`/info` needs outbound internet access.** With no route to `api.hyperliquid.xyz`, metadata calls
  fail and clients cannot resolve coin names.
- **`/info` returns real market state**, not simulator state. `l2Book`, `openOrders`, `userFills` and
  friends describe the real exchange. Your simulated orders and fills appear nowhere in `/info`
  output.
- A transport failure (connection refused, DNS failure, timeout) is distinguished from a genuine
  upstream error by whether the body parses as JSON, and is reported as:

```json
{"status":"err","response":"failed to reach upstream Hyperliquid API"}
```

with HTTP `502 Bad Gateway`. Otherwise the upstream status code and body are passed through unchanged.

The upstream host comes from `order_gateway.base_url` on port 4002 and
`md_publisher.upstream_base_url` on port 5001 — [different key names for the same thing](configuration.md#md_publisher_1).

## Authentication

There is none, and no signature is verified. The wallet identity is resolved in priority order:

1. The `X-Wallet-Address` request header.
2. `vaultAddress` in the request body.
3. The configured `default_wallet` (`0x00…01` by default).

Whatever comes out is used as the internal `user_id`. Since nothing routes responses asynchronously,
its only real effect is on response correlation inside a single request — but if two clients share the
`default_wallet` and submit orders concurrently, their responses can be mixed up, because correlation
is on `user_id` + `client_order_id`.

## `POST /exchange`

The single order-entry route. The body must contain an `action` object with a `type`:

| `action.type` | Supported | Notes |
| --- | --- | --- |
| `order` | Yes | Places one or more orders, synchronously |
| `cancel` | Partly | Submitted fire-and-forget; see the `oid` problem below |
| `cancelByCloid` | Yes | Fire-and-forget, but the identifier actually resolves |
| `batchModify` | Partly | Fire-and-forget, same `oid` problem |

Anything else returns `400`:

```json
{"status":"err","response":"unsupported action type: usdSend"}
```

`nonce` and `signature` fields are accepted and completely ignored.

### Asset resolution

Orders identify their instrument by the `a` field. The gateway resolves it through metadata loaded at
startup:

```cpp
auto it = info_.name_to_coin.find("@" + std::to_string(asset_index));
```

If that lookup fails and `a` is a **string**, the string is used directly as the coin name. In
practice, passing the coin name (`"a": "ETH"`) is the reliable path; numeric indices only resolve for
assets that appear in the loaded `name_to_coin` map under the `@N` spot naming convention.

If neither resolves, that entry's status is `{"error": "unknown asset index N"}`.

The metadata load happens in the gateway's constructor via `info_.load_meta()`, which is a **live HTTP
call to `base_url` at startup**. Failure is logged as a warning and startup continues with an empty
asset map.

### `order`

```json
{
  "action": {
    "type": "order",
    "orders": [{
      "a": "ETH",
      "b": true,
      "p": "3000.5",
      "s": "1.0",
      "r": false,
      "t": { "limit": { "tif": "Gtc" } },
      "c": "my-cloid-1"
    }],
    "grouping": "na"
  },
  "nonce": 1723300000000,
  "signature": { }
}
```

| Field | Meaning | Handling |
| --- | --- | --- |
| `a` | Asset | Index or coin name, as above |
| `b` | Is buy | `true` → `BUY`, otherwise `SELL` |
| `p` | Limit price | Parsed with `std::stod` |
| `s` | Size | Parsed with `std::stod` |
| `r` | Reduce-only | **Parsed and discarded** |
| `t.limit.tif` | Time in force | See below |
| `t.trigger` | Trigger order | Downgraded to a plain limit order — trigger price, `isMarket` and `tpsl` are all ignored |
| `c` | Client order id | Optional; defaults to `hl_<nanoseconds>` |

Time-in-force mapping:

| Hyperliquid `tif` | Internal | Note |
| --- | --- | --- |
| `Gtc` | `GOOD_TILL_CANCEL` | Default when `t` is absent |
| `Ioc` | `IMMEDIATE_OR_CANCEL` | |
| `Alo` | `GOOD_TILL_CANCEL` | **Post-only semantics are lost.** `Alo` is mapped to plain GTC and `post_only` is never set, so an `Alo` order that crosses will trade instead of being rejected |

Each order in the array is submitted and then polled for individually, with a **5-second timeout per
order**. A batch of ten orders against a stalled engine takes 50 seconds.

**Response**

```json
{
  "status": "ok",
  "response": {
    "type": "order",
    "data": { "statuses": [ { "resting": { "oid": 12345 } } ] }
  }
}
```

Per-entry statuses are `{"resting": {"oid": N}}`, `{"error": "<reject reason>"}`, or
`{"error": "timeout"}`.

!!! danger "`oid` is derived from a UUID and is not usable"
    Internally an order's id is a UUID string. To fit Hyperliquid's numeric `oid` the gateway does
    `std::stoll(order_id)` on that UUID:

    - A UUID beginning with a digit yields only its leading digits — `"9f0c1a4e-…"` becomes `9`.
    - A UUID beginning with a hex letter (`a`–`f`, roughly 37% of them) makes `std::stoll` throw
      `std::invalid_argument`. The exception is caught by the outer handler and the **whole request**
      returns `400 {"status":"err","response":"parse error: stoll"}` — even for orders that were
      accepted and are now resting.

    The returned `oid` therefore cannot be used to cancel or modify: `cancel` stringifies it back and
    the engine looks it up against the full UUID, which never matches.

    **Use `cancelByCloid` with your own client order id.** It is the only identifier path that works
    end to end.

### `cancel`

```json
{ "action": { "type": "cancel", "cancels": [ { "a": "ETH", "o": 12345 } ] } }
```

Requests are published to the queue and the response is written immediately, without waiting:

```json
{ "status": "ok", "response": { "type": "cancel", "data": { "statuses": [ { "success": 12345 } ] } } }
```

`"success"` here means *submitted*, not *cancelled*. Combined with the `oid` problem above, this route
reports success while the engine rejects the cancel with `UNKNOWN_ORDER`.

### `cancelByCloid`

```json
{ "action": { "type": "cancelByCloid", "cancels": [ { "a": "ETH", "c": "my-cloid-1" } ] } }
```

Same fire-and-forget shape, but the `cloid` is written into `cancel_order.client_order_id`, which is
exactly what the engine looks up first. This route works. The response echoes the cloid:

```json
{ "status": "ok", "response": { "type": "cancel", "data": { "statuses": [ { "success": "my-cloid-1" } ] } } }
```

Still no confirmation that the cancel actually applied — a cancel for an unknown cloid reports
`success` too.

### `batchModify`

```json
{ "action": { "type": "batchModify",
              "modifies": [ { "oid": 12345,
                              "order": { "a": "ETH", "b": true, "p": "3010.0", "s": "2.0" } } ] } }
```

Fire-and-forget, keyed on the numeric `oid`, so it inherits the same identifier problem. Only `a`, `p`
and `s` are read from the nested `order`; `b`, `t`, `r` and `c` are ignored.

An amendment that makes the order marketable trades immediately, and a price change loses queue
priority — see [Matching engine](matching-engine.md#modifies-re-enter-the-matching-loop).

## Market-data WebSocket

Connect to `ws://localhost:5001/`. Messages follow Hyperliquid's subscription protocol:

```json
{ "method": "subscribe", "subscription": { "type": "l2Book", "coin": "ETH" } }
```

| `method` | Handling |
| --- | --- |
| `ping` | Replies `{"channel":"pong"}` |
| `subscribe` | Handled per `subscription.type` |
| `unsubscribe` | Accepted and **silently ignored** — no branch acts on it |
| anything else | `{"channel":"error", …}` |

| `subscription.type` | Coin field | Notes |
| --- | --- | --- |
| `l2Book` | `coin` | Full book snapshots |
| `l2` | **`c`** | Undocumented compressed-diff channel — note the different field name |
| `trades` | `coin` | Trade prints |
| `heartbeat` | — | Enables periodic heartbeats |
| anything else | — | `subscription type ... is not supported` |

### `l2Book`

First message after subscribing, and every routine update thereafter:

```json
{ "channel": "l2Book",
  "data": {
    "coin": "ETH",
    "time": 1723300000123,
    "levels": [
      [ {"px": "3000.500000", "sz": "12.000000", "n": 3} ],
      [ {"px": "3001.000000", "sz": "8.500000",  "n": 2} ]
    ]
  }}
```

`levels[0]` is bids, `levels[1]` is asks, both best-first. `time` is milliseconds. `n` is the order
count at that level, which includes any of your own resting orders.

Prices and sizes are formatted with `std::to_string`, giving fixed six-decimal output
(`"3000.500000"`) rather than Hyperliquid's variable precision.

### `l2` (compressed diffs)

Subscribing is acknowledged immediately, before any data:

```json
{ "channel": "subscriptionResponse",
  "data": { "method": "subscribe", "subscription": { "type": "l2", "c": "ETH" } } }
```

The first delivery is an uncompressed snapshot under key `s`:

```json
{ "channel": "l2", "data": { "s": { "coin": "ETH", "time": …, "levels": [ [...], [...] ] } } }
```

Subsequent updates are a raw-deflate, base64-encoded diff under key `c`:

```json
{ "channel": "l2", "data": { "c": "eJyrVkrLz1eyUkoqSlWq5QIAKN0EqQ==" } }
```

Decoding gives:

```json
{ "c": "ETH",
  "l": [ [ {"p":"3000.5","s":"12.0"} ], [ ] ],
  "r": [ [2], [] ],
  "t": 1723300000123 }
```

- `l` — changed or added levels with **absolute** new sizes, `[bids, asks]`.
- `r` — removed levels as **indices into the previous per-side ordered price array**.
- `t` — event time in milliseconds.

Decode with raw inflate (`-MAX_WBITS`, no zlib header) after base64. The diff baseline is per-symbol
and seeded from the snapshot, so a client must process the `s` message before any `c` message to stay
in sync.

### `trades`

The subscription snapshot is always empty:

```json
{ "channel": "trades", "data": [] }
```

Live prints:

```json
{ "channel": "trades",
  "data": [ { "coin": "ETH", "side": "B", "px": "3000.500000", "sz": "0.250000",
              "time": 1723300000123, "hash": "0x0" } ] }
```

`side` is `"B"` for buy, `"A"` for sell. `hash` is always the literal `"0x0"` — there is no chain.

### Subscription confirmation

Once every pending subscription for a socket has delivered its snapshot:

```json
{ "channel": "subscriptions", "data": { "subscriptions": ["ETH", "BTC"] } }
```

### Heartbeats

Every 15 seconds the publisher pings all sockets and, for those subscribed to `heartbeat`, sends a
heartbeat message. A socket that has not ponged for 60 seconds is closed. Neither interval is
configurable.

## End-to-end example

```bash
# 1. start the simulator (the logs/ directory must exist)
mkdir -p logs
slick-sim config/slick_sim.json

# 2. metadata comes from the real exchange through the proxy
curl -s -X POST http://localhost:4002/info \
  -H "Content-Type: application/json" \
  -d '{"type":"meta"}' | head -c 200

# 3. watch the book
websocat ws://localhost:5001/ <<'EOF'
{"method":"subscribe","subscription":{"type":"l2Book","coin":"ETH"}}
EOF

# 4. place a resting limit order, keyed by your own cloid
curl -s -X POST http://localhost:4002/exchange \
  -H "Content-Type: application/json" \
  -H "X-Wallet-Address: 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" \
  -d '{
        "action": {
          "type": "order",
          "orders": [{
            "a": "ETH", "b": true, "p": "1000.0", "s": "0.1",
            "t": { "limit": { "tif": "Gtc" } },
            "c": "demo-1"
          }],
          "grouping": "na"
        },
        "nonce": 1723300000000
      }'

# 5. cancel it by cloid — the only identifier path that reliably works
curl -s -X POST http://localhost:4002/exchange \
  -H "Content-Type: application/json" \
  -H "X-Wallet-Address: 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" \
  -d '{
        "action": { "type": "cancelByCloid",
                    "cancels": [ { "a": "ETH", "c": "demo-1" } ] },
        "nonce": 1723300000001
      }'
```

Unlike Coinbase, Hyperliquid can subscribe coins upstream at startup: any coin listed in
`md_feeds[].coins` has its `Symbol` created as soon as the first snapshot arrives, and can be traded
without a client-side market-data subscription. **The committed sample no longer lists any**, so as
written you must subscribe to a coin's market data before sending orders for it — otherwise they are
rejected with `UNKNOWN_CONTRACT`. Add a `coins` array to the feed entry to pre-subscribe, or run step
3 first.

Step 4 uses a deliberately far-from-market price so the order rests instead of filling — a marketable
order fills against real quoted liquidity, and with no push channel you would only see the result in
the HTTP response.
