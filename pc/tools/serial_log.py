#!/usr/bin/env python3
"""Timestamped serial logger for the bench test day (replaces Tera Term).

Opens a serial port, and for every received line writes
``<HH:MM:SS.mmm> <line>`` (PC local time, millisecond precision) to a log
file and to stdout. The file starts with one header line so that every log
says what it is:

    # test=D1 date=2026-11-05 tag=bench-5nov tile=3/9c:13:.. image=-
      net=A pico=- who=jolyn port=COM5

(one line in the file, wrapped here). Optional values that are not given are
written as ``-``. When the run stops (Ctrl+C or ``--minutes``) a final line
``# end lines=<n>`` is written. The file is opened in append mode, so several
boots can be logged into one file; each run adds its own header and end line.

Details:
  * bytes are decoded as UTF-8 with errors='replace', so noise on the line
    never stops the logger;
  * a partial line is held until its newline arrives; the trailing CR and LF
    are removed;
  * the file is flushed after every line, so a crash loses nothing.

Usage:
    python pc/tools/serial_log.py --port COM5 --out docs/week10/foo.log \\
        --test D1 --who jolyn [--tile 3/9c:13:..] [--image tile_plain_A] \\
        [--net A] [--pico A] [--tag bench-5nov] [--baud 115200] [--minutes N]

Exit codes: 0 normal stop, 2 the port or the file cannot be opened.

Standard library plus pyserial (imported only when the port is opened).
"""

from __future__ import annotations

import argparse
import os
import sys
import time
from datetime import datetime
from typing import Callable, Iterable, Iterator, Optional

EXIT_OK = 0
EXIT_BAD_INPUT = 2

DEFAULT_TAG = "bench-5nov"
DEFAULT_BAUD = 115200

#: Serial read timeout in seconds, so Ctrl+C and --minutes are noticed.
READ_TIMEOUT_S = 0.2
READ_SIZE = 256


def make_header(test: str, date: str, tag: str, tile: Optional[str],
                image: Optional[str], net: Optional[str], pico: Optional[str],
                who: str, port: str) -> str:
    """The first line of every log; missing optional values become '-'."""
    def dash(value: Optional[str]) -> str:
        return value if value else "-"

    return (f"# test={test} date={date} tag={dash(tag)} tile={dash(tile)} "
            f"image={dash(image)} net={dash(net)} pico={dash(pico)} "
            f"who={who} port={port}")


def format_stamp(moment: datetime) -> str:
    """HH:MM:SS.mmm in the moment's own (local) time."""
    return f"{moment:%H:%M:%S}.{moment.microsecond // 1000:03d}"


def iter_lines(chunks: Iterable[bytes]) -> Iterator[str]:
    """Yield decoded lines from byte chunks, without CR or LF.

    A line split across chunks is joined; a CRLF split between two chunks is
    handled. Bytes left without a newline when the chunks end are yielded as
    a last line, so nothing received is lost.
    """
    buffer = b""
    for chunk in chunks:
        buffer += chunk
        while True:
            end = buffer.find(b"\n")
            if end < 0:
                break
            raw, buffer = buffer[:end], buffer[end + 1:]
            yield raw.rstrip(b"\r").decode("utf-8", errors="replace")
    if buffer:
        yield buffer.rstrip(b"\r").decode("utf-8", errors="replace")


def log_lines(chunks: Iterable[bytes],
              clock: Callable[[], datetime]) -> Iterator[str]:
    """Yield ``<HH:MM:SS.mmm> <line>`` for every line in the chunks.

    The clock is read when a line completes, not when the generator starts.
    """
    for line in iter_lines(chunks):
        yield f"{format_stamp(clock())} {line}"


def read_chunks(port: str, baud: int,
                max_seconds: Optional[float] = None) -> Iterator[bytes]:
    """Read bytes from a serial port until Ctrl+C or max_seconds pass.

    pyserial is imported here so that the rest of the module (and the tests)
    work without it.
    """
    import serial  # noqa: PLC0415 (lazy on purpose)

    deadline = None if max_seconds is None else time.monotonic() + max_seconds
    handle = serial.Serial(port, baud, timeout=READ_TIMEOUT_S)
    try:
        while deadline is None or time.monotonic() < deadline:
            data = handle.read(READ_SIZE)
            if data:
                yield data
    except KeyboardInterrupt:
        return
    finally:
        handle.close()


def run_log(chunks: Iterable[bytes], out, echo,
            clock: Callable[[], datetime] = datetime.now) -> int:
    """Write stamped lines to out (flushing each) and echo; return the count."""
    count = 0
    for text in log_lines(chunks, clock):
        out.write(text + "\n")
        out.flush()
        echo.write(text + "\n")
        echo.flush()
        count += 1
    return count


def parse_args(argv: Optional[list] = None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(description="Timestamped serial logger")
    ap.add_argument("--port", required=True, help="serial port, e.g. COM5")
    ap.add_argument("--out", required=True, help="log file (appended to)")
    ap.add_argument("--test", required=True, help="test id, e.g. D1")
    ap.add_argument("--who", required=True, help="who is logging")
    ap.add_argument("--tile", help="tile id, e.g. 3/9c:13:..")
    ap.add_argument("--image", help="image name, e.g. tile_plain_A")
    ap.add_argument("--net", help="network name, e.g. A")
    ap.add_argument("--pico", help="Pico name, e.g. A")
    ap.add_argument("--tag", default=DEFAULT_TAG,
                    help=f"run tag (default {DEFAULT_TAG})")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD,
                    help=f"baud rate (default {DEFAULT_BAUD})")
    ap.add_argument("--minutes", type=float,
                    help="stop after this many minutes (default: Ctrl+C)")
    return ap.parse_args(argv)


def main(argv: Optional[list] = None) -> int:
    args = parse_args(argv)
    header = make_header(args.test, datetime.now().strftime("%Y-%m-%d"),
                         args.tag, args.tile, args.image, args.net,
                         args.pico, args.who, args.port)
    seconds = None if args.minutes is None else args.minutes * 60
    try:
        folder = os.path.dirname(args.out)
        if folder:
            os.makedirs(folder, exist_ok=True)
        with open(args.out, "a", encoding="utf-8", newline="\n") as out:
            out.write(header + "\n")
            out.flush()
            print(header)
            try:
                count = run_log(read_chunks(args.port, args.baud, seconds),
                                out, sys.stdout)
            except ImportError:
                print("ERROR: pyserial is not installed "
                      "(pip install pyserial)", file=sys.stderr)
                return EXIT_BAD_INPUT
            except Exception as exc:  # serial.SerialException and friends
                if type(exc).__module__.split(".")[0] != "serial":
                    raise
                print(f"ERROR: cannot use port {args.port}: {exc}",
                      file=sys.stderr)
                return EXIT_BAD_INPUT
            end = f"# end lines={count}"
            out.write(end + "\n")
            out.flush()
            print(end)
    except OSError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return EXIT_BAD_INPUT
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
