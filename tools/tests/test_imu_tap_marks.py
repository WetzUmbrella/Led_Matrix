"""Tests for tools/imu_tap_log/marks.py on small synthetic logs.

Each fixture gives a tile a clock that starts at a known wall time, writes
its T lines with that clock, and writes the taps CSV with wall times on the
operator's clock. The offset that align() finds must then equal the shift.
Run: python3 -m pytest -q tools/tests
"""

import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "imu_tap_log"))
import marks  # noqa: E402
from marks import LogError  # noqa: E402

BASE_MS = 10 * 3600 * 1000  # the scenarios run from 10:00:00 local time


def wall_text(rel_ms):
    """HH:MM:SS.mmm for rel_ms after 10:00:00."""
    total = BASE_MS + rel_ms
    hours = total // 3600000
    minutes = (total // 60000) % 60
    seconds = (total // 1000) % 60
    return "%02d:%02d:%02d.%03d" % (hours, minutes, seconds, total % 1000)


def bump(bumps, start_ms, peak, width=3):
    """Set width consecutive windows (10 ms apart) to peak."""
    for k in range(width):
        bumps[start_ms + 10 * k] = peak


def write_log(path, board, first_ms, end_ms, bumps=None, skip=(),
              overruns=0, prefix=False, src="fifo", header=True):
    """Tile log: T lines every 10 ms from first_ms to end_ms (tile clock),
    one S line per whole second. skip removes T lines (missed windows)."""
    bumps = bumps or {}
    lines = []
    if header:
        lines.append("H,board=%s,range_g=16,odr_hz=1000,window_ms=10,"
                     "src=%s,tau_ms=500,hold_mg=100" % (board, src))
    for ms in range(first_ms, end_ms, 10):
        if ms % 1000 == 0:
            lines.append("S,%d,1000,%d" % (ms, overruns))
        if ms in skip:
            continue
        lines.append("T,%d,%d" % (ms, bumps.get(ms, 10)))
    stamp = "10:00:00.000 " if prefix else ""
    with open(path, "w", encoding="utf-8", newline="") as handle:
        handle.write("".join(stamp + line + "\r\n" for line in lines))
    return str(path)


def write_taps(path, rows, header="wall,tile,kind"):
    lines = ["# operator taps, 10:00:00 test session", header]
    lines += ["%s,%s,%s" % row for row in rows]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return str(path)


# --- wall times -----------------------------------------------------------

def test_parse_wall_accepts_seconds_and_fractions():
    assert marks.parse_wall("00:00:00") == 0
    assert marks.parse_wall("10:00:00") == BASE_MS
    assert marks.parse_wall("10:00:00.5") == BASE_MS + 500
    assert marks.parse_wall("10:00:00.123") == BASE_MS + 123
    assert marks.parse_wall("23:59:59.999") == 86399999


@pytest.mark.parametrize("text", ["10:00", "25:00:00", "10:60:00",
                                  "10:00:60", "tomorrow", ""])
def test_parse_wall_rejects_bad_times(text):
    with pytest.raises(LogError):
        marks.parse_wall(text)


# --- tile logs ------------------------------------------------------------

def test_parse_log_reads_fields_and_strips_serial_prefix(tmp_path):
    path = write_log(tmp_path / "h6_tap_3.log", "3", 1000, 2000,
                     bumps={1010: 500}, prefix=True)
    log = marks.parse_log(path)
    assert log.tile == "3"
    assert log.src == "fifo"
    assert [w.start_ms for w in log.windows][:3] == [1000, 1010, 1020]
    assert log.windows[1].peak == 500
    assert log.stats == [(1000, 1000, 0)]


def test_parse_log_takes_tile_from_file_name_without_h_line(tmp_path):
    path = write_log(tmp_path / "h6_tap_9.log", "9", 0, 100, header=False)
    log = marks.parse_log(path)
    assert log.tile == "h6_tap_9"
    assert log.src == "?"


def test_parse_log_refuses_two_boots_in_one_file(tmp_path):
    path = tmp_path / "two_boots.log"
    path.write_text("H,board=3,src=fifo\nT,1000,10\nT,1010,10\n"
                    "H,board=3,src=fifo\nT,20,10\n", encoding="utf-8")
    with pytest.raises(LogError, match="two boots"):
        marks.parse_log(str(path))


# --- taps CSV -------------------------------------------------------------

def test_read_taps_skips_comments_and_reports_line_numbers(tmp_path):
    path = write_taps(tmp_path / "taps.csv", [
        ("10:00:02", "3", "c"),
        ("10:00:04.250", "5", "E"),
        ("10:00:06", "board", "k"),
    ])
    taps = marks.read_taps(path)
    assert [(t.wall_ms, t.tile, t.kind) for t in taps] == [
        (BASE_MS + 2000, "3", "c"),
        (BASE_MS + 4250, "5", "e"),
        (BASE_MS + 6000, "board", "k"),
    ]
    assert [t.line_no for t in taps] == [3, 4, 5]


@pytest.mark.parametrize("body, message", [
    ("wall,tile,kind\n10:00:02,3\n", "3 columns"),
    ("wall,tile,kind\n10:00:02,3,x\n", "not one of"),
    ("wall,tile,kind\n10:00:02,3,k\n", "knocks use tile 'board'"),
    ("wall,tile,kind\n10:00:02,board,c\n", "knocks use tile 'board'"),
    ("wall,tile,kind\n10:00:02,,c\n", "empty tile"),
    ("tile,wall,kind\n10:00:02,3,c\n", "wall,tile,kind"),
])
def test_read_taps_rejects_bad_rows(tmp_path, body, message):
    path = tmp_path / "bad.csv"
    path.write_text(body, encoding="utf-8")
    with pytest.raises(LogError, match=message):
        marks.read_taps(str(path))


# --- alignment ------------------------------------------------------------

def test_align_recovers_each_tile_clock(tmp_path):
    # Tile 3 booted 37 s before 10:00:00, tile 5 booted 12 s before it.
    # Tile ms = rel_ms + shift, so wall_ms = tile ms + (BASE_MS - shift).
    bumps3 = {}
    bump(bumps3, 2000 + 37000, 800)
    bump(bumps3, 6000 + 37000, 500)
    bumps5 = {}
    bump(bumps5, 4000 + 12000, 600)
    bump(bumps5, 6000 + 12000, 500)
    log3 = marks.parse_log(write_log(tmp_path / "h6_tap_3.log", "3",
                                     37000, 47000, bumps3))
    log5 = marks.parse_log(write_log(tmp_path / "h6_tap_5.log", "5",
                                     12000, 22000, bumps5))
    taps = marks.read_taps(write_taps(tmp_path / "taps.csv", [
        (wall_text(2000), "3", "c"),
        (wall_text(4000), "5", "e"),
        (wall_text(6000), "3", "e"),
    ]))
    assert marks.align(log3, taps) == BASE_MS - 37000
    assert marks.align(log5, taps) == BASE_MS - 12000


def test_residuals_show_a_wrong_csv_time(tmp_path):
    bumps = {}
    bump(bumps, 2000 + 37000, 800)
    bump(bumps, 6000 + 37000, 800)
    log = marks.parse_log(write_log(tmp_path / "h6_tap_3.log", "3",
                                    37000, 47000, bumps))
    # The second tap was written 300 ms late on the operator's sheet.
    taps = marks.read_taps(write_taps(tmp_path / "taps.csv", [
        (wall_text(2000), "3", "c"),
        (wall_text(6300), "3", "c"),
    ]))
    offset = marks.align(log, taps)
    residual = dict((t.wall_ms - BASE_MS, r) for t, r in
                    marks.residuals(log, taps, offset))
    assert residual[2000] == 0
    assert residual[6300] == -300


def test_align_needs_a_strong_peak_and_a_tap(tmp_path):
    quiet = marks.parse_log(write_log(tmp_path / "h6_tap_3.log", "3",
                                      37000, 39000))
    taps = marks.read_taps(write_taps(tmp_path / "taps.csv", [
        (wall_text(2000), "3", "c"),
    ]))
    with pytest.raises(LogError, match="no peak"):
        marks.align(quiet, taps)

    loud = marks.parse_log(write_log(tmp_path / "h6_tap_5.log", "5",
                                     12000, 14000, {12000: 900}))
    with pytest.raises(LogError, match="no c or e rows"):
        marks.align(loud, taps)


def test_clusters_group_runs_above_a_threshold(tmp_path):
    bumps = {}
    bump(bumps, 1000, 400, width=2)
    bump(bumps, 1500, 900, width=4)
    log = marks.parse_log(write_log(tmp_path / "h6_tap_3.log", "3",
                                    1000, 2000, bumps))
    assert marks.clusters(log, 300) == [(1000, 400), (1500, 900)]


# --- command line ---------------------------------------------------------

def test_cli_prints_offsets_and_exits_zero(tmp_path, capsys):
    bumps = {}
    bump(bumps, 2000 + 37000, 800)
    log = write_log(tmp_path / "h6_tap_3.log", "3", 37000, 47000, bumps)
    taps = write_taps(tmp_path / "taps.csv", [(wall_text(2000), "3", "c")])
    assert marks.main(["--taps", taps, log]) == 0
    out = capsys.readouterr().out
    assert "offset_ms=%d" % (BASE_MS - 37000) in out
    assert "0 beyond 500 ms" in out


def test_cli_detect_only_needs_no_taps(tmp_path, capsys):
    bumps = {}
    bump(bumps, 1500, 900)
    log = write_log(tmp_path / "h6_tap_3.log", "3", 1000, 2000, bumps)
    assert marks.main(["--detect", log]) == 0
    assert "peak at tile ms 1500: 900 mg" in capsys.readouterr().out


def test_cli_without_taps_or_detect_is_a_usage_error(tmp_path):
    log = write_log(tmp_path / "h6_tap_3.log", "3", 0, 100)
    with pytest.raises(SystemExit):
        marks.main([log])


def test_cli_returns_2_on_a_bad_taps_file(tmp_path, capsys):
    log = write_log(tmp_path / "h6_tap_3.log", "3", 0, 100)
    taps = tmp_path / "taps.csv"
    taps.write_text("wall,tile,kind\n10:00:02,3,q\n", encoding="utf-8")
    assert marks.main(["--taps", str(taps), log]) == 2
    assert "not one of" in capsys.readouterr().err
