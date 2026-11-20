# CLI

A command port for changing a running simulator. A small TCP server on its own thread takes each
command and hands it over a lock-free queue to the thread that owns what it changes: the main thread
for `sim` commands, and a venue's order gateway threads for that venue's, so the matching loop never
handles a command.

## Config

A top-level `cli` block turns it on:

```json
"cli": { "port": 17400 }
```

| Key | Default | Notes |
|---|---|---|
| `port` | `17400` | `0` asks the OS for one |
| `max_connections` | `4` | A connection past this is closed as soon as it's accepted |
| `idle_timeout` | `300` | Seconds a connection can stay silent before it's closed, so dead ones can't hold every slot. `0` never closes them. The client reconnects by itself |
| `enabled` | `true` | |

The port binds all interfaces and has no authentication, so keep it off untrusted networks. It needs
a build with `SLICK_SIM_ENABLE_TCP_GATEWAY` (the default); without one the block only logs a warning.

## Client

`tools/slick_sim_cli.py` needs nothing beyond Python 3:

```bash
tools/slick_sim_cli.py --port 17400 targets
tools/slick_sim_cli.py --port 17400 cme reject-logon on FIRM9:BAD
tools/slick_sim_cli.py --port 17400 ice reject-new-orders off GW1 GW2
tools/slick_sim_cli.py --port 17400 cme rejects
tools/slick_sim_cli.py --port 17400 sim log-level debug
tools/slick_sim_cli.py --port 17400 sim shutdown
tools/slick_sim_cli.py --port 17400            # a prompt taking the same commands
```

Each ID goes as a request of its own. The exit code is 1 if any reply failed.

## Targets

| Target | Reaches |
|---|---|
| `sim` | The main thread |
| `<venue>` | Everything built from that venue's config key: `cme` is every segment's gateway |
| `<venue>/<instance>[/...]` | What is under that path: `cme/<segment>`, `ice/<silo>`, `ice/<silo>/bgw0` |

A venue configured without a list of instances has one, named after the venue, so its path is plain
`cme`. `targets` lists what the running process has. A command sent to several endpoints is answered
once, a line per endpoint, and fails if any of them failed.

## Commands

| Command | Target | What it does |
|---|---|---|
| `reject-logon on <ID>...` | CME, ICE | Refuses the ID's logons from its next one. A session already logged in stays up. `off` undoes it. |
| `reject-new-orders on <ID>...` | CME, ICE | Rejects the ID's new orders from the next one, sessions already up included. `off` undoes it. |
| `rejects` | CME, ICE | Lists both. |
| `log-level [level]` | `sim` | Sets the log level, or reports it. |
| `shutdown` | `sim` | Stops the simulator, as Ctrl+C does. |

The CME and ICE commands need slick-sim-cme or slick-sim-ice 0.5.0 or later built in. Without a venue
that serves them, there are only `targets`, `log-level` and `shutdown`.

IDs are `FIRM:SESSION` for CME and the gateway ID for ICE. Both lists start from the venue's
`reject_logon` and `reject_new_orders` config. Changes aren't saved, so a restart goes back to the
config.

## Wire format

Little-endian and packed, framed like the [pcap control channel](pcap-replay.md#control-channel):
`ControlHeader { u16 length; u8 type }`, where `length` is the whole message.

| `type` | Name | Direction |
|---|---|---|
| 3 | `CLI_REQUEST` | client → sim |
| 4 | `CLI_REPLY` | sim → client |

`CliRequestMessage` is 105 bytes:

| Field | Type | Notes |
|---|---|---|
| `header` | 3 bytes | |
| `request_id` | u32 | Echoed in the reply |
| `command` | u8 | 1 targets, 2 log_level, 3 shutdown, 4 reject_logon, 5 reject_new_orders, 6 rejects |
| `flag` | u8 | 1 on, 0 off |
| `target` | char[32] | NUL-padded |
| `arg` | char[64] | The ID or level, NUL-padded |

`CliReplyMessage` is the header, `request_id` (u32) and `status` (u8, 0 OK and 1 FAILED), then UTF-8
text up to `length`. Each line reads `<endpoint>: <what it said>`. `targets` gets one target per line,
with `(N endpoints)` after one that reaches more than one. An endpoint's answer is cut at 4096 bytes,
ending in `...`, and the whole text at 65527 so `length` fits its u16.

A client can send several requests at once, but they're answered one at a time and in order. Up to
64 can wait behind the one being answered, and one more closes the connection, as anything malformed
does. An endpoint that doesn't answer within 2 s fails the request, though the command stays queued
and still runs if the endpoint catches up. An endpoint with 16 commands unanswered is busy and refuses
more until it answers.

## Adding commands

A venue returns its endpoints from `Exchange::cliEndpoints()` (`cli/cli_endpoint.hpp`), names each one
`<venue>/<instance>[/...]`, and has the thread that owns the state call `CliEndpoint::serve()` from its
loop. CME and ICE do it in their gateways' `on_poll()`, with `cli::serve_rejects`
(`cli/reject_commands.hpp`). New command codes go at the end of `CliCommandType`.
