#!/usr/bin/env python3
"""Turn a Pico used as a home-made logic analyser into a Saleae style CSV.

The Pico prints one line per edge on its input pins:

    T,<t_us_low32>,<mask>

``t_us_low32`` is the low 32 bits of a microsecond timer (it wraps at 2^32,
about 71.6 minutes, and is unwrapped here: a value smaller than the previous
one adds 2^32). ``mask`` is a decimal integer whose bit i is the level of
channel i after the edge. Lines starting with ``#`` are comments (banner,
statistics) and are ignored, except that a comment containing ``dropped=<n>``
with n > 0 is reported on stderr as a warning and added to the dropped total.
Blank lines are ignored; any other line is counted as malformed and skipped.

Output is the CSV that ``pc/tools/skew_analyse.py`` reads (Saleae Logic 2
digital export): header ``Time [s],Channel 0,Channel 1,...``, time in seconds
relative to the first event with 6 decimals, the first row being the first
event's levels (initial state, t = 0.000000), then one row each time one of
the first N channels changes. Values are 0 or 1. Other mask bits are ignored.

A summary goes to stderr: events, rows written, rising edges per channel,
malformed lines, dropped total and duration.

Usage:
    python pc/tools/pico_tap_capture.py --port COM7 --channels 2 \\
        --seconds 600 --out docs/week10/skew_2tile/skew_2tile.csv
    python pc/tools/pico_tap_capture.py --from-log raw.log --channels 2 \\
        --out skew_2tile.csv

``--from-log`` converts a saved raw log instead of reading a port (no
pyserial needed); a log written by ``serial_log.py`` also works, because a
leading ``HH:MM:SS.mmm`` stamp is accepted. Stop a live capture early with
Ctrl+C; the CSV is still written.

Exit codes: 0 done, 1 no events found, 2 bad input.

Standard library plus pyserial (imported only when the port is opened).
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from dataclasses import dataclass, field
from typing import Callable, Iterable, List, Optional, TextIO, Tuple

import serial_log

EXIT_OK = 0
EXIT_NO_EVENTS = 1
EXIT_BAD_INPUT = 2

MIN_CHANNELS = 1
MAX_CHANNELS = 5
DEFAULT_BAUD = 115200
WRAP = 1 << 32

#: A stamp added by serial_log.py: "HH:MM:SS.mmm ".
STAMP_RE = re.compile(r"^\d{2}:\d{2}:\d{2}\.\d{3} ")
DROPPED_RE = re.compile(r"dropped=(\d+)")


@dataclass
class Capture:
    """The result of converting one stream."""

    channels: int
    rows: List[Tuple[int, Tuple[int, ...]]] = field(default_factory=list)
    events: int = 0
    malformed: int = 0
    dropped: int = 0
    rising: List[int] = field(default_factory=list)
    duration_us: int = 0


def parse_event(line: str) -> Optional[Tuple[int, int]]:
    """Return (t_us_low32, mask) for a 'T,<t>,<mask>' line, else None."""
    parts = line.split(",")
    if len(parts) != 3 or parts[0] != "T":
        return None
    try:
        t_low, mask = int(parts[1]), int(parts[2])
    except ValueError:
        return None
    if not 0 <= t_low < WRAP or mask < 0:
        return None
    return t_low, mask


def format_time(t_us: int) -> str:
    """Seconds with 6 decimals, from integer microseconds (no rounding)."""
    return f"{t_us // 1_000_000}.{t_us % 1_000_000:06d}"


def levels(mask: int, channels: int) -> Tuple[int, ...]:
    return tuple((mask >> i) & 1 for i in range(channels))


def convert(lines: Iterable[str], channels: int,
            on_warning: Optional[Callable[[str], None]] = None) -> Capture:
    """Convert raw Pico lines to CSV rows (pure apart from on_warning)."""
    cap = Capture(channels=channels, rising=[0] * channels)
    offset = 0
    previous_low: Optional[int] = None
    first_us = 0
    last_levels: Optional[Tuple[int, ...]] = None
    for raw in lines:
        line = STAMP_RE.sub("", raw.strip(), count=1).strip()
        if not line:
            continue
        if line.startswith("#"):
            found = DROPPED_RE.search(line)
            if found and int(found.group(1)) > 0:
                cap.dropped += int(found.group(1))
                if on_warning:
                    on_warning(f"WARNING: Pico reports dropped events: {line}")
            continue
        event = parse_event(line)
        if event is None:
            cap.malformed += 1
            continue
        t_low, mask = event
        if previous_low is not None and t_low < previous_low:
            offset += WRAP
        previous_low = t_low
        t_us = t_low + offset
        if cap.events == 0:
            first_us = t_us
        cap.events += 1
        rel_us = t_us - first_us
        cap.duration_us = rel_us
        now = levels(mask, channels)
        if now == last_levels:
            continue
        if last_levels is not None:
            for i in range(channels):
                if last_levels[i] == 0 and now[i] == 1:
                    cap.rising[i] += 1
        cap.rows.append((rel_us, now))
        last_levels = now
    return cap


def write_csv(cap: Capture, out: TextIO) -> None:
    header = ["Time [s]"] + [f"Channel {i}" for i in range(cap.channels)]
    out.write(",".join(header) + "\n")
    for rel_us, values in cap.rows:
        out.write(",".join([format_time(rel_us)]
                           + [str(v) for v in values]) + "\n")


def summary(cap: Capture) -> str:
    rising = " ".join(f"ch{i}={n}" for i, n in enumerate(cap.rising))
    return (f"events={cap.events} rows={len(cap.rows)} rising_edges: {rising} "
            f"malformed={cap.malformed} dropped={cap.dropped} "
            f"duration_s={format_time(cap.duration_us)}")


def port_lines(port: str, baud: int, seconds: Optional[float]) -> Iterable[str]:
    """Lines from a live port until Ctrl+C or the time is up."""
    return serial_log.iter_lines(serial_log.read_chunks(port, baud, seconds))


def parse_args(argv: Optional[list] = None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(
        description="Pico tap serial output to a Saleae style CSV")
    source = ap.add_mutually_exclusive_group(required=True)
    source.add_argument("--port", help="serial port of the Pico, e.g. COM7")
    source.add_argument("--from-log", help="convert a saved raw log instead")
    ap.add_argument("--channels", type=int, required=True,
                    help=f"channels to write ({MIN_CHANNELS} to {MAX_CHANNELS})")
    ap.add_argument("--out", required=True, help="CSV file to write")
    ap.add_argument("--seconds", type=float,
                    help="stop a live capture after this long (default: Ctrl+C)")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                    help=f"baud rate (default {DEFAULT_BAUD})")
    return ap.parse_args(argv)


def warn(message: str) -> None:
    print(message, file=sys.stderr)


def main(argv: Optional[list] = None) -> int:
    args = parse_args(argv)
    if not MIN_CHANNELS <= args.channels <= MAX_CHANNELS:
        print(f"ERROR: --channels must be {MIN_CHANNELS} to {MAX_CHANNELS}",
              file=sys.stderr)
        return EXIT_BAD_INPUT
    try:
        if args.from_log:
            with open(args.from_log, encoding="utf-8",
                      errors="replace") as handle:
                cap = convert(handle, args.channels, warn)
        else:
            cap = convert(port_lines(args.port, args.baud, args.seconds),
                          args.channels, warn)
    except OSError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return EXIT_BAD_INPUT
    except ImportError:
        print("ERROR: pyserial is not installed (pip install pyserial)",
              file=sys.stderr)
        return EXIT_BAD_INPUT
    except Exception as exc:  # serial.SerialException and friends
        if type(exc).__module__.split(".")[0] != "serial":
            raise
        print(f"ERROR: cannot use port {args.port}: {exc}", file=sys.stderr)
        return EXIT_BAD_INPUT
    print(summary(cap), file=sys.stderr)
    if not cap.rows:
        print("ERROR: no events captured; no CSV written", file=sys.stderr)
        return EXIT_NO_EVENTS
    try:
        folder = os.path.dirname(args.out)
        if folder:
            os.makedirs(folder, exist_ok=True)
        with open(args.out, "w", encoding="utf-8", newline="\n") as out:
            write_csv(cap, out)
    except OSError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return EXIT_BAD_INPUT
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
