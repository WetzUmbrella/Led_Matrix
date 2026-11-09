#!/usr/bin/env python3
"""Put imu_tap_log tile logs on one wall clock, using the operator's tap times.

Each tile counts milliseconds from its own boot, so the clocks are not synced
and a T line from one tile cannot be compared with another tile's, or with
the operator's notes, until the log is aligned. This module does that.

Inputs
  taps CSV  header line "wall,tile,kind", then one row per action:
              wall  local time of the action, HH:MM:SS or HH:MM:SS.mmm
              tile  the board number exactly as in the log's H line
                    (board=<n>), or "board" for a knock on the board
              kind  c centre tap, e edge tap, k board knock,
                    b Blu Tack press
            Lines starting with # are comments and are skipped.
  tile log  a file written by pc/tools/serial_log.py (its HH:MM:SS.mmm
            prefix is ignored) or raw imu_tap_log output.

Alignment, per tile
  offset_ms = wall time of the tile's first centre or edge tap row
              - ms of the tile's first strong peak (T line, peak >= align_mg)
  wall_ms   = tile ms + offset_ms

So the first strong peak in each log must be the first tap: start every run
with a few seconds of stillness, and do not knock the board before the first
tap. Tile crystals drift by tens of ppm, which is a few milliseconds over a
run, so one offset per log is enough. The residual check compares every tap
row of the tile with the nearest strong peak; a large residual means the
wall times in the CSV are off, or the first peak was not the first tap.

Usage
  python3 marks.py --taps taps.csv docs/bench/6nov/h6_tap_*.log
  python3 marks.py --detect docs/bench/6nov/h6_tap_3.log   # list the peaks
Exit status 0 on success, 2 when a file or the alignment cannot be used.
"""

import argparse
import csv
import os
import re
import sys
from dataclasses import dataclass
from typing import List, Optional, Sequence, Tuple

WINDOW_MS = 10
ALIGN_MG = 300     # first peak at or above this marks the start of a log
TOL_MS = 500       # a window belongs to an event within this many ms

TAP_KINDS = ("c", "e")                       # deliberate taps on the tile
EVENT_KINDS = {
    "c": "centre tap",
    "e": "edge tap",
    "b": "Blu Tack press",
    "k": "board knock",
}
BOARD_TILE = "board"

STAMP_RE = re.compile(r"^\d{2}:\d{2}:\d{2}\.\d{3} ")  # serial_log.py prefix
H_RE = re.compile(r"^H,(.*)$")
T_RE = re.compile(r"^T,([0-9]+),([0-9]+)$")
S_RE = re.compile(r"^S,([0-9]+),([0-9]+),([0-9]+)$")
WALL_RE = re.compile(r"^(\d{1,2}):(\d{2}):(\d{2})(?:\.(\d{1,3}))?$")


class LogError(ValueError):
    """A file, row or alignment that cannot be used as given."""


@dataclass
class Window:
    start_ms: int    # tile ms (window start)
    peak: int        # mg, deviation from the slow baseline


@dataclass
class TileLog:
    path: str
    tile: str                                  # board= from the H line
    src: str                                   # fifo or poll; "?" if no H
    windows: List[Window]
    stats: List[Tuple[int, int, int]]          # (ms, samples, overruns) per S


@dataclass
class Tap:
    wall_ms: int     # ms since local midnight
    tile: str
    kind: str
    line_no: int


def parse_wall(text: str) -> int:
    """HH:MM:SS or HH:MM:SS.mmm, to ms since local midnight."""
    match = WALL_RE.match(text.strip())
    if not match:
        raise LogError("bad wall time %r, want HH:MM:SS or HH:MM:SS.mmm"
                       % text)
    hour, minute, second = (int(match.group(i)) for i in (1, 2, 3))
    if hour > 23 or minute > 59 or second > 59:
        raise LogError("wall time %r is out of range" % text)
    frac = match.group(4) or ""
    millis = int(frac.ljust(3, "0")) if frac else 0
    return ((hour * 60 + minute) * 60 + second) * 1000 + millis


