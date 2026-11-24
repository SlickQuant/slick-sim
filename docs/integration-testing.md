# Integration tests

`tests/integration/` starts the shipped `slick-sim` executable and connects to the same ports as a
trading client. The simulator parses its config file and binds the gateway and publisher sockets.
Every order, fill and market-data message crosses a socket between processes.

The Coinbase suite runs without a live feed, internet access or credentials. It covers HTTP routing
and validation, the order-entry and market-data WebSocket protocols, the CLI port, and process
startup and shutdown, which the unit tests cannot exercise.

## What runs

Three trading clients, each a separate process running the real
[`coinbase-advanced-cpp`](https://github.com/SlickQuant/coinbase-advanced-cpp) SDK, plus the
simulator:

```text
slick_sim_integration_tests  (gtest)
├── slick-sim  config/coinbase_self_contained.json   REST, WS, MD and CLI ports
├── slick_sim_coinbase_client --user alice           SDK REST + WebSocket, JSON Lines on stdin/stdout
├── slick_sim_coinbase_client --user bob
└── slick_sim_coinbase_client --user carol
```

Each client needs its own process because the SDK caches `COINBASE_API_KEY` in a function-static
local. A process can sign as only one `sub`, and the simulator routes execution reports by that
`sub`. Three sockets in one process would therefore receive the same user's fills. Separate
processes also match how three clients connect to a real venue.

The suite starts the simulator once for the shared tests and starts a fresh instance for the tests
that need a different configuration (short heartbeat timers, self-match prevention) or that must stop
the process.

## What it covers

| Area | Assertions |
| --- | --- |
| Startup | Only Coinbase is instantiated; REST, WS, market-data and CLI ports listen; no live-feed capture directory; no outbound call to the real venue |
| Configuration | The checked-in template is the one under test, and it names no other venue and no `md_feeds` |
| REST products | Both product routes; a single product; an unknown product → `INVALID_ARGUMENT` |
| REST authentication | No header → 401; malformed JWT → 401; a decoded `sub` reaches the engine (the reject is `UNKNOWN_CONTRACT`, not 401) |
| REST validation | Unknown product, off-tick price, below-minimum size, unsupported `order_configuration`, malformed JSON; all rejected before queueing |
| Symbol lifecycle | An order for a product nobody subscribed to is rejected `UNKNOWN_CONTRACT`, on the HTTP response and on the user channel |
| FIFO matching | Two resting offers at one price fill in arrival order; a partial IOC remainder is cancelled; the completion percentage and fill count are correct |
| TIF | GTC rests; IOC cancels its remainder; FOK is rejected whole when it cannot fill; a market order sweeps two levels |
| Cancel and edit | Cancel by order id; an edit changes price and size and the order trades at the new price |
| Batch cancel | Four known ids and one unknown in one request, correct per-entry results, and a latency bound that the old per-order 200 ms poll could not meet |
| Malformed batch | A non-string id is rejected before anything is submitted, and the ids in the same request stay cancellable |
| Rejected amendment | An amendment the engine refuses with `SMP` leaves the original order working and still cancellable by id |
| Disconnect mid-request | A create sent over a raw socket that hangs up once the exchange accepts it, before its reply, still produces an order that can be cancelled by id, with no listing in between and with the response poll slowed to 1 s so the timer cannot mask the gap |
| Historical listing | `GET /orders/historical/batch` returns the caller's own orders with their statuses, one page, `has_next` false |
| User channel | Snapshot on subscribe, own reports only, `PENDING_NEW` suppressed, `OPEN` for a partial fill, `GOOD_UNTIL_CANCELLED` for GTC |
| Market data | `l2_data` snapshot and updates (including other clients' liquidity, and deletes when a level empties), `market_trades` with trade ids stable across clients, `ticker` snapshot |
| Sequences | A new socket's first market-data message is numbered zero, and no gap is reported |
| Heartbeats | A client survives several ping intervals and can still trade afterwards |
| CLI | `targets`, `log-level` get/set, `shutdown`; exit code 0; every port released |
| Self-match prevention | `cancel_newest` rejects the crossing order with `SMP` and leaves the resting order working |

Each test owns a product, so the suite does not depend on test order: BTC-USD is the FIFO and
market-data test, ETH-USD the partial-fill and edit test, SOL-USD the TIF and listing test, and
LTC-USD is never subscribed, which is what makes it the `UNKNOWN_CONTRACT` case.

### Defects the suite found

The first full runs found failures that the unit tests' direct calls had missed. The suite
reproduced them through the processes, SDK and wire formats. The following defects are fixed:

| Symptom | Cause |
| --- | --- |
| Startup aborted with no credentials, or threw inside the venue constructor | The WebSocket order gateway fetched the taker fee rate from the live API during construction. `order_gateway.ws.fee_rate` now skips the call and an unset secret skips it with a warning |
| A rejected order killed the process | `rejectNewOrderRequest` and its two siblings never initialized `error_message`, and the handler read the field as a C string; 256 bytes of recycled slot were copied into a `std::string` |
| Cancelling a partly filled order aborted the process | `sendOrderCancelPending` asserted `leaves_qty == qty`, which is false after any fill |
| A partial fill was reported as `FILLED`, with an absurd completion percentage | The user-channel status mapping ignored the leaves; the percentage mixed fixed-point and double quantities; the fill count was derived from quantities instead of the stream |
| A valid limit `FILL_OR_KILL` order was rejected before the engine | The REST gateway did not parse `limit_limit_fok`, the configuration the SDK sends |
| A `ticker` subscription was never confirmed | The exchange published no snapshot for the channel |
| The first message on a fresh socket reported a sequence gap | `sequence_num` was uninitialized instead of starting at zero |
| `GET /orders/historical/batch` stopped answering, and then shutdown hung | Cancel reports carried recycled `error_message` bytes; the listing put them into JSON, `dump()` threw on the invalid UTF-8 inside the uWS callback, and that gateway's only loop thread never ran again. The field is now cleared by every report builder, the store copies it with a bounded read, and the dumps replace invalid bytes instead of throwing |
| A five-order `batch_cancel` took a second, and shutdown could hang | Each cancel blocked the gateway's only loop thread for 200 ms, and a repeating timer kept the uWS loop with an active handle forever |

The last two failures appeared as client-side timeouts. Both required a full session with three
clients to reproduce, so the tests start and monitor the simulator process as well as its clients.

### Not covered, and why

- Live feeds require the venue and internet access. Feed sequencing has separate unit tests in
  `tests/unit/venues/coinbase/`.
- Hyperliquid, CME, ICE and PCAP replay are outside the Coinbase suite's scope. CME's out-of-process
  suite lives in the `slick-sim-cme` repository, in `tests/integration/`, and runs through the same
  `INTEGRATION_TESTS` hook; it is registered as `cme_integration` and `cme_regression` in CTest.
- The suite exercises shutdown through the portable CLI path, without testing SIGINT. The signal
  handler stores an atomic flag, and the CLI takes the same exit path without a signal.
- The SDK reads `reject_Reason` (capital `R`) but the wire field is `reject_reason`. It also discards
  non-2xx HTTP bodies. The suite checks reject reasons in raw HTTP responses and checks the
  user-channel *status*.
- The template uses heap-backed queues. Shared-memory queues use the separate `*_shm_name`
  configuration path.

## Running

```bash
ctest --test-dir build -R coinbase_integration --output-on-failure
```

The test is registered with the `integration` label, runs serially, and has a 900 s timeout. It is
built when the Coinbase adapter, the CLI (`SLICK_SIM_ENABLE_TCP_GATEWAY`) and GoogleTest are, and
can be switched off for a fast inner loop:

```bash
cmake -S . -B build -DSLICK_SIM_BUILD_INTEGRATION_TESTS=OFF
```

To run it directly, point it at the two executables (this is what CTest does):

```bash
build/tests/integration/slick_sim_integration_tests \
  --sim build/src/slick-sim \
  --client build/tests/integration/slick_sim_coinbase_client \
  --config config/coinbase_self_contained.json
```

## Debugging a failure

Every run creates its working directories under `<build>/.integration-coinbase/`:

| Path | Contents |
| --- | --- |
| `<run>/config.json` | The generated config, with the free ports the run picked |
| `<run>/logs/slick-sim.log` | The simulator's own log |
| `<run>/slick-sim.out`, `.err` | Its stdout and stderr |
| `<run>/alice.events.jsonl` | Everything alice's client observed, one JSON object per line |
| `<run>/alice.err` | The SDK's own logging from that client |

A failing assertion prints the relevant event stream and the client's stderr. The first place to
look for a startup failure is `slick-sim.err`: a venue that throws in its constructor never reaches
the log file.

## The client protocol

The test client `slick_sim_coinbase_client` reads one JSON command per line on stdin and writes one
JSON event per line on stdout. It is intended for the tests, rather than as a sample client:

```json
{"cmd":"md_subscribe","products":["BTC-USD"],"channels":["level2","market_trades","ticker"]}
{"cmd":"new","product":"BTC-USD","side":"BUY","type":"LIMIT","tif":"GTC","qty":0.001,"price":99.0,"coid":"a-1"}
{"cmd":"cancel","order_ids":["..."]}
{"cmd":"edit","order_id":"...","product":"BTC-USD","price":99.5,"size":0.002}
{"cmd":"orders","product":"BTC-USD","status":"OPEN","limit":50}
{"cmd":"bench","product":"BTC-USD","count":1000,"threads":2,"side":"BUY","tif":"IOC","qty":0.001,"price":100.0}
{"cmd":"quit"}
```

Events are `hello`, `ready`, `md_connected`/`user_connected`, `l2`, `trades`, `ticker`, `order`,
`user_snapshot`, `ack`, `gap`, `error` and `bye`. Every order event carries the order's state as the
SDK parsed it, plus `latency_ms` on the first terminal report.

The `bench` command submits `count` orders over `threads` blocking REST calls and reports round-trip
percentiles and throughput; it is what the performance work in the CHANGELOG was measured with.

A baseline, three client processes each benching 800 orders with 3 threads (2,400 orders total,
crossing IOC orders so every one trades), Debug build, simulator on the same machine:

| Client | orders/s | p50 | p90 | p99 | max |
| --- | --- | --- | --- | --- | --- |
| alice | 150 | 13.4 ms | 38.1 ms | 49.3 ms | 271 ms |
| bob | 100 | 27.9 ms | 38.3 ms | 50.6 ms | 57 ms |
| carol | 104 | 27.4 ms | 38.4 ms | 50.0 ms | 56 ms |

No request failed. A single client's p50 is about half the other two because it starts a second
earlier and is the one that had to create the symbol; the long tail is the first burst. These are
round-trip times of blocking HTTP calls from three processes, so they include the SDK's own
serialization and the loop's timer poll (`order_gateway.rest.response_poll_ms`).
