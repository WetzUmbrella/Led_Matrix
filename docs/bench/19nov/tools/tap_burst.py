#!/usr/bin/env python3
"""Bursts of valid TAP datagrams for tiles 1 to 8 at once, as a hard tap
felt by the wall, sent from the laptop to the Pico (19 Nov 2026).

Usage: tap_burst.py PICO SESSION BURST PERIOD SECONDS [FIRST_SEQ]
The Pico drops a TAP whose seq is not newer than the last one it saw from
that tile in the session (CL_STATS stale), so a second run in the same
session needs a FIRST_SEQ past the last run's (printed at the end)."""
import socket, subprocess, sys, time
sys.path.insert(0, "pc")
import proto_layout as pl
pico, session, burst, period, seconds = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind(("", 0))
seq = int(sys.argv[6]) - 1 if len(sys.argv) > 6 else 1000
end = time.time() + seconds
n = 0
while time.time() < end:
    seq += 1
    for node in range(1, burst + 1):
        s.sendto(pl.encode_tap(session_id=session, seq=seq, node_id=node,
                               peak=20000 if node == 5 else 900,
                               t_tap=int(time.monotonic() * 1e6)),
                 (pico, 4211))
    n += 1
    import json, urllib.request
    age = json.load(urllib.request.urlopen("http://127.0.0.1:8080/api/state"))["status_age_s"]
    if n % 10 == 0 or age is None or age > 3:
        print("%s burst %d, status age %s" % (time.strftime("%T"), n, age), flush=True)
    if age is None or age > 3:
        print("FROZE: no STATUS for over 3 s", flush=True)
        break
    time.sleep(period)
print("last seq %d" % seq, flush=True)
