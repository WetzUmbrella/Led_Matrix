#!/usr/bin/env python3
"""Summarise imu_tap_log tile logs against the operator's tap times.

Usage:
  python3 summarise.py --taps taps.csv [options] LOG [LOG ...]
  python3 summarise.py --taps taps.csv --neighbours "3-5,3-4" LOG ...

The taps CSV and the alignment are described in marks.py. Each tile is put
on the wall clock by marks.align(), and every window (a T line, 10 ms) is
then given one class:

  tap      within tol of a centre or edge tap on this tile (c, e)
  press    within tol of a Blu Tack press on this tile (b)
  nbr      within tol of a tap, press on a neighbour tile (c, e, b)
  knock    within tol of a board knock (k)
  far      within tol of a tap on a tile that is not a neighbour
  quiet    none of the above (includes the noise-floor stretch)

A trigger is a window at or above the threshold, where windows less than
dedupe-ms apart form one trigger (the highest peak of the run). A trigger in
a class other than tap or far is a false trigger. Neighbours default to every
other tile; --neighbours limits them to the listed pairs.

Printed: per tile the sample rate (from S lines), overruns (missed samples in
src=poll, FIFO overflow events in src=fifo) and window gaps; the peaks per
event (the highest window within tol of the event); false triggers per tile
and the share of taps detected for a sweep of thresholds; and a suggested
threshold: the lowest one with no false trigger on any tile that still
detects at least --detect-min percent of taps.

Exits 0 on success, 2 when a file or the alignment cannot be used.
"""

import argparse
import bisect
import sys
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Sequence, Set, Tuple

from marks import (ALIGN_MG, BOARD_TILE, TAP_KINDS, TOL_MS, WINDOW_MS,
                   LogError, TileLog, Tap, align, aligned_windows,
                   fmt_wall, parse_log, read_taps, residuals)

DEDUPE_MS = 50
MG_MIN = 50
MG_MAX = 1000
MG_STEP = 25
DETECT_MIN = 95   # percent of taps
FALSE_CLASSES = ("press", "nbr", "knock", "quiet")
CLASS_ORDER = ("tap", "press", "nbr", "knock", "far")


@dataclass
class Analysis:
    tile: str
    offset: int
    rates: List[float]
    s_lines: int
    overruns: int
    gaps: int
    resid_max: Optional[int]
    walls: List[int]                    # wall clock, ms
    peaks: List[int]                    # mg
    cls: List[str]                      # class per window
    tap_peaks: List[Optional[int]] = field(default_factory=list)
    press_peaks: List[Optional[int]] = field(default_factory=list)
    nbr_peaks: List[Optional[int]] = field(default_factory=list)
    knock_peaks: List[Optional[int]] = field(default_factory=list)


def near(sorted_walls: Sequence[int], wall: int, tol: int) -> bool:
    """True when some event in sorted_walls is within tol of wall."""
    idx = bisect.bisect_right(sorted_walls, wall - tol)
    return idx < len(sorted_walls) and sorted_walls[idx] < wall + tol


def event_peak(walls: Sequence[int], peaks: Sequence[int], event: int,
               tol: int) -> Optional[int]:
    """Highest window peak within tol of an event, or None if no window."""
    lo = bisect.bisect_right(walls, event - tol)
    hi = bisect.bisect_left(walls, event + tol)
    return max(peaks[lo:hi]) if hi > lo else None


