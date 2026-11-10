# PCAP replay

A venue can run its market data from recorded multicast captures instead of live sockets. CME
(`cme_mdp3_pcap`) and ICE (`ice_impact_pcap`) do. The captures go through the same handlers the live
feeds use, so the book, the phantom orders, the matching engine and the published multicast are the
ones described elsewhere; only the source of the packets differs.

Nothing is replayed until a client says start. The sim loads the captures, builds the book, then holds
still and tells its clients so over a [control channel](#control-channel). A `START` from a client then
replays every instrument the captures hold.

## States

```text
IDLE → LOADING → SYNCING → READY → REPLAYING → FINISHED          (ERROR from any working state)
```

| State | What the sim is doing |
|---|---|
| `LOADING` | Opening the capture files and reading each one up to the window's `begin`. A `.zst` or `.gz` stream cannot be seeked, so this is decompression at roughly the speed of `zstd -d`. |
| `SYNCING` | Feeding the venue handler at full speed, unpaced, until it reports the book built. For CME and ICE that is the first complete pass of the snapshot capture after `begin`, with the incremental packets that arrived meanwhile applied on top. No market data is published. |
| `READY` | The book is held at the sync point. The capture is not read any further. |
| `REPLAYING` | A client's `START` begins it. The publisher opens and sends every instrument, from the held book on, and packets are released at the capture's own pace (see [speed](#speed)). |
| `FINISHED` | The window is replayed out. |
| `ERROR` | A file was missing or unreadable, or the capture ended before the book was built. `message` says which. |

Once started, the replay runs to `FINISHED` whether or not any client stays connected.

## Configuration

An entry of `md_feeds[]`. The groups use the same `{ "address", "port" }` keys as the live feed, so
one config can be flipped between live and recorded.

```json
{
  "type": "cme_mdp3_pcap",
  "pcap_root": "/data/clusterFS/firm/data/pcap/tradedaily/exchange",
  "begin": "2026-11-06T00:30:00Z",
  "end":   "2026-11-06T01:30:00Z",
  "speed": 1.0,
  "control_channel": { "port": 17310 },
  "incremental_a": { "address": "224.0.31.1",   "port": 14310 },
  "snapshot":      { "address": "233.72.75.1",  "port": 23310 }
}
```

| Key | Default | Meaning |
|---|---|---|
| `type` | — | `cme_mdp3_pcap` or `ice_impact_pcap` |
| `begin`, `end` | required | The window, UTC ISO 8601 (`2026-11-06T14:00:00Z`, fractions allowed) or integer nanoseconds since the epoch. `end` must be after `begin`. |
| `pcap_root` | — | Directory of captures. Needed unless every group names its own file. |
| `speed` | `1.0` | `1.0` replays at the capture's own pace, `N` is `N` times faster, `0` sends as fast as the sim can. |
| `control_channel.port` | required | TCP port of the [control channel](#control-channel). `0` asks the OS for one. |
| `control_channel.max_connections` | `100` | Most clients the control channel accepts at once. |
| groups | — | CME: `incremental_a` and/or `incremental_b`, and `snapshot` (the **MBO** snapshot). ICE: `live` and `snapshot`. Each may add `"path"` (or `"paths"`, which must be listed oldest first: a packet at or before the previous file's last timestamp is dropped as overlap) to name capture files directly. |
| handler keys | — | The same `max_out_of_order`, `gap_timeout_us`, `max_recovery_packets` (and ICE's `include_implied`) as the live feed. |

A segment or silo has at most one pcap feed, and cannot mix it with a live feed. A pcap feed may name
the groups the sim itself publishes on, since it reads files, not the network.

### Where the captures are

Under `pcap_root` a group's file is `<YYYY-MM-DD>/<address>:<port>/<address>:<port>.pcap.zst`
(`.pcap.gz` and `.pcap` are found too): one file per group per day, which starts the evening before
its date. The dates from the day before `begin` to the day after `end` are candidates. The sim reads
each candidate's first packet and skips the files a later file already covers, so a window late in the
day does not decompress yesterday's capture. Days overlap, and the overlap is replayed once.

`pcap_reader.hpp` reads classic pcap (micro- or nanosecond) and pcapng, over Ethernet (802.1Q tags are
understood) or raw IP, and keeps only whole IPv4 UDP datagrams, so IGMP and fragments in a capture are
skipped. Compressed files are streamed through an external `zstd -dc` or `gzip -dc`, which must be on
`PATH`.

Finding the groups of a channel is up to you. On CME, in the `tradedaily` captures we have seen that
channel 310's incremental feed A is `224.0.31.1:14310`, its **MBP** snapshot is `224.0.31.22:14310`,
and its **MBO** snapshot, the one this feed needs, is `233.72.75.1:23310` (templates 52 and 53). The
snapshot capture must carry template 53 or the book never builds and the feed ends in `ERROR`.

### Speed

With `speed` above 0 the feed holds each packet until the wall time since the first replayed packet
equals the capture-time gap divided by `speed`. At `0` it never waits. The sim does not have a clock of
its own: the handlers' gap timers and the timestamps on client orders stay on the wall clock exactly
as in live mode. At a high `speed`, gap handling therefore behaves like a slow network, and the
replay is not deterministic.

### Timestamps

The market data published during a replay is stamped with the **wall clock**, not with the capture's
time: an event is stamped with the wall time at which it was due. A client that compares market-data
time with its own clock sees a live feed. At `speed` 0 the stamp is the send time. The capture time
that has been reached is in `FEED_STATUS.current_ns`, and nowhere else.

## Control channel

Clients receive their market data from the venue's multicast groups and their orders go through the
venue's TCP gateway, and neither can say "still loading" or ask for the replay to begin. So the pcap feed
runs a small TCP server of its own (`control_channel.port`). Messages are little-endian, packed, and
start with a header; `length` is the whole message.

```text
ControlHeader   { u16 length; u8 type }                      3 bytes
```

| `type` | Name | Direction |
|---|---|---|
| 1 | `FEED_STATUS` | sim → client |
| 2 | `START` | client → sim |

`FEED_STATUS` (`common/control_messages.hpp`) is sent to a client when it connects, on every state
change, and about once a second while the feed is `LOADING`, `SYNCING` or `REPLAYING`.

| Field | Type | |
|---|---|---|
| `header` | 3 bytes | |
| `venue` | u8 | The sim's `Venue` code (CME 1, ICE 2) |
| `state` | u8 | 0 IDLE, 1 LOADING, 2 SYNCING, 3 READY, 4 REPLAYING, 5 FINISHED, 6 ERROR |
| `feed` | char[32] | The feed's name |
| `begin_ns`, `end_ns` | u64 | The configured window |
| `current_ns` | u64 | Capture time reached |
| `progress_permille` | u16 | 0 to 1000 through the window, by capture time |
| `speed` | f64 | The configured speed |
| `message` | char[96] | NUL-terminated; the reason in `ERROR` |

`START` is the header alone (`length` 3). Send it once the status says `READY`, or earlier: the sim keeps
it and starts when it gets there. It is ignored once the replay has started, and in `FINISHED` or
`ERROR`, where the status already says why nothing will come. The replay is shared: the first `START`
from any client begins it for every client. Any other message disconnects the client.

**What `START` changes.** Until it arrives the publisher holds back market data on the multicast
groups. The venue may still send its static traffic, such as instrument definitions or a channel reset.
The book and the publisher's own state for every instrument stay current regardless, so when the gate
opens every instrument is published from its true state: all of them appear in the next snapshot cycle,
and their updates follow. A client takes its book from the venue's normal recovery loops, as it would
joining a live channel. The publisher runs behind the exchange thread, so the gate records where
`md_queue` stood when it opened: frames queued while the book was built reach the clients as the
snapshot, never as updates. `START` opens the gate even when the synchronization used up the last
packet of the window.

## Limitations

- Starting deep into a day's file costs the decompression of everything before `begin`.
- The control channel has no authentication and binds all interfaces.
- CME status messages (SecurityStatus30) and ICE's own are still dropped on the way in; the venue
  publishes every instrument as open.
- The window must start inside a stretch with a snapshot capture: a window that ends before one full
  snapshot pass fails with `ERROR`.
