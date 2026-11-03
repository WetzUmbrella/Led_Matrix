#!/usr/bin/env python3
"""TELEMETRY receiver and ACK sender for the VC-4 test (T-B3-06).

Owner: Cheston (Buddy 3), task T-B3-06. Implemented by Ahsanul (B4) for
Cheston. A bench tool, not the dashboard (``pc/telemetry.py`` is T-B1-05).

Plan row T-B3-06, acceptance: "VC-4 test: the PC receiver drops every third
telemetry packet for 10 min while faults are injected: 100% of injected
faults appear on the dashboard log; a condition fault held for 10 s shows
continuously, with 0 off/on transitions. Pass = both."

What it does (``protocol/layout_table.md`` sections 3 and 11):
  * binds UDP 4215, where every tile broadcasts TELEMETRY;
  * logs one line per packet: time, source address, node, seq, fault word,
    orientation, display state, and ``DROPPED`` for the packets it ignores;
  * replies TELEMETRY_ACK {acked_seq = the packet's header seq} to the
    sender's address on port 4214, except for dropped packets;
  * with ``--drop-every 3`` ignores every third packet of each node;
  * on exit prints, per node, the number of reports of each fault bit
    (a report is a received packet with the bit set after one without it)
    and the off/on transitions of the ``--condition`` bit.

Usage:
    python3 pc/tools/telemetry_ack.py --drop-every 3 --minutes 10
        [--condition 3] --log docs/week13/vc4_dashboard.log
"""

from __future__ import annotations

import argparse
import os
import socket
import sys
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, TextIO, Tuple

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                ".."))
import proto_layout as pl  # noqa: E402
import telemetry  # noqa: E402

#: Ports (layout_table.md section 11).
TELEMETRY_PORT = 4215
TILE_PC_PORT = 4214

#: Bits in the 32-bit fault word.
WORD_BITS = 32

#: FAULT_BIT_BEACON_LOSS (panel_sync/main/fault_word.h), the condition the
#: bench holds for 10 s by switching the Pico off.
DEFAULT_CONDITION_BIT = 3


@dataclass
class NodeTrack:
    """What the receiver has seen from one node."""

    count: int = 0  #: TELEMETRY packets received, dropped ones included.
    last_word: Optional[int] = None  #: Fault word of the last kept packet.
    reports: Dict[int, int] = field(default_factory=dict)  #: bit -> reports
    condition_seen: bool = False  #: The condition bit has been on.
    condition_transitions: int = 0  #: Off/on changes after it came on.


def should_drop(count: int, drop_every: int) -> bool:
    """True for the drop_every-th, 2 x drop_every-th, ... packet (count is
    1-based); 0 drops nothing."""
    return drop_every > 0 and count % drop_every == 0


#: TELEMETRY_ACK encoder shared with the dashboard's receiver.
make_ack = telemetry.make_ack


def track(node: NodeTrack, word: int, condition_bit: int) -> None:
    """Count new reports and condition transitions for one kept packet."""
    previous = 0 if node.last_word is None else node.last_word
    for bit in range(WORD_BITS):
        mask = 1 << bit
        if word & mask and not previous & mask:
            node.reports[bit] = node.reports.get(bit, 0) + 1
    on = bool(word & (1 << condition_bit))
    was_on = bool(previous & (1 << condition_bit))
    if node.condition_seen and on != was_on:
        node.condition_transitions += 1
    if on:
        node.condition_seen = True
    node.last_word = word


def handle(data: bytes, nodes: Dict[int, NodeTrack], drop_every: int,
           condition_bit: int) -> Tuple[Optional[str], Optional[bytes]]:
    """One datagram: returns (log text or None, ACK bytes or None).

    Anything that is not a valid TELEMETRY gives (None, None).
    """
    try:
        msg = pl.decode_telemetry(data)
    except pl.LayoutError:
        return None, None
    node_id = int(msg["node_id"])  # type: ignore[call-overload]
    seq = int(msg["seq"])  # type: ignore[call-overload]
    word = int(msg["fault_word"])  # type: ignore[call-overload]
    node = nodes.setdefault(node_id, NodeTrack())
    node.count += 1
    text = "node=%d seq=%d fault=0x%08x orientation=%d display_state=%d" % (
        node_id, seq, word, msg["orientation"], msg["display_state"])
    if should_drop(node.count, drop_every):
        return text + " DROPPED", None
    track(node, word, condition_bit)
    session = int(msg["session_id"])  # type: ignore[call-overload]
    return text, make_ack(session, seq)


def summary(nodes: Dict[int, NodeTrack], condition_bit: int) -> List[str]:
    """Closing lines: reports per bit and condition transitions per node."""
    lines = []
    for node_id in sorted(nodes):
        node = nodes[node_id]
        bits = " ".join("bit%d=%d" % (bit, node.reports[bit])
                        for bit in sorted(node.reports))
        lines.append("SUMMARY node=%d packets=%d reports: %s" % (
            node_id, node.count, bits or "none"))
        lines.append("SUMMARY node=%d condition_bit=%d seen=%s "
                     "transitions=%d" % (node_id, condition_bit,
                                         node.condition_seen,
                                         node.condition_transitions))
    return lines


def run(drop_every: int, minutes: float, condition_bit: int,
        out: TextIO) -> None:
    """Receive for ``minutes`` and ACK, logging to ``out`` and stdout."""
    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    rx.bind(("", TELEMETRY_PORT))
    rx.settimeout(0.5)
    nodes: Dict[int, NodeTrack] = {}
    end = time.monotonic() + minutes * 60.0

    def emit(line: str) -> None:
        print(line)
        out.write(line + "\n")
        out.flush()

    try:
        while time.monotonic() < end:
            try:
                data, (addr, _port) = rx.recvfrom(64)
            except socket.timeout:
                continue
            text, ack = handle(data, nodes, drop_every, condition_bit)
            if text is None:
                continue
            emit("%.3f %s %s" % (time.time(), addr, text))
            if ack is not None:
                rx.sendto(ack, (addr, TILE_PC_PORT))
    except KeyboardInterrupt:
        pass
    finally:
        rx.close()
        for line in summary(nodes, condition_bit):
            emit(line)


def main(argv: Optional[List[str]] = None) -> int:
    """Command line entry point."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--drop-every", type=int, default=0,
                        help="ignore every Nth packet of each node (VC-4: 3)")
    parser.add_argument("--minutes", type=float, default=10.0)
    parser.add_argument("--condition", type=int,
                        default=DEFAULT_CONDITION_BIT,
                        help="condition bit to watch (default 3, "
                             "BEACON_LOSS)")
    parser.add_argument("--log", default="telemetry_ack.log")
    args = parser.parse_args(argv)
    with open(args.log, "a", encoding="utf-8") as out:
        run(args.drop_every, args.minutes, args.condition, out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