def window_stats(log: TileLog) -> Tuple[List[float], int, int]:
    """Sample rate per S interval (Hz), overruns, and window gaps."""
    rates = []
    for (ms0, _, _), (ms1, samples, _) in zip(log.stats, log.stats[1:]):
        if ms1 > ms0:
            rates.append(samples * 1000.0 / (ms1 - ms0))
    overruns = sum(over for _, _, over in log.stats)
    starts = [w.start_ms for w in log.windows]
    gaps = sum(max(0, (b - a) // WINDOW_MS - 1)
               for a, b in zip(starts, starts[1:]))
    return rates, overruns, gaps


def analyse(log: TileLog, taps: Sequence[Tap], align_mg: int, tol: int,
            neighbours: Optional[Dict[str, Set[str]]]) -> Analysis:
    offset = align(log, taps, align_mg)
    walls, peaks = aligned_windows(log, offset)
    tile = log.tile

    def is_neighbour(other: str) -> bool:
        if neighbours is None:
            return True
        return other in neighbours.get(tile, set())

    own_tap = [t for t in taps if t.tile == tile and t.kind in TAP_KINDS]
    own_press = [t for t in taps if t.tile == tile and t.kind == "b"]
    knocks = [t for t in taps if t.kind == "k"]
    nbr = [t for t in taps if t.tile not in (tile, BOARD_TILE)
           and t.kind != "k" and is_neighbour(t.tile)]
    far = [t for t in taps if t.tile not in (tile, BOARD_TILE)
           and t.kind != "k" and not is_neighbour(t.tile)]

    lists = {
        "tap": sorted(t.wall_ms for t in own_tap),
        "press": sorted(t.wall_ms for t in own_press),
        "nbr": sorted(t.wall_ms for t in nbr),
        "knock": sorted(t.wall_ms for t in knocks),
        "far": sorted(t.wall_ms for t in far),
    }
    cls = []
    for wall in walls:
        label = "quiet"
        for name in CLASS_ORDER:
            if near(lists[name], wall, tol):
                label = name
                break
        cls.append(label)

    rates, overruns, gaps = window_stats(log)
    resid = [r for _, r in residuals(log, taps, offset, align_mg)
             if r is not None]
    return Analysis(
        tile=tile, offset=offset, rates=rates,
        s_lines=len(log.stats), overruns=overruns, gaps=gaps,
        resid_max=max((abs(r) for r in resid), default=None),
        walls=walls, peaks=peaks, cls=cls,
        tap_peaks=[event_peak(walls, peaks, t.wall_ms, tol) for t in own_tap],
        press_peaks=[event_peak(walls, peaks, t.wall_ms, tol)
                     for t in own_press],
        nbr_peaks=[event_peak(walls, peaks, t.wall_ms, tol) for t in nbr],
        knock_peaks=[event_peak(walls, peaks, t.wall_ms, tol)
                     for t in knocks],
    )


def false_triggers(a: Analysis, threshold: int, dedupe: int) -> int:
    """Triggers outside own-tap windows and far windows at a threshold."""
    count = 0
    last: Optional[int] = None
    for wall, peak, label in zip(a.walls, a.peaks, a.cls):
        if peak >= threshold:
            if (last is None or wall - last > dedupe) and \
                    label in FALSE_CLASSES:
                count += 1
            last = wall
    return count


def detected(a: Analysis, threshold: int) -> int:
    return sum(1 for p in a.tap_peaks if p is not None and p >= threshold)


def quiet_peaks(a: Analysis) -> List[int]:
    return [p for p, c in zip(a.peaks, a.cls) if c == "quiet"]


def nearest_rank(sorted_vals: List[int], pct: int) -> int:
    rank = max(1, min(len(sorted_vals),
                      -(-pct * len(sorted_vals) // 100)))
    return sorted_vals[rank - 1]


def fmt(value: Optional[float], spec: str = "%d") -> str:
    return "-" if value is None else spec % value


def natural(name: str) -> Tuple[int, str]:
    return (len(name), name)


def parse_pairs(text: str) -> Dict[str, Set[str]]:
    """'3-5,3-4' to {'3': {'5','4'}, '5': {'3'}, '4': {'3'}}."""
    pairs: Dict[str, Set[str]] = {}
    for item in text.split(","):
        item = item.strip()
        if not item:
            continue
        parts = item.split("-")
        if len(parts) != 2 or not parts[0] or not parts[1]:
            raise LogError("neighbour pair %r, want A-B" % item)
        a, b = parts
        pairs.setdefault(a, set()).add(b)
        pairs.setdefault(b, set()).add(a)
    return pairs


def print_tile_table(results: List[Analysis]) -> None:
    print("Per tile: sample rate from S lines (Hz), missed samples")
    print("(overruns: samples in src=poll, FIFO overflow events in "
          "src=fifo), window gaps (10 ms windows missing), align offset, "
          "worst tap residual.")
    print("boot_wall is the wall-clock time of the tile's ms 0.")
    print("%-6s %10s %10s %8s %9s %6s %13s %9s"
          % ("tile", "rate_med", "rate_min", "s_lines", "overruns",
             "gaps", "boot_wall", "resid_ms"))
    for a in results:
        med = min_rate = None
        if a.rates:
            ordered = sorted(a.rates)
            med = ordered[len(ordered) // 2]
            min_rate = ordered[0]
        print("%-6s %10s %10s %8d %9d %6d %13s %9s"
              % (a.tile, fmt(med, "%.1f"), fmt(min_rate, "%.1f"),
                 a.s_lines, a.overruns, a.gaps, fmt_wall(a.offset),
                 fmt(a.resid_max)))
    print()


def print_peak_table(results: List[Analysis]) -> None:
    print("Peaks in mg. Per event = highest window within tol of the event.")
    print("%-6s %5s %7s %7s %7s %9s %8s %9s %9s %8s"
          % ("tile", "taps", "tap_min", "tap_med", "tap_max", "press_max",
             "nbr_max", "knock_max", "quiet_p99", "quiet_max"))
    for a in results:
        tap = sorted(p for p in a.tap_peaks if p is not None)
        quiet = sorted(quiet_peaks(a))
        print("%-6s %5d %7s %7s %7s %9s %8s %9s %9s %8s"
              % (a.tile, len(a.tap_peaks),
                 fmt(tap[0] if tap else None),
                 fmt(tap[len(tap) // 2] if tap else None),
                 fmt(tap[-1] if tap else None),
                 fmt(max((p for p in a.press_peaks if p is not None),
                         default=None)),
                 fmt(max((p for p in a.nbr_peaks if p is not None),
                         default=None)),
                 fmt(max((p for p in a.knock_peaks if p is not None),
                         default=None)),
                 fmt(nearest_rank(quiet, 99) if quiet else None),
                 fmt(quiet[-1] if quiet else None)))
    print()


def print_sweep(results: List[Analysis], thresholds: List[int], dedupe: int,
                detect_min: int) -> Optional[int]:
    tiles = [a.tile for a in results]
    total_taps = sum(len(a.tap_peaks) for a in results)
    print("Sweep: false triggers per tile (outside own taps, dedupe %d ms)"
          % dedupe)
    header = "%6s " % "mg" + " ".join("%6s" % ("t" + t) for t in tiles)
    print(header + " %9s" % "detected")
    suggested = None
    for threshold in thresholds:
        counts = [false_triggers(a, threshold, dedupe) for a in results]
        det = sum(detected(a, threshold) for a in results)
        share = 100.0 * det / total_taps if total_taps else 0.0
        print("%6d " % threshold
              + " ".join("%6d" % c for c in counts)
              + " %8.1f%%" % share)
        if suggested is None and sum(counts) == 0 and share >= detect_min:
            suggested = threshold
    print()
    return suggested


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Summarise imu_tap_log tile logs against the taps CSV.")
    parser.add_argument("logs", nargs="+", help="tile log files")
    parser.add_argument("--taps", required=True,
                        help="taps CSV (see marks.py)")
    parser.add_argument("--neighbours",
                        help="neighbour pairs, e.g. 3-5,3-4 (default: every "
                        "other tile)")
    parser.add_argument("--align-mg", type=int, default=ALIGN_MG)
    parser.add_argument("--tol-ms", type=int, default=TOL_MS)
    parser.add_argument("--dedupe-ms", type=int, default=DEDUPE_MS)
    parser.add_argument("--mg-min", type=int, default=MG_MIN)
    parser.add_argument("--mg-max", type=int, default=MG_MAX)
    parser.add_argument("--mg-step", type=int, default=MG_STEP)
    parser.add_argument("--detect-min", type=int, default=DETECT_MIN,
                        help="percent of taps to detect (default %d)"
                        % DETECT_MIN)
    args = parser.parse_args(argv)

    if args.mg_step <= 0 or args.mg_min > args.mg_max:
        parser.error("need --mg-step > 0 and --mg-min <= --mg-max")

    try:
        taps = read_taps(args.taps)
        neighbours = (parse_pairs(args.neighbours)
                      if args.neighbours else None)
        logs = [parse_log(path) for path in args.logs]
        names = [log.tile for log in logs]
        if len(set(names)) != len(names):
            raise LogError("two logs have the same tile (%s); each tile "
                           "needs one log" % ", ".join(sorted(names)))
        results = [analyse(log, taps, args.align_mg, args.tol_ms, neighbours)
                   for log in sorted(logs, key=lambda l: natural(l.tile))]
    except (LogError, OSError) as exc:
        sys.stderr.write("summarise.py: %s\n" % exc)
        return 2

    thresholds = list(range(args.mg_min, args.mg_max + 1, args.mg_step))
    print_tile_table(results)
    print_peak_table(results)
    suggested = print_sweep(results, thresholds, args.dedupe_ms,
                            args.detect_min)

    tap_min = min((p for a in results for p in a.tap_peaks
                   if p is not None), default=None)
    other = [p for a in results
             for p in a.press_peaks + a.nbr_peaks + a.knock_peaks
             + quiet_peaks(a) if p is not None]
    other_max = max(other, default=None)
    print("Highest non-tap peak: %s mg. Lowest tap peak: %s mg."
          % (fmt(other_max), fmt(tap_min)))
    if suggested is not None:
        print("Suggested threshold: %d mg (no false trigger on any tile, "
              "at least %d%% of taps detected)." % (suggested,
                                                   args.detect_min))
    else:
        print("No threshold in the sweep has zero false triggers and at "
              "least %d%% of taps detected. Read the sweep table and choose "
              "by hand, or log a longer quiet stretch." % args.detect_min)
    return 0


if __name__ == "__main__":
    sys.exit(main())
