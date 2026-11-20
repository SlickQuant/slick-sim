#!/usr/bin/env python3
"""Command client for slick-sim's CLI port (the top-level "cli" config block).

Runs the command on the command line, or with none, reads commands from a prompt.

    tools/slick_sim_cli.py --port 17400 targets
    tools/slick_sim_cli.py --port 17400 cme reject-logon on FIRM9:BAD
    tools/slick_sim_cli.py --port 17400 ice reject-new-orders off GW1 GW2
    tools/slick_sim_cli.py --port 17400 cme rejects
    tools/slick_sim_cli.py --port 17400 sim log-level debug
    tools/slick_sim_cli.py --port 17400 sim shutdown
    tools/slick_sim_cli.py --port 17400

A target is `sim`, a venue's config key (`cme` is every segment) or a path under it
(`cme/<segment>`, `ice/<silo>`, `ice/<silo>/bgw0`); `targets` lists them. IDs are
FIRM:SESSION for CME and the gateway ID for ICE, one request each. Exits 1 if any
reply failed.

The wire format is in slick-sim's docs/cli.md (common/control_messages.hpp).
"""
import argparse
import shlex
import socket
import struct
import sys

REQUEST = struct.Struct("<HBIBB32s64s")   # length, type, request id, command, flag, target, arg
REPLY = struct.Struct("<HBIB")            # length, type, request id, status; then the text
CLI_REQUEST, CLI_REPLY = 3, 4
TARGETS, LOG_LEVEL, SHUTDOWN, REJECT_LOGON, REJECT_NEW_ORDERS, REJECTS = range(1, 7)
# name: (command, takes on|off, IDs: "+" one or more, "?" at most one, "" none)
COMMANDS = {
    "reject-logon": (REJECT_LOGON, True, "+"),
    "reject-new-orders": (REJECT_NEW_ORDERS, True, "+"),
    "rejects": (REJECTS, False, ""),
    "log-level": (LOG_LEVEL, False, "?"),
    "shutdown": (SHUTDOWN, False, ""),
}
USAGE = f"<target> <command> [on|off] [ID ...], or targets. Commands: {', '.join(COMMANDS)}"


class Cli:
    def __init__(self, host, port, timeout):
        self.address, self.timeout = (host, port), timeout
        self.next_id = 1
        self.connect()

    def connect(self):
        self.sock = socket.create_connection(self.address, timeout=self.timeout)
        self.buffer = b""

    def call(self, target, command, flag, arg):
        """Sends one request; returns (ok, text) from its reply. A prompt left idle past
        the port's cli.idle_timeout finds its connection closed, so this reconnects once
        and sends again."""
        try:
            return self.send(target, command, flag, arg)
        except ConnectionError:
            self.sock.close()
            self.connect()
            return self.send(target, command, flag, arg)

    def send(self, target, command, flag, arg):
        request_id, self.next_id = self.next_id, self.next_id + 1
        self.sock.sendall(REQUEST.pack(REQUEST.size, CLI_REQUEST, request_id, command, flag,
                                       target.encode(), arg.encode()))
        while True:
            if len(self.buffer) >= REPLY.size:
                length, kind, reply_id, status = REPLY.unpack_from(self.buffer)
                if len(self.buffer) >= length:
                    text, self.buffer = self.buffer[REPLY.size:length], self.buffer[length:]
                    if kind == CLI_REPLY and reply_id == request_id:
                        return status == 0, text.decode(errors="replace")
                    continue
            data = self.sock.recv(65536)
            if not data:
                raise ConnectionError("the CLI port closed the connection")
            self.buffer += data


def parse(words):
    """The requests one command line makes, as (target, command, flag, arg)."""
    if words == ["targets"]:
        return [("", TARGETS, 0, "")]
    if len(words) < 2:
        raise ValueError(USAGE)
    target, name, rest = words[0], words[1], words[2:]
    if name not in COMMANDS:
        raise ValueError(f"unknown command '{name}'. {USAGE}")
    command, toggles, ids = COMMANDS[name]
    flag = 0
    if toggles:
        if not rest or rest[0] not in ("on", "off"):
            raise ValueError(f"{name} takes on or off, then one or more IDs")
        flag, rest = int(rest[0] == "on"), rest[1:]
    if (ids == "+" and not rest) or (ids == "?" and len(rest) > 1) or (not ids and rest):
        raise ValueError(f"{name}: wrong number of arguments. {USAGE}")
    if len(target.encode()) > 32 or any(len(arg.encode()) > 64 for arg in rest):
        raise ValueError("a target is at most 32 bytes, an ID 64")
    return [(target, command, flag, arg) for arg in (rest or [""])]


def run(cli, words):
    """Runs one command line and prints the replies. True when all were OK."""
    ok = True
    for request in parse(words):
        good, text = cli.call(*request)
        ok &= good
        print(text if good else f"failed: {text}", file=sys.stdout if good else sys.stderr, flush=True)
    return ok


def prompt(cli):
    try:
        import readline  # noqa: F401  line editing and history, where there is one
    except ImportError:
        pass
    print(f"{USAGE}. quit to leave.")
    while True:
        try:
            line = input("slick-sim> ")
        except EOFError:
            print()
            return 0
        words = shlex.split(line)
        if not words:
            continue
        if words[0] in ("quit", "exit"):
            return 0
        try:
            run(cli, words)
        except ValueError as error:
            print(error, file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=17400, help="the sim's cli.port (default 17400)")
    parser.add_argument("--timeout", type=float, default=10, help="seconds to wait for a reply")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="a command; none for a prompt")
    args = parser.parse_args()

    try:
        cli = Cli(args.host, args.port, args.timeout)
        if not args.command:
            return prompt(cli)
        return 0 if run(cli, args.command) else 1
    except (OSError, ValueError) as error:
        print(error, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
