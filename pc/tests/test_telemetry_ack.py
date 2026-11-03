"""pytest: pc/tools/telemetry_ack.py (T-B3-06 VC-4 receiver).

Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston.
No network: the tests drive ``handle()`` with encoded TELEMETRY bytes.
"""

from __future__ import annotations

import os
import sys
from typing import Dict

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import proto_layout as pl  # noqa: E402
import telemetry_ack as ta  # noqa: E402


def _telemetry(seq: int, word: int, node: int = 4) -> bytes:
    return pl.encode_telemetry(session_id=0, seq=seq, node_id=node,
                               fault_word=word, orientation=0,
                               display_state=4)


def test_drop_every_third() -> None:
    assert [ta.should_drop(n, 3) for n in range(1, 7)] == [
        False, False, True, False, False, True]
    assert not any(ta.should_drop(n, 0) for n in range(1, 10))


def test_ack_carries_the_packet_seq() -> None:
    nodes: Dict[int, ta.NodeTrack] = {}
    text, ack = ta.handle(_telemetry(1234, 0), nodes, 0, 3)
    assert text is not None and "seq=1234" in text
    assert ack is not None
    msg = pl.decode_telemetry_ack(ack)
    assert msg["acked_seq"] == 1234


def test_dropped_packet_gets_no_ack() -> None:
    nodes: Dict[int, ta.NodeTrack] = {}
    acks = [ta.handle(_telemetry(seq, 0), nodes, 3, 3)[1]
            for seq in range(6)]
    assert [ack is None for ack in acks] == [
        False, False, True, False, False, True]


def test_drop_counts_per_node() -> None:
    nodes: Dict[int, ta.NodeTrack] = {}
    first = [ta.handle(_telemetry(s, 0, node=1), nodes, 3, 3)[1]
             for s in range(2)]
    other = ta.handle(_telemetry(0, 0, node=2), nodes, 3, 3)[1]
    assert all(a is not None for a in first) and other is not None


def test_not_telemetry_is_ignored() -> None:
    nodes: Dict[int, ta.NodeTrack] = {}
    beacon = pl.encode_beacon(session_id=0, seq=1, t_p=5)
    assert ta.handle(beacon, nodes, 3, 3) == (None, None)
    assert ta.handle(b"\x00", nodes, 3, 3) == (None, None)
    assert nodes == {}


def test_reports_and_condition_transitions() -> None:
    nodes: Dict[int, ta.NodeTrack] = {}
    event = 1 << 1  # FAULT_BIT_CRC_FAIL
    cond = 1 << 3  # FAULT_BIT_BEACON_LOSS
    words = [0, event, event, 0, cond, cond | event, cond, cond, 0]
    for seq, word in enumerate(words):
        ta.handle(_telemetry(seq, word), nodes, 0, 3)
    node = nodes[4]
    assert node.reports[1] == 2
    assert node.reports[3] == 1
    # The condition goes on once and off once at the end: one transition.
    assert node.condition_transitions == 1
    lines = ta.summary(nodes, 3)
    assert "bit1=2" in lines[0] and "transitions=1" in lines[1]


def test_held_condition_has_zero_transitions_with_drops() -> None:
    nodes: Dict[int, ta.NodeTrack] = {}
    cond = 1 << 3
    for seq in range(30):
        ta.handle(_telemetry(seq, cond), nodes, 3, 3)
    assert nodes[4].condition_seen
    assert nodes[4].condition_transitions == 0
