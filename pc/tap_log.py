#!/usr/bin/env python3
"""Tap log of the website (bench 19 Nov 2026): every TAP the Pico relays
to the PC, in a tap test or a round, as one CSV row, so a tap test of soft
and hard taps can be studied afterwards (echoes through the board, the
threshold).

Columns: ``pc_time`` (when the PC received it, local time with
milliseconds), ``tile``, ``peak_mg`` (the tile's peak deviation),
``t_tap_us`` (the tile's time stamp of the tap on the Pico clock: the taps
of one physical tap and its echoes share it within a few ms), ``mode``,
``tap_test`` (1 while the tap test is on), ``round`` (the round state) and
``session``. The file is flushed after every row, so a crash loses
nothing.
"""

from __future__ import annotations

import csv
import os
import time
from typing import Optional, TextIO

#: Where the website writes its tap logs by default (gitignored).
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")

#: The CSV columns.
COLUMNS = ("pc_time", "tile", "peak_mg", "t_tap_us", "mode", "tap_test",
           "round", "session")


def default_path(now: Optional[float] = None) -> str:
    """pc/logs/taps_YYYYMMDD_HHMMSS.csv for a website started at ``now``."""
    stamp = time.strftime("%Y%m%d_%H%M%S", time.localtime(now))
    return os.path.join(LOG_DIR, "taps_%s.csv" % stamp)


def clock_text(wall: float) -> str:
    """Local time with milliseconds, for the pc_time column."""
    return "%s.%03d" % (time.strftime("%Y-%m-%d %H:%M:%S",
                                      time.localtime(wall)),
                        int((wall % 1.0) * 1000))


class TapLog:
    """Appends tap rows to a CSV file, the header first when it is new."""

    def __init__(self, path: str) -> None:
        self.path = path
        folder = os.path.dirname(os.path.abspath(path))
        os.makedirs(folder, exist_ok=True)
        b_new = not os.path.exists(path) or os.path.getsize(path) == 0
        self._handle: TextIO = open(path, "a", newline="", encoding="utf-8")
        self._writer = csv.writer(self._handle)
        self.rows = 0
        if b_new:
            self._writer.writerow(COLUMNS)
            self._handle.flush()

    def write(self, wall: float, tile: int, peak_mg: int,
              t_tap_us: Optional[int], mode: str, tap_test: bool,
              round_state: str, session: int) -> None:
        """One tap; ``wall`` is time.time() when the PC received it."""
        self._writer.writerow((
            clock_text(wall), tile, peak_mg,
            "" if t_tap_us is None else t_tap_us, mode,
            1 if tap_test else 0, round_state, session))
        self._handle.flush()
        self.rows += 1

    def close(self) -> None:
        """Close the file."""
        self._handle.close()