def fmt_wall(ms: int) -> str:
    """ms since local midnight as HH:MM:SS.mmm (wraps at 24 h)."""
    ms %= 86400000
    return "%02d:%02d:%02d.%03d" % (ms // 3600000, (ms // 60000) % 60,
                                   (ms // 1000) % 60, ms % 1000)


def parse_log(path: str) -> TileLog:
    """Read one tile log. A log that holds two boots is refused."""
    tile: Optional[str] = None
    src = "?"
    windows: List[Window] = []
    stats: List[Tuple[int, int, int]] = []
    last_t: Optional[int] = None
    last_s: Optional[int] = None

    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line_no, raw in enumerate(handle, 1):
            line = STAMP_RE.sub("", raw.rstrip("\r\n"), count=1)

            match = H_RE.match(line)
            if match:
                fields = dict(part.split("=", 1)
                              for part in match.group(1).split(",")
                              if "=" in part)
                if tile is None and "board" in fields:
                    tile = fields["board"]
                src = fields.get("src", src)
                continue

            match = T_RE.match(line)
            if match:
                start = int(match.group(1))
                if last_t is not None and start < last_t:
                    raise LogError("%s:%d: time goes back, so the file holds "
                                   "two boots; split it per boot"
                                   % (path, line_no))
                last_t = start
                windows.append(Window(start, int(match.group(2))))
                continue

            match = S_RE.match(line)
            if match:
                ms = int(match.group(1))
                if last_s is not None and ms < last_s:
                    raise LogError("%s:%d: S line time goes back"
                                   % (path, line_no))
                last_s = ms
                stats.append((ms, int(match.group(2)), int(match.group(3))))

    if tile is None:
        tile = os.path.splitext(os.path.basename(path))[0]
    return TileLog(path, tile, src, windows, stats)


def read_taps(path: str) -> List[Tap]:
    """Read and check the taps CSV."""
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        kept = [(n, row) for n, row in enumerate(handle, 1)
                if row.strip() and not row.lstrip().startswith("#")]
    reader = csv.reader(row for _, row in kept)
    header = next(reader, None)
    if header is None or [c.strip().lower() for c in header] != [
            "wall", "tile", "kind"]:
        raise LogError("%s: the first row must be wall,tile,kind" % path)

    taps: List[Tap] = []
    for record in reader:
        line_no = kept[reader.line_num - 1][0]  # file line, comments counted
        if len(record) != 3:
            raise LogError("%s:%d: want 3 columns" % (path, line_no))
        wall, tile, kind = (c.strip() for c in record)
        kind = kind.lower()
        if kind not in EVENT_KINDS:
            raise LogError("%s:%d: kind %r is not one of c, e, k, b"
                           % (path, line_no, kind))
        if not tile:
            raise LogError("%s:%d: empty tile" % (path, line_no))
        if (kind == "k") != (tile == BOARD_TILE):
            raise LogError("%s:%d: knocks use tile 'board' and only "
                           "knocks do" % (path, line_no))
        taps.append(Tap(parse_wall(wall), tile, kind, line_no))
    return taps


def own_taps(taps: Sequence[Tap], tile: str) -> List[Tap]:
    """The centre and edge taps on one tile, in time order."""
    return sorted((t for t in taps if t.tile == tile and t.kind in TAP_KINDS),
                  key=lambda t: t.wall_ms)


def first_strong(log: TileLog, align_mg: int) -> Optional[Window]:
    for window in log.windows:
        if window.peak >= align_mg:
            return window
    return None


def clusters(log: TileLog, mg: int) -> List[Tuple[int, int]]:
    """Runs of windows at or above mg, as (first window ms, highest peak)."""
    found: List[Tuple[int, int]] = []
    current: Optional[List[int]] = None
    for window in log.windows:
        if window.peak >= mg:
            if current is None:
                current = [window.start_ms, window.peak]
            else:
                current[1] = max(current[1], window.peak)
        elif current is not None:
            found.append((current[0], current[1]))
            current = None
    if current is not None:
        found.append((current[0], current[1]))
    return found


def align(log: TileLog, taps: Sequence[Tap],
          align_mg: int = ALIGN_MG) -> int:
    """Offset in ms such that wall_ms = tile ms + offset."""
    own = own_taps(taps, log.tile)
    if not own:
        raise LogError("tile %s: no c or e rows in the taps file"
                       % log.tile)
    peak = first_strong(log, align_mg)
    if peak is None:
        raise LogError("tile %s: no peak of %d mg or more in %s; lower "
                       "--align-mg" % (log.tile, align_mg, log.path))
    return own[0].wall_ms - peak.start_ms


def residuals(log: TileLog, taps: Sequence[Tap], offset: int,
              align_mg: int = ALIGN_MG) -> List[Tuple[Tap, Optional[int]]]:
    """For each c or e row of the tile: the nearest strong peak minus the
    row's wall time, in ms (None when the log has no strong peak)."""
    peaks = [start + offset for start, _ in clusters(log, align_mg)]
    result: List[Tuple[Tap, Optional[int]]] = []
    for tap in own_taps(taps, log.tile):
        if peaks:
            nearest = min(peaks, key=lambda p: abs(p - tap.wall_ms))
            result.append((tap, nearest - tap.wall_ms))
        else:
            result.append((tap, None))
    return result


def aligned_windows(log: TileLog, offset: int) -> Tuple[List[int], List[int]]:
    """Window times on the wall clock (ms) and their peaks, in order."""
    walls = [w.start_ms + offset for w in log.windows]
    peaks = [w.peak for w in log.windows]
    return walls, peaks


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Align imu_tap_log tile logs to the operator's taps.")
    parser.add_argument("logs", nargs="+", help="tile log files")
    parser.add_argument("--taps", help="taps CSV (see the module docstring)")
    parser.add_argument("--detect", action="store_true",
                        help="also list each log's peak clusters")
    parser.add_argument("--align-mg", type=int, default=ALIGN_MG,
                        help="peak that marks a log's first tap (default %d)"
                        % ALIGN_MG)
    parser.add_argument("--tol-ms", type=int, default=TOL_MS,
                        help="residual limit in ms (default %d)" % TOL_MS)
    args = parser.parse_args(argv)

    if args.taps is None and not args.detect:
        parser.error("give --taps, or --detect to list peaks only")

    try:
        taps = read_taps(args.taps) if args.taps else []
        for path in args.logs:
            log = parse_log(path)
            print("tile %s (%s): %d windows, src=%s"
                  % (log.tile, os.path.basename(path), len(log.windows),
                     log.src))
            if args.detect:
                for start, peak in clusters(log, args.align_mg):
                    print("  peak at tile ms %d: %d mg" % (start, peak))
            if args.taps is None:
                continue
            offset = align(log, taps, args.align_mg)
            print("  offset_ms=%d: tile ms 0 is wall %s (first strong peak "
                  "at tile ms %d)"
                  % (offset, fmt_wall(offset),
                     first_strong(log, args.align_mg).start_ms))
            rows = residuals(log, taps, offset, args.align_mg)
            bad = [(t, r) for t, r in rows
                   if r is None or abs(r) > args.tol_ms]
            known = [abs(r) for _, r in rows if r is not None]
            print("  %d tap rows, worst residual %s ms, %d beyond %d ms"
                  % (len(rows), max(known) if known else "-", len(bad),
                     args.tol_ms))
            for tap, res in bad[:5]:
                print("    line %d: wall %d ms (%s), residual %s"
                      % (tap.line_no, tap.wall_ms, tap.kind,
                         "none" if res is None else "%d ms" % res))
    except (LogError, OSError) as exc:
        sys.stderr.write("marks.py: %s\n" % exc)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
