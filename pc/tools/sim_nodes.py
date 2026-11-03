#!/usr/bin/env python3
"""Nine simulated tiles for the T-B2-06 Pico load test.

Owner: Benjamin (Buddy 2), task T-B2-06. Written 29 Oct 2026 in Benjamin's
session.

Plan row T-B2-06: "Nine simulated senders register together, then
heartbeat for 10 min while the Pico beacons." Acceptance: "Every callback at
most 1 ms; beacon lateness under 5 ms (proposed NFR16) for 100% of beacons
over 10 min." The Pico measures both itself: ``cb_max_us``, ``CB_HIST``,
``DISPATCH_HIST`` and ``LATE_HIST`` in its ``CL_STATS`` log lines
(``ledwall_sync/coordinator_loop.c``). This tool only creates the load.

What it sends (``protocol/layout_table.md`` sections 3 and 11), all to the
Pico's UDP port 4211:
  * one REGISTER for each Node ID 1 to 9, back to back (the "register
    together" burst);
  * then a HEARTBEAT from each Node ID every 0.5 s (proposed, VA-2.7). By
    default the nine heartbeats are staggered by (Node ID - 1) x 500 / 9 ms
    as real tiles do (T-B3-03); ``--burst`` sends all nine together every
    0.5 s instead, the worst case for the Pico loop (plan choice).

Every message carries the session_id of the last BEACON heard on port 4210
(tiles copy it the same way), or ``--session`` when no beacon is heard. All
nine come from this PC's address; the Pico registry is indexed by Node ID,
so that is enough to load it.

Usage:
    python3 pc/tools/sim_nodes.py --pico 192.168.1.50 [--minutes 10]
        [--burst] [--session N]
"""

from __future__ import annotations

import argparse
import os
import socket
import sys
import time
from typing import Callable, List, Optional, Tuple

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                ".."))
import proto_layout as pl  # noqa: E402

#: Ports (layout_table.md section 11).
PICO_PORT = 4211
BEACON_PORT = 4210

#: Heartbeat period, seconds (VA-2.7, proposed).
HEARTBEAT_S = 0.5

#: Node IDs simulated (T-B2-06: nine).
NODES = list(range(1, pl.NODE_MAX + 1))

#: One planned send: (time offset in seconds, node, message name).
Plan = List[Tuple[float, int, str]]


def schedule(duration_s: float, burst: bool) -> Plan:
    """Every send of a run, in time order: the REGISTER burst at 0, then the
    heartbeats. Staggered: node n at (n - 1) x 0.5 / 9 s + k x 0.5 s."""
    plan: Plan = [(0.0, node, "REGISTER") for node in NODES]
    for node in NODES:
        offset = 0.0 if burst else (node - 1) * HEARTBEAT_S / pl.NODE_MAX
        t = HEARTBEAT_S + offset
        while t < duration_s:
            plan.append((round(t, 6), node, "HEARTBEAT"))
            t += HEARTBEAT_S
    plan.sort(key=lambda item: (item[0], item[1]))
    return plan


class Sender:
    """Encodes and sends the simulated tiles' messages. ``send`` is the
    transport (a socket's ``sendto`` in a real run, a list in the tests)."""

    def __init__(self, send: Callable[[bytes], object], session: int) -> None:
        self._send = send
        self.session = session
        self._seq = {node: 0 for node in NODES}

    def message(self, node: int, name: str) -> bytes:
        """The next message from ``node``; each node has its own seq
        counter, as every tile does (layout_table.md section 10 item 13)."""
        seq = self._seq[node]
        self._seq[node] = (seq + 1) & 0xFFFF
        if name == "REGISTER":
            return pl.encode_register(session_id=self.session, seq=seq,
                                      node_id=node, cached_session_id=0,
                                      cached_crc32=0)
        return pl.encode_heartbeat(session_id=self.session, seq=seq,
                                   node_id=node)

    def send(self, node: int, name: str) -> bytes:
        data = self.message(node, name)
        self._send(data)
        return data


def last_beacon_session(sock: socket.socket) -> Optional[int]:
    """session_id of the newest BEACON waiting on the socket, if any."""
    session = None
    while True:
        try:
            data = sock.recv(64)
        except (BlockingIOError, socket.timeout):
            return session
        try:
            msg = pl.decode(data)
        except pl.LayoutError:
            continue
        if msg["type"] == "BEACON":
            session = int(msg["session_id"])  # type: ignore[call-overload]


def run(pico: str, minutes: float, burst: bool, session: int) -> int:
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    rx.bind(("", BEACON_PORT))
    rx.setblocking(False)
    sender = Sender(lambda data: tx.sendto(data, (pico, PICO_PORT)),
                    session)
    plan = schedule(minutes * 60.0, burst)
    start = time.monotonic()
    sent = 0
    for offset, node, name in plan:
        delay = start + offset - time.monotonic()
        if delay > 0:
            time.sleep(delay)
        heard = last_beacon_session(rx)
        if heard is not None:
            sender.session = heard
        sender.send(node, name)
        sent += 1
    print("sim_nodes: %d messages sent (%s heartbeats), last session %d"
          % (sent, "burst" if burst else "staggered", sender.session))
    return 0


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--pico", required=True, help="Pico IPv4 address")
    parser.add_argument("--minutes", type=float, default=10.0,
                        help="run length (T-B2-06: 10 min)")
    parser.add_argument("--burst", action="store_true",
                        help="all nine heartbeats together (worst case)")
    parser.add_argument("--session", type=int, default=0,
                        help="session_id until a beacon is heard")
    args = parser.parse_args(argv)
    return run(args.pico, args.minutes, args.burst, args.session)


if __name__ == "__main__":
    sys.exit(main())
