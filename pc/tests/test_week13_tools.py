"""pytest: pc/tools/gen_week13_docs.py (T-X-06, T-X-04)."""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import gen_week13_docs as gw  # noqa: E402


def test_after_bytes_match_the_encoder() -> None:
    assert gw.after_bytes("BEACON")[0] == 12
    assert gw.after_bytes("ASSET_CHUNK")[0] == 1029
    assert "93 bits" in gw.after_bytes("BEACON")[1]


def test_fill_after_only_fills_empty_cells() -> None:
    text = ("| Asset per tile | 115,200 B | derived | x | |\n"
            "| RAM copy plus margin | 147,968 B | derived | x | kept |\n")
    new, filled = gw.fill_after(text)
    assert filled == 1
    assert "57,600 B" in new.splitlines()[0]
    assert new.splitlines()[1].endswith("| kept |")
