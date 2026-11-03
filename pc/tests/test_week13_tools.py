"""pytest: pc/tools/gen_week13_docs.py (T-X-06, T-X-04) and
pc/tools/evidence_index.py (T-B1-07).

Implemented by Ahsanul (B4) for Benjamin and Jolyn.
"""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import evidence_index as ei  # noqa: E402
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


def test_unrun_procedure_is_not_evidence() -> None:
    task = ei.Task("T-B5-08", "Jia Jun", "", "`docs/week13/power_thermal.md`",
                   "", "Week 13")
    assert ei.status_of(task)[0] == "not yet produced"


def test_measured_files_are_evidence() -> None:
    task = ei.Task("T-B4-01", "Ahsanul", "`docs/week13/before/idf_size.txt`",
                   "", "", "Week 10")
    assert ei.status_of(task) == ("evidence present",
                                  ["docs/week13/before/idf_size.txt"])


def test_code_without_evidence() -> None:
    task = ei.Task("T-B3-04", "Cheston", "`panel_sync/main/asset_store.c`",
                   "Logs", "", "Week 13")
    assert ei.status_of(task)[0].startswith("code present")
