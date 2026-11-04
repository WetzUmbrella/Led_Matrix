"""pytest suite for pc/tools/pico_tap_capture.py on synthetic streams."""

from __future__ import annotations

import io
import os
import sys
from typing import List, Tuple

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import pico_tap_capture as ptc  # noqa: E402
import skew_analyse as sa  # noqa: E402

WRAP = 1 << 32


def csv_text(lines, channels: int) -> str:
    cap = ptc.convert(lines, channels)
    out = io.StringIO()
    ptc.write_csv(cap, out)
    return out.getvalue()


def test_small_stream_to_csv() -> None:
    lines = ["T,1000,0", "T,1500,1", "T,2250,3", "T,3000,2"]
    assert csv_text(lines, 2).splitlines() == [
        "Time [s],Channel 0,Channel 1",
        "0.000000,0,0",
        "0.000500,1,0",
        "0.001250,1,1",
        "0.002000,0,1",
    ]


def test_first_row_is_initial_state_at_zero() -> None:
    text = csv_text(["T,987654,5"], 3)
    assert text.splitlines()[1] == "0.000000,1,0,1"


def test_time_has_six_decimals_without_float_error() -> None:
    text = csv_text(["T,0,0", "T,1234567,1"], 1)
    assert text.splitlines()[2] == "1.234567,1"


def test_wrap_of_the_32_bit_timer_is_unwrapped() -> None:
    lines = [f"T,{WRAP - 500},0", "T,300,1", "T,1300,0"]
    cap = ptc.convert(lines, 1)
    assert [r[0] for r in cap.rows] == [0, 800, 1800]
    assert cap.duration_us == 1800


def test_two_wraps() -> None:
    lines = [f"T,{WRAP - 10},0", "T,5,1", f"T,{WRAP - 1},0", "T,2,1"]
    cap = ptc.convert(lines, 1)
    assert [r[0] for r in cap.rows] == [0, 15, 15 + WRAP - 6, 15 + WRAP - 3]


def test_comments_are_ignored_and_dropped_is_warned_and_counted() -> None:
    warnings: List[str] = []
    lines = ["# pico tap v1", "T,0,0", "# stats dropped=0", "T,10,1",
             "# stats dropped=3 max_q=12", "# dropped=4"]
    cap = ptc.convert(lines, 1, warnings.append)
    assert cap.dropped == 7
    assert len(warnings) == 2 and "dropped=3" in warnings[0]
    assert cap.events == 2 and cap.malformed == 0


def test_dropped_zero_gives_no_warning() -> None:
    warnings: List[str] = []
    cap = ptc.convert(["# dropped=0"], 1, warnings.append)
    assert warnings == [] and cap.dropped == 0


@pytest.mark.parametrize("bad", [
    "T,abc,1", "T,100", "T,100,1,2", "X,100,1", "T,100,-1", "T,-5,1",
    f"T,{WRAP},1", "garbage", "T,1.5,1", "T,,",
])
def test_malformed_lines_are_counted_and_skipped(bad: str) -> None:
    cap = ptc.convert(["T,0,0", bad, "T,10,1"], 1)
    assert cap.malformed == 1
    assert cap.events == 2
    assert [r[0] for r in cap.rows] == [0, 10]


def test_blank_lines_are_not_malformed() -> None:
    cap = ptc.convert(["", "  ", "T,0,0"], 1)
    assert cap.malformed == 0 and cap.events == 1


def test_only_first_n_channels_decide_changes() -> None:
    # Bit 2 toggles alone at 20 and 40: no new row for 2 channels.
    lines = ["T,0,0", "T,20,4", "T,30,5", "T,40,1", "T,50,3"]
    cap = ptc.convert(lines, 2)
    assert [r[0] for r in cap.rows] == [0, 30, 50]
    assert [r[1] for r in cap.rows] == [(0, 0), (1, 0), (1, 1)]
    assert cap.events == 5
    text = csv_text(lines, 2)
    assert "Channel 2" not in text


def test_rising_edges_per_channel() -> None:
    lines = ["T,0,0", "T,1,1", "T,2,0", "T,3,1", "T,4,3", "T,5,2"]
    cap = ptc.convert(lines, 2)
    assert cap.rising == [2, 1]


def test_initial_high_is_not_a_rising_edge() -> None:
    cap = ptc.convert(["T,0,1", "T,5,0"], 1)
    assert cap.rising == [0]


