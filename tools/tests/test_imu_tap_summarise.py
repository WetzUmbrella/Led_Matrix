"""Tests for tools/imu_tap_log/summarise.py on small synthetic logs.

Scenario (wall time relative to 10:00:00, tile 3 clock 37 s ahead of it,
tile 5 clock 12 s ahead):

  2 s   tile 3 centre tap   tile 3 peaks 800, tile 5 (neighbour) 150
  4 s   tile 5 edge tap     tile 5 peaks 600, tile 3 (neighbour) 120
  6 s   board knock         both tiles 90
  8 s   tile 3 Blu Tack     tile 3 peaks 300

So the tap peaks are 800 (tile 3) and 600 (tile 5), the highest non-tap peak
is 300 (the press), and the first threshold with no false trigger and every
tap detected is 325 mg on the default sweep (50 to 1000, step 25).
Run: python3 -m pytest -q tools/tests
"""

import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "imu_tap_log"))
import marks  # noqa: E402
import summarise  # noqa: E402
from marks import LogError  # noqa: E402

BASE_MS = 10 * 3600 * 1000
SHIFT = {"3": 37000, "5": 12000}


def wall_text(rel_ms):
    total = BASE_MS + rel_ms
    return "%02d:%02d:%02d.%03d" % (total // 3600000, (total // 60000) % 60,
                                    (total // 1000) % 60, total % 1000)


def bump(bumps, start_ms, peak, width=3):
    for k in range(width):
        bumps[start_ms + 10 * k] = peak


def write_log(path, board, first_ms, end_ms, bumps=None, skip=(),
              overruns=0):
    bumps = bumps or {}
    lines = ["H,board=%s,range_g=16,odr_hz=1000,window_ms=10,src=fifo,"
             "tau_ms=500,hold_mg=100" % board]
    for ms in range(first_ms, end_ms, 10):
        if ms % 1000 == 0:
            lines.append("S,%d,1000,%d" % (ms, overruns))
        if ms not in skip:
            lines.append("T,%d,%d" % (ms, bumps.get(ms, 10)))
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("\n".join(lines) + "\n")
    return str(path)


def write_taps(path, rows):
    path.write_text("wall,tile,kind\n" + "".join(
        "%s,%s,%s\n" % (wall_text(rel), tile, kind)
        for rel, tile, kind in rows), encoding="utf-8")
    return str(path)


def scenario(tmp_path, knock_peak=90, extra_rows=(), skip3=(), extra3=None):
    """Write the two tile logs and the taps file of the scenario.
    extra3 adds {wall_rel_ms: peak} bumps to tile 3's log."""
    bumps3, bumps5 = {}, {}
    bump(bumps3, 2000 + SHIFT["3"], 800)
    bump(bumps3, 4000 + SHIFT["3"], 120)
    bump(bumps3, 6000 + SHIFT["3"], knock_peak)
    bump(bumps3, 8000 + SHIFT["3"], 300)
    for rel, peak in (extra3 or {}).items():
        bump(bumps3, rel + SHIFT["3"], peak)
    bump(bumps5, 2000 + SHIFT["5"], 150)
    bump(bumps5, 4000 + SHIFT["5"], 600)
    bump(bumps5, 6000 + SHIFT["5"], knock_peak)
    logs = [
        write_log(tmp_path / "h6_tap_3.log", "3", SHIFT["3"],
                  SHIFT["3"] + 10000, bumps3, skip=set(skip3)),
        write_log(tmp_path / "h6_tap_5.log", "5", SHIFT["5"],
                  SHIFT["5"] + 10000, bumps5),
    ]
    rows = [(2000, "3", "c"), (4000, "5", "e"), (6000, "board", "k"),
            (8000, "3", "b")] + list(extra_rows)
    taps = write_taps(tmp_path / "taps.csv", rows)
    return taps, logs


def analyse_all(taps, logs, neighbours=None):
    tap_list = marks.read_taps(taps)
    nb = summarise.parse_pairs(neighbours) if neighbours else None
    return {log.tile: summarise.analyse(log, tap_list, marks.ALIGN_MG,
                                        marks.TOL_MS, nb)
            for log in map(marks.parse_log, logs)}


# --- classes, peaks and false triggers -----------------------------------

def test_windows_are_classed_by_what_happened_near_them(tmp_path):
    taps, logs = scenario(tmp_path)
    tile3 = analyse_all(taps, logs)["3"]
    assert tile3.offset == BASE_MS - SHIFT["3"]
    assert tile3.tap_peaks == [800]
    assert tile3.press_peaks == [300]
    # Neighbour events are read on this tile's windows: tile 5's edge tap
    # at 4 s is seen by tile 3 at 120 mg.
    assert tile3.nbr_peaks == [120]
    assert tile3.knock_peaks == [90]
    tile5 = analyse_all(taps, logs)["5"]
    # Tile 5 sees tile 3's centre tap at 150 mg and its press at 10 mg.
    assert sorted(tile5.nbr_peaks) == [10, 150]
    labels = set(tile3.cls)
    assert {"tap", "press", "nbr", "knock", "quiet"} <= labels


def test_false_triggers_fall_as_the_threshold_rises(tmp_path):
    taps, logs = scenario(tmp_path)
    analysed = analyse_all(taps, logs)
    tile3, tile5 = analysed["3"], analysed["5"]
    # Tile 3: neighbour 120, knock 90 and press 300 are the non-tap triggers.
    assert summarise.false_triggers(tile3, 50, 50) == 3
    assert summarise.false_triggers(tile3, 125, 50) == 1
    assert summarise.false_triggers(tile3, 300, 50) == 1
    assert summarise.false_triggers(tile3, 325, 50) == 0
    # Tile 5: neighbour 150 and knock 90, its own edge tap is not false.
    assert summarise.false_triggers(tile5, 50, 50) == 2
    assert summarise.false_triggers(tile5, 150, 50) == 1
    assert summarise.false_triggers(tile5, 175, 50) == 0


def test_one_tap_spanning_three_windows_is_one_trigger(tmp_path):
    taps, logs = scenario(tmp_path)
    tile3 = analyse_all(taps, logs)["3"]
    # The press is three windows at 300 mg: one trigger, not three.
    assert summarise.false_triggers(tile3, 300, 50) == 1
    # Without dedupe each window counts again.
    assert summarise.false_triggers(tile3, 300, 0) == 3


def test_far_tiles_are_not_neighbours_when_pairs_are_given(tmp_path):
    # Tile 7 taps at 9 s, and tile 3 shows a 400 mg bump at the same time.
    taps, logs = scenario(tmp_path, extra_rows=[(9000, "7", "c")],
                          extra3={9000: 400})
    everyone = analyse_all(taps, logs)["3"]
    pairs = analyse_all(taps, logs, neighbours="3-5")["3"]
    # Every other tile is a neighbour by default: the 400 mg bump counts.
    assert summarise.false_triggers(everyone, 50, 50) == 4
    # Only 3-5 are neighbours: tile 7's tap is far, and far never counts.
    assert "far" in pairs.cls
    assert summarise.false_triggers(pairs, 50, 50) == 3


def test_detection_counts_tap_events_at_or_above_the_threshold(tmp_path):
    taps, logs = scenario(tmp_path)
    analysed = analyse_all(taps, logs)
    total = sum(len(a.tap_peaks) for a in analysed.values())
    assert total == 2
    assert sum(summarise.detected(a, 325) for a in analysed.values()) == 2
    assert sum(summarise.detected(a, 801) for a in analysed.values()) == 0


# --- rate, overruns and gaps ---------------------------------------------

def test_rate_overruns_and_gaps_come_from_the_log(tmp_path):
    skip = {37000 + 500 + k for k in range(0, 60, 10)}  # 6 windows missing
    bumps = {}
    bump(bumps, 2000 + 37000, 800)
    path = write_log(tmp_path / "h6_tap_3.log", "3", 37000, 47000,
                     bumps, skip=skip, overruns=2)
    log = marks.parse_log(path)
    rates, overruns, gaps = summarise.window_stats(log)
    assert rates == [1000.0] * 9
    assert overruns == 20
    assert gaps == 6


def test_consecutive_windows_have_no_gap(tmp_path):
    path = tmp_path / "h6_tap_3.log"
    path.write_text("H,board=3,src=fifo\nT,1000,10\nT,1010,10\n"
                    "T,1020,10\nT,1030,10\n", encoding="utf-8")
    log = marks.parse_log(str(path))
    assert summarise.window_stats(log)[2] == 0


# --- parsing the neighbour pairs -----------------------------------------

def test_parse_pairs_is_symmetric():
    assert summarise.parse_pairs("3-5,3-4") == {
        "3": {"4", "5"}, "5": {"3"}, "4": {"3"}}


def test_parse_pairs_rejects_a_single_tile():
    with pytest.raises(LogError, match="A-B"):
        summarise.parse_pairs("3")


# --- command line ---------------------------------------------------------

def test_cli_suggests_the_first_clean_threshold(tmp_path, capsys):
    taps, logs = scenario(tmp_path)
    assert summarise.main(["--taps", taps] + logs) == 0
    out = capsys.readouterr().out
    assert "Suggested threshold: 325 mg" in out
    assert "Highest non-tap peak: 300 mg. Lowest tap peak: 600 mg." in out


def test_cli_says_so_when_no_threshold_works(tmp_path, capsys):
    # A knock of 900 mg is louder than the 800 mg tap on tile 3, so no
    # threshold both stays quiet on the knock and sees every tap.
    taps, logs = scenario(tmp_path, knock_peak=900)
    assert summarise.main(["--taps", taps] + logs) == 0
    assert "No threshold in the sweep" in capsys.readouterr().out


def test_cli_rate_and_overrun_table(tmp_path, capsys):
    taps, logs = scenario(tmp_path)
    summarise.main(["--taps", taps] + logs)
    out = capsys.readouterr().out
    assert "1000.0" in out
    assert "boot_wall" in out
    assert "09:59:23.000" in out and "09:59:48.000" in out


def test_cli_returns_2_when_a_log_has_no_taps(tmp_path, capsys):
    taps, logs = scenario(tmp_path)
    stray = write_log(tmp_path / "h6_tap_8.log", "8", 0, 1000)
    assert summarise.main(["--taps", taps, stray]) == 2
    assert "no c or e rows" in capsys.readouterr().err


def test_cli_refuses_two_logs_for_one_tile(tmp_path, capsys):
    taps, logs = scenario(tmp_path)
    copy = write_log(tmp_path / "again_3.log", "3", 37000, 47000)
    assert summarise.main(["--taps", taps, logs[0], copy]) == 2
    assert "same tile" in capsys.readouterr().err
