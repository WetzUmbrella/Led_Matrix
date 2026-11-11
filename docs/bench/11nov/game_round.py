#!/usr/bin/env python3
"""One process for the whole step 1 round (11 Nov 2026): configure, build,
upload (twice, for the session race), game mode, start, then follow STATUS.

A single process keeps one command seq and one session, so the Pico never
sees a restarted PC (the stale-command bug found 11 Nov). Usage:
    .venv/bin/python docs/bench/11nov/game_round.py <pico ip> <session> <gif> <map>
"""
import sys
import time

sys.path.insert(0, "pc")
import dashboard as db  # noqa: E402

pico, session, gif, nmap = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
sh = db.DashboardShell(pico, session)


def run(line):
    print(">>> %s" % line, flush=True)
    sh.onecmd(line)
    sh.postcmd(False, line)


def settle(seconds):
    end = time.time() + seconds
    while time.time() < end:
        sh._flush()
        time.sleep(0.25)


settle(3)
run("config %s 1x2 %s pause 8" % (gif, nmap))
run("build")
run("upload")
settle(3)
run("upload")
settle(3)
run("status")
run("mode game")
settle(1)
run("game start")
start, last = time.time(), None
while time.time() - start < 40:
    sh._flush()
    s = sh.model.status
    if s is not None:
        line = "%s score=%d online=0x%03x" % (s.state_name(), s.score, s.online)
        if line != last:
            print("%5.1f s  %s" % (time.time() - start, line), flush=True)
            last = line
    time.sleep(0.25)
run("status")
