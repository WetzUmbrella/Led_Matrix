#!/usr/bin/env python3
"""Play a GIF on the wall for a while (bench 12 Nov 2026, step 4d): config,
build, upload, START, then STOP after N seconds. One process, so the Pico
takes every command. Usage:
    .venv/bin/python docs/bench/12nov/video_check.py <pico ip> <session> \\
        <gif> <layout> <map> <seconds>
The map is row-major over the whole grid (rows x cols entries, 0 = an
empty cell), for example 3x3 4,0,0,0,0,0,0,0,5.
"""
import sys
import time

sys.path.insert(0, "pc")
import dashboard as db  # noqa: E402

pico, session, gif, layout, map_text, seconds = sys.argv[1:7]
sh = db.DashboardShell(pico, int(session))


def run(line):
    print(">>> %s" % line, flush=True)
    sh.onecmd(line)
    sh.postcmd(False, line)


def settle(wait_s):
    end = time.time() + wait_s
    while time.time() < end:
        sh._flush()
        time.sleep(0.25)


settle(3)
run("mode video")  # The Pico refuses video START in game mode (A14).
run("config %s %s %s pause 8" % (gif, layout, map_text))
run("build")
run("upload")
settle(3)
run("status")
run("start")
print("playing for %s s" % seconds, flush=True)
settle(float(seconds))
run("stop")
settle(1)
run("status")