def test_leading_serial_log_stamp_is_accepted() -> None:
    lines = ["# test=D1 date=2026-11-05", "09:30:01.002 T,100,0",
             "09:30:01.003 T,200,1", "# end lines=2"]
    cap = ptc.convert(lines, 1)
    assert cap.events == 2 and cap.malformed == 0


def test_summary_text() -> None:
    cap = ptc.convert(["T,0,0", "T,1000000,3", "oops", "# dropped=2"], 2)
    text = ptc.summary(cap)
    assert text == ("events=2 rows=2 rising_edges: ch0=1 ch1=1 malformed=1 "
                    "dropped=2 duration_s=1.000000")


def test_main_from_log_writes_csv_and_summary(tmp_path, capsys) -> None:
    log = tmp_path / "raw.log"
    log.write_text("# banner\nT,5,0\nT,15,1\nbad\n# dropped=1\n")
    out = tmp_path / "deep" / "o.csv"
    rc = ptc.main(["--from-log", str(log), "--channels", "1", "--out",
                   str(out)])
    err = capsys.readouterr().err
    assert rc == 0
    assert out.read_text().splitlines() == [
        "Time [s],Channel 0", "0.000000,0", "0.000010,1"]
    assert "WARNING" in err and "malformed=1" in err and "dropped=1" in err


def test_main_rejects_bad_channel_count_and_empty_input(tmp_path) -> None:
    log = tmp_path / "raw.log"
    log.write_text("# nothing\n")
    out = tmp_path / "o.csv"
    assert ptc.main(["--from-log", str(log), "--channels", "6",
                     "--out", str(out)]) == ptc.EXIT_BAD_INPUT
    assert ptc.main(["--from-log", str(log), "--channels", "2",
                     "--out", str(out)]) == ptc.EXIT_NO_EVENTS
    assert not out.exists()
    assert ptc.main(["--from-log", str(tmp_path / "missing"), "--channels",
                     "2", "--out", str(out)]) == ptc.EXIT_BAD_INPUT


def synthetic_stream(base_low32: int, delay_us: int = 300,
                     ticks: int = 200) -> List[str]:
    """Two channels: 1 ms pulses every 16667 us, 4 ms marker every 64 ticks,
    channel 1 delayed by delay_us. Starts with the idle state."""
    edges: List[Tuple[int, int, int]] = []  # (t, channel, level)
    for n in range(ticks):
        width = 4000 if n % 64 == 0 else 1000
        for ch, delay in ((0, 0), (1, delay_us)):
            t = 5000 + n * 16667 + delay
            edges.append((t, ch, 1))
            edges.append((t + width, ch, 0))
    edges.sort()
    mask = 0
    lines = ["# pico tap synthetic", f"T,{base_low32 % WRAP},0"]
    for t, ch, level in edges:
        mask = (mask | (1 << ch)) if level else (mask & ~(1 << ch))
        lines.append(f"T,{(base_low32 + t) % WRAP},{mask}")
    lines.append("# stats dropped=0")
    return lines


@pytest.mark.parametrize("base", [0, WRAP - 1_000_000])
def test_end_to_end_skew_of_300_us(base: int, tmp_path, capsys) -> None:
    cap_path = tmp_path / "skew.csv"
    raw = tmp_path / "raw.log"
    raw.write_text("\n".join(synthetic_stream(base)) + "\n")
    assert ptc.main(["--from-log", str(raw), "--channels", "2", "--out",
                     str(cap_path)]) == 0

    lines = cap_path.read_text().splitlines()
    assert lines[0] == "Time [s],Channel 0,Channel 1"
    times, va, vb = sa.read_saleae(lines, "Channel 0", "Channel 1")
    res = sa.analyse_marker(times, va, vb)
    assert res["passed"]
    assert len(res["pairs"]) == 200
    assert res["max_us"] == pytest.approx(300, abs=2)
    assert res["median_us"] == pytest.approx(300, abs=2)

    out_pairs = tmp_path / "pairs.csv"
    rc = sa.main([str(cap_path), "--ch-a", "Channel 0", "--ch-b", "Channel 1",
                  "--marker", "--out", str(out_pairs)])
    assert rc == sa.EXIT_PASS
    assert "max_abs_skew_ms=0.300" in capsys.readouterr().out
