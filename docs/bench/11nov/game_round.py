#!/usr/bin/env python3
"""One process for a whole bench round (11 Nov 2026): configure, build,
upload (twice, for the session race), game mode, then the round, then follow
STATUS until RESULTS.

Step 3 (default): ARM at a level; the start sprite waits on the tile nearest
the centre for the child's tap (or --start-now skips it); the countdown, the
round and the results follow. --legacy runs the step 1 round instead (START
at once, no sprite or countdown). --tap-test turns the tap test on for a
number of seconds before arming (every tap lights its tile).

A single process keeps one command seq and one session, so the Pico never
sees a restarted PC (the stale-command bug found 11 Nov). Usage:
    .venv/bin/python docs/bench/11nov/game_round.py <pico ip> <session> \\
        <gif> <map> [--level easy|normal|hard] [--threshold MG] \\
        [--tap-test S] [--start-now] [--legacy] [--layout 1x2]
"""
import argparse
import sys
import time

sys.path.insert(0, "pc")
import dashboard as db  # noqa: E402

parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
parser.add_argument("pico")
parser.add_argument("session", type=int)
parser.add_argument("gif")
parser.add_argument("map")
parser.add_argument("--layout", default="1x2")
parser.add_argument("--level", default="easy",
                    choices=("easy", "normal", "hard"))
parser.add_argument("--threshold", type=int, default=0,
                    help="tap threshold, mg; 0 = the tiles' default")
parser.add_argument("--tap-test", type=float, default=0.0,
                    help="seconds of tap test before the round")
parser.add_argument("--start-now", action="store_true",
                    help="skip the start sprite")
parser.add_argument("--legacy", action="store_true",
                    help="the step 1 round: START at once")
parser.add_argument("--wait", type=float, default=120.0,
                    help="give up after this many seconds without RESULTS")
args = parser.parse_args()
sh = db.DashboardShell(args.pico, args.session)


def run(line):
    print(">>> %s" % line, flush=True)
    sh.onecmd(line)
    sh.postcmd(False, line)


def settle(seconds):
    end = time.time() + seconds
    while time.time() < end:
        sh._flush()
        time.sleep(0.25)


def follow(seconds, until=None):
    """Print each change of the Pico's state, round state and counts; stop
    after seconds, or a few seconds after the round state named until, once
    the round was seen playing (the Pico may still show an old round's
    RESULTS when the round is armed; bench 12 Nov 2026, round 2)."""
    start, last, done_at, played = time.time(), None, None, False
    while time.time() - start < seconds:
        sh._flush()
        s = sh.model.status
        if s is not None:
            line = ("%s / %s score=%d hits=%d missed=%d wrong=%d shown=%d "
                    "left=%d reaction=%d online=0x%03x" % (
                        s.state_name(), s.round_name(), s.score, s.hits(),
                        s.missed, s.wrong, s.shown, s.time_left_s,
                        s.reaction_ms, s.online))
            if line != last:
                print("%6.1f s  %s" % (time.time() - start, line),
                      flush=True)
                last = line
            played = played or (s.round_name() == "PLAYING")
            if (until is not None) and (s.round_name() == until) and \
                    played and (done_at is None):
                done_at = time.time()
        if (done_at is not None) and (time.time() - done_at > 5.0):
            return
        time.sleep(0.25)


settle(3)
run("config %s %s %s pause 8" % (args.gif, args.layout, args.map))
run("build")
run("upload")
settle(3)
run("upload")
settle(3)
run("status")
run("mode game")
settle(1)
if args.tap_test > 0:
    run("tap on")
    print("tap test: tap the tiles for %.0f s" % args.tap_test, flush=True)
    follow(args.tap_test)
    run("tap off")
    print("tap test over: stop tapping; arming in 3 s", flush=True)
    settle(3)
if args.legacy:
    run("game start")
    follow(40.0, until="RESULTS")
else:
    run("threshold %d" % args.threshold)
    run("game arm %s" % args.level)
    if args.start_now:
        settle(1)
        run("game now")
    else:
        print("armed: tap the start sprite (60 s)", flush=True)
    follow(args.wait, until="RESULTS")
run("status")
