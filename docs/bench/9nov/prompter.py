#!/usr/bin/env python3
"""Bench prompter for feasibility steps 5 (taps) and 6 (turns), 9 Nov 2026.

Prints one action per beat and writes the taps CSV for marks.py and
summarise.py (tools/imu_tap_log on origin/tools/imu-tap-log) with the wall
time of each prompt, so nobody has to note times by hand. A constant
reaction delay does not matter: marks.py aligns each tile on its first tap.

Usage (from the repo root, in a terminal you can watch):
    .venv/bin/python docs/bench/9nov/prompter.py taps
    .venv/bin/python docs/bench/9nov/prompter.py turns
"""

import datetime
import sys
import time

TILES = (4, 5)
BEAT_S = 1.5
PAUSE_S = 6.0
HOLD_S = 3.0
READY_S = 5.0
QUIET_S = 30.0

#: (kind, count, text) per tile for the taps run.
TAP_BLOCKS = (("c", 20, "SOFT fingertip tap, the weakest a 7-year-old would do"),
              ("e", 20, "OPEN-PALM slap, as hard as an excited child"))


class Csv:
    """The taps CSV, one row per prompted action."""

    def __init__(self, path, comment):
        self.f = open(path, "w", buffering=1)
        self.f.write("# %s\n" % comment)
        self.f.write("wall,tile,kind\n")

    def row(self, tile, kind):
        now = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.f.write("%s,%s,%s\n" % (now, tile, kind))


def say(text):
    print("\n\n  >>>  %s\n" % text, flush=True)


def wait(seconds, text=None):
    if text:
        print("      (%s)" % text, flush=True)
    time.sleep(seconds)


def countdown(text):
    say(text)
    for left in range(int(READY_S), 0, -1):
        print("      starting in %d" % left, flush=True)
        time.sleep(1)


def taps(csv):
    for tile in TILES:
        for kind, count, text in TAP_BLOCKS:
            countdown("TILE %d: %s, %d times, on each prompt" % (tile, text, count))
            for n in range(1, count + 1):
                csv.row(tile, kind)
                say("TAP tile %d  (%d of %d)" % (tile, n, count))
                time.sleep(BEAT_S)
            wait(PAUSE_S, "pause, hands off")
    countdown("KNOCK on the shared surface (box or book), away from both tiles, 10 times")
    for n in range(1, 11):
        csv.row("board", "k")
        say("KNOCK the surface  (%d of 10)" % n)
        time.sleep(2.0)
    say("QUIET: nobody touches anything for %d s" % int(QUIET_S))
    time.sleep(QUIET_S)


def turns(csv):
    for tile in TILES:
        countdown("TILE %d: 3 SOFT taps first, to line up the clocks" % tile)
        for n in range(1, 4):
            csv.row(tile, "c")
            say("TAP tile %d  (%d of 3)" % (tile, n))
            time.sleep(BEAT_S)
        countdown("TILE %d alone: turn 90 deg clockwise, hold, turn back, hold (10 times)" % tile)
        for n in range(1, 11):
            csv.row(tile, "b")
            say("TURN tile %d 90 deg CLOCKWISE  (%d of 10)" % (tile, n))
            time.sleep(HOLD_S)
            csv.row(tile, "b")
            say("TURN tile %d BACK  (%d of 10)" % (tile, n))
            time.sleep(HOLD_S)
        wait(PAUSE_S, "pause, hands off")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "taps":
        csv = Csv("docs/bench/9nov/f05_taps.csv", "c = soft tap, e = palm slap, k = knock")
        taps(csv)
    elif mode == "turns":
        csv = Csv("docs/bench/9nov/f06_taps.csv", "c = soft tap (alignment), b = a 90 deg turn")
        turns(csv)
    else:
        print(__doc__)
        return 1
    say("DONE. Tell Claude.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
