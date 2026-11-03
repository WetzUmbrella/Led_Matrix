"""pytest: pc/tools/sim_nodes.py (T-B2-06 load generator).

Owner: Benjamin (Buddy 2). Written 29 Oct 2026 in Benjamin's session.
The load itself is measured on the Pico; these tests check that the tool
sends what the plan row says: nine REGISTERs together, then one HEARTBEAT
per node every 0.5 s, staggered by (Node ID - 1) x 500 / 9 ms or in bursts.
"""

from __future__ import annotations

import os
import sys
from collections import Counter
from typing import List

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import proto_layout as pl  # noqa: E402
import sim_nodes as sn  # noqa: E402


def test_register_burst_first() -> None:
    plan = sn.schedule(2.0, burst=False)
    first = plan[:9]
    assert [name for _, _, name in first] == ["REGISTER"] * 9
    assert {t for t, _, _ in first} == {0.0}
    assert sorted(node for _, node, _ in first) == list(range(1, 10))


def test_ten_minutes_of_heartbeats() -> None:
    plan = sn.schedule(600.0, burst=False)
    beats = Counter(node for _, node, name in plan if name == "HEARTBEAT")
    # 0.5 s period from 0.5 s (plus the stagger) to before 600 s: 1199 each.
    assert set(beats.values()) == {1199}
    assert len(beats) == 9


def test_stagger_matches_tile_net() -> None:
    plan = sn.schedule(1.0, burst=False)
    first_beat = {node: t for t, node, name in reversed(plan)
                  if name == "HEARTBEAT"}
    for node, t in first_beat.items():
        assert abs(t - (0.5 + (node - 1) * 0.5 / 9)) < 1e-6


def test_burst_mode_sends_nine_together() -> None:
    plan = sn.schedule(0.9, burst=True)
    beats = [t for t, _, name in plan if name == "HEARTBEAT"]
    assert beats == [0.5] * 9


def test_messages_decode_with_own_seq_per_node() -> None:
    sent: List[bytes] = []
    sender = sn.Sender(sent.append, session=3)
    for _ in range(3):
        sender.send(4, "HEARTBEAT")
    sender.send(5, "REGISTER")
    decoded = [pl.decode(data) for data in sent]
    assert [d["type"] for d in decoded] == ["HEARTBEAT"] * 3 + ["REGISTER"]
    assert [d["seq"] for d in decoded] == [0, 1, 2, 0]
    assert all(d["session_id"] == 3 for d in decoded)
    assert decoded[3]["node_id"] == 5
