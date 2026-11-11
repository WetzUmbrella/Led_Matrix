#!/usr/bin/env python3
"""Step 1 (11 Nov 2026): first end-to-end round of the v1 game.

Drives the dashboard shell: stop any video, switch to game mode, start a
30 s round, and print the Pico's STATUS score once a second until the round
ends. Usage (repo root, laptop on the wall's network):
    .venv/bin/python docs/bench/11nov/game_run.py <pico ip> <session>
The session must be the one the tiles' asset was uploaded in (READY needs it).
"""
import sys
import time

sys.path.insert(0, "pc")
import dashboard as db  # noqa: E402

pico, session = sys.argv[1], int(sys.argv[2])
sh = db.DashboardShell(pico, session)
# Workaround (bug found 11 Nov): a new PC process restarts its command seq at
# 0, and the Pico drops those as stale within the same session. Start well
# above the last run's commands so they count as newer.
sh.link.seq = int(time.time()) % 16384 + 8192
# Same bug, second half: the link only learns the session from a new
# SESSION_CONFIG, so its commands carry session 0 until then.
sh.link.session_id = session


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
run("status")
run("stop")
settle(3)
run("mode game")
settle(2)
run("status")
run("game start")
start = time.time()
last = None
while time.time() - start < 45:
    sh._flush()
    s = sh.model.status
    if s is not None:
        line = "%5.1f s  %s score=%d online=0x%03x" % (time.time() - start, s.state_name(), s.score, s.online)
        if line[8:] != last:
            print(line, flush=True)
            last = line[8:]
    time.sleep(0.25)
run("status")
