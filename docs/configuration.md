# Configuration

`slick-sim` takes a single JSON configuration file:

```bash
mkdir -p logs                       # required: startup aborts without it
slick-sim config/slick_sim.json
```

!!! warning "Always pass the path explicitly"
    With no argument the binary looks for **`slick-sim.json`** in the current working directory. The
    sample committed to the repository is **`config/slick_sim.json`** — different directory, and an
    underscore rather than a hyphen. The default will not find it.

This page documents every key the code actually reads, with the source that reads it. Anything not
listed here is ignored.

## Top level

| Key | Type | Default | Required |
| --- | --- | --- | --- |
| `log_level` | string | `"info"` | No |
| `exchanges` | object | — | **Yes** |

`log_level` accepts `trace`, `debug`, `info`, `warn`, `error`, `fatal` (parsed by
`slick::logger::to_log_level`). An unrecognised value falls back to the logger's default rather than
erroring.

`exchanges` maps a venue key to that venue's configuration. A missing `exchanges` object makes the
process print `Missing exchanges config` and exit with `EXIT_FAILURE`.

Recognised venue keys are **`coinbase`** and **`hyperliquid`**, matched case-insensitively. A key is
resolved through the [venue registry](extending.md#6-register-the-venue), so it is recognised only if
that adapter was compiled in — see the `SLICK_SIM_ENABLE_*` options in
[Adding an exchange](extending.md#7-wire-up-the-build).

An **enabled** key with no matching adapter is a fatal startup error naming the adapters the binary
does carry. A block that is `"enabled": false` is skipped before the lookup, so it needs no adapter —
which is why the committed sample's disabled `cme` entry still works.

## Per-exchange keys

Read by the [`Exchange` base constructor](https://github.com/SlickQuant/slick-sim/blob/main/src/exchange/exchange.cpp#L17-L32)
and `main.cpp`, so they apply to every venue.

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `enabled` | bool | `true` | When false the venue is not started. `coinbase` and `hyperliquid` are skipped before construction by `main.cpp`; every other key is constructed but returns early from `start()` with a `... is disabled, skipping start` warning |
| `request_queue_size` | int | `1048576` | Capacity of the gateway → exchange queue |
| `response_queue_size` | int | `1048576` | Capacity of the exchange → gateway queue |
| `md_queue_size` | int | `16777216` | Capacity of the exchange → publisher byte queue. Coinbase also uses this to size its WebSocket stream multiplexer |
| `request_queue_shm_name` | string | *(none)* | Back `request_queue_` with a named shared-memory segment |
| `response_queue_shm_name` | string | *(none)* | Same for `response_queue_` |
| `md_queue_shm_name` | string | *(none)* | Same for `md_queue_` |
| `self_match_prevention` | string | `"none"` | Stops a client trading against its own resting orders. See below |
| `md_feeds` | array | *(none)* | Market-data feeds to subscribe to upstream. **Optional** — omitting it runs the venue as a self-contained simulator; see [Operating modes](architecture.md#operating-modes) |
| `order_gateway` | object | — | **Required** — the constructor throws if absent |
| `md_publisher` | object | — | **Required** — the constructor throws if absent |

### `self_match_prevention`

| Value | Behaviour when an order would cross its owner's own resting order |
| --- | --- |
| `"none"` *(default)* | Allow the match |
| `"cancel_resting"` | Cancel the resting order and carry on matching against the next one |
| `"cancel_newest"` | Reject the incoming order with `OrdRejectReason::SMP`, no trades |

The mode applies to every symbol the venue creates, and to amendments as well as new orders.
"Self" means the same **authenticated `user_id`**, so an order carrying no `user_id` is exempt —
including against another order with no `user_id`. An unrecognised value logs a warning and leaves
SMP disabled rather than aborting startup. See
[Matching engine](matching-engine.md#self-match-prevention).

The three `*_shm_name` keys appear in no sample config and are not otherwise exercised. Note that a
shared-memory reader must be built with the same `int_fast64_t` width — see
[Market data](market-data.md#the-internal-market-data-wire-format).

!!! danger "A missing `order_gateway` or `md_publisher` aborts the process"
    The constructor throws `std::runtime_error` and nothing in `main` catches it.

### `md_feeds[]`

| Key | Type | Default | Applies to |
| --- | --- | --- | --- |
| `type` | string | `""` | Both |
| `base_url` | string | `"https://api.hyperliquid.xyz"` | Hyperliquid only |
| `coins` | string array | *(empty)* | Hyperliquid only |

Only two `type` values do anything: `"coinbase_live_ws"` and `"hyperliquid_live_ws"`. Any other value
is silently ignored.

An exchange with no usable feed — `md_feeds` omitted, empty, or containing only unrecognised types —
runs in **self-contained mode**: books start empty and the only liquidity is what clients submit.
That is a supported configuration, not a broken one; it turns `slick-sim` into a conventional exchange
simulator with the same order-entry and market-data surface. See
[Operating modes](architecture.md#operating-modes).

To disable a live feed without deleting the block, change its `type` to something the adapter does not
recognise — there is no `enabled` flag on a feed entry.

The two venues treat feeds differently:

- **Coinbase** — the entry only sets an internal `use_live_feed_` flag. Feed objects are created
  lazily, one per symbol, when a client first subscribes. There is no symbol list in the config.
- **Hyperliquid** — the feed object is built in the constructor from `coins`, and those coins are
  subscribed upstream at startup. `base_url` is the upstream WebSocket/REST host. Additional coins are
  added on demand when clients subscribe.

## Coinbase

```json
"coinbase": {
    "enabled": true,
    "order_gateway": {
        "rest": { "port": 4000 },
        "ws":   { "port": 4001 }
    },
    "md_publisher": { "port": 5000 },
    "md_feeds": [ { "type": "coinbase_live_ws" } ]
}
```

### `order_gateway`

`rest` is **required** — `CoinbaseExchange`'s constructor throws
`"COINBASE Exchange order_gateway missing rest config"` without it. `ws` is optional; omitting it
means no order-entry WebSocket, and therefore no fills or execution reports reach the client at all
(the REST route only ever returns the `PENDING_NEW` acknowledgement).

**`order_gateway.rest`**

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `port` | int | `3000` | HTTP listen port |
| `initial_products` | string | *(none)* | A raw JSON string served verbatim from the products routes. Use this to avoid an outbound call at startup |
| `base_url` | string | `"https://api.coinbase.com"` | Only used when `initial_products` is absent: the gateway fetches `/api/v3/brokerage/market/products` from here at construction time |

Without `initial_products`, **`slick-sim` makes a live HTTP call to Coinbase during construction**.
If it fails, a warning is logged and the product list stays empty — which then causes every order to
fail validation with `INVALID_PRODUCT_ID`.

**`order_gateway.ws`**

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `port` | int | `3001` | WebSocket listen port |
| `ping_interval` | int (seconds) | `30` | Heartbeat timer period; multiplied by 1000 internally |
| `pong_timeout` | int (seconds) | `60` | Disconnect a client that has not ponged within this window |

### `md_publisher`

| Key | Type | Default |
| --- | --- | --- |
| `port` | int | `5000` |

The publisher's own ping interval (15 s) and pong timeout (60 s) are hard-coded in
`WebsocketMarketDataPublisher` and not configurable.

## Hyperliquid

```json
"hyperliquid": {
    "enabled": true,
    "order_gateway": {
        "port": 4002,
        "base_url": "https://api.hyperliquid.xyz",
        "default_wallet": "0x0000000000000000000000000000000000000001"
    },
    "md_publisher": {
        "port": 5001,
        "upstream_base_url": "https://api.hyperliquid.xyz"
    },
    "md_feeds": [
        { "type": "hyperliquid_live_ws",
          "base_url": "https://api.hyperliquid.xyz",
          "coins": ["ETH", "BTC"] }
    ]
}
```

### `order_gateway`

A single object — no `rest`/`ws` split.

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `port` | int | `4003` | HTTP listen port serving `/exchange` and `/info`. The committed sample overrides it to **4002** |
| `base_url` | string | `"https://api.hyperliquid.xyz"` | Upstream host that `POST /info` requests are proxied to |
| `default_wallet` | string | `"0x00…01"` | Wallet address used as the user identity when a request carries none |

### `md_publisher`

| Key | Type | Default | Notes |
| --- | --- | --- | --- |
| `port` | int | `5001` | WebSocket listen port; also serves `POST /info` |
| `upstream_base_url` | string | `"https://api.hyperliquid.xyz"` | Upstream host for the `/info` proxy |

!!! note "`upstream_base_url`, not `base_url`"
    The publisher uses a different key name from the order gateway for the same concept. Setting
    `base_url` under `md_publisher` has no effect.

## Complete annotated sample

The committed sample with every optional key spelled out. It also carries a `cme` block, omitted here:
it is `"enabled": false`, which is what lets it sit in the config with no adapter behind it.

```json
{
    "log_level": "debug",

    "exchanges": {
        "coinbase": {
            "enabled": true,

            "request_queue_size":  16777216,
            "response_queue_size": 16777216,
            "md_queue_size":       16777216,

            "md_feeds": [
                { "type": "coinbase_live_ws" }
            ],

            "order_gateway": {
                "rest": {
                    "port": 4000,
                    "base_url": "https://api.coinbase.com"
                },
                "ws": {
                    "port": 4001,
                    "ping_interval": 30,
                    "pong_timeout": 60
                }
            },

            "md_publisher": { "port": 5000 }
        },

        "hyperliquid": {
            "enabled": true,

            "md_feeds": [
                {
                    "type": "hyperliquid_live_ws",
                    "base_url": "https://api.hyperliquid.xyz",
                    "coins": ["ETH", "BTC"]
                }
            ],

            "order_gateway": {
                "port": 4002,
                "base_url": "https://api.hyperliquid.xyz",
                "default_wallet": "0x0000000000000000000000000000000000000001"
            },

            "md_publisher": {
                "port": 5001,
                "upstream_base_url": "https://api.hyperliquid.xyz"
            }
        }
    }
}
```

## Ports at a glance

Defaults from the code differ from the sample; the sample's values are what you get if you use it
unchanged.

| Endpoint | Code default | Sample |
| --- | --- | --- |
| Coinbase REST order entry | 3000 | 4000 |
| Coinbase WebSocket order entry | 3001 | 4001 |
| Coinbase market data | 5000 | 5000 |
| Hyperliquid REST order entry (`/exchange`, `/info`) | 4003 | **4002** |
| Hyperliquid market data (+ `/info`) | 5001 | 5001 |

The sample's disabled `cme` block reserves 4003 for its order gateway and 5002 for its market data
publisher, so nothing collides if you enable it once a CME adapter exists.

## Outbound network calls

Worth knowing before running this in a restricted environment. `slick-sim` reaches the public internet
in four places:

1. **Coinbase market data** — WebSocket, whenever a `coinbase_live_ws` feed is configured and a client
   subscribes.
2. **Coinbase product list** — one HTTPS GET at startup, unless `initial_products` is set.
3. **Coinbase taker fee rate** — the WebSocket order gateway calls
   `CoinbaseRestClient::get_taker_fee_rate()` in its constructor, an authenticated call to the real
   Coinbase API. Used only to compute the display-only `total_fees` field in execution reports.
4. **Coinbase transaction summary** — `GET /api/v3/brokerage/transaction_summary` is proxied straight
   through to `https://api.coinbase.com` (hard-coded, not configurable) with a freshly generated JWT.
5. **Hyperliquid `/info`** — every client `/info` request is relayed to `base_url` /
   `upstream_base_url`, so `hyperliquid-cpp` clients get real production metadata. Additionally, the
   Hyperliquid order gateway calls `info_.load_meta()` in its constructor to build the asset map.

Hyperliquid market data likewise runs over a live WebSocket to `base_url`.

## Logging

Not configurable beyond `log_level`. Destinations are fixed in `main.cpp`:

| Destination | When |
| --- | --- |
| `logs/slick-sim.log` | Always — and the `logs/` directory must already exist, or startup aborts |
| Console | Debug builds only (`#ifdef DEBUG`) |
| `logs/coinbase_data/` | Whenever a Coinbase feed is active — a raw capture of the upstream WebSocket stream |

The logger is initialised with a 65,535-entry queue and a 16 MiB buffer.
