#!/usr/bin/env python3
"""Send bursts of junk UDP datagrams to the Pico through the router and
report whether it stops answering (19 Nov 2026 freeze investigation)."""
import socket, subprocess, sys, time
pico, burst, period, seconds = sys.argv[1], int(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
end = time.time() + seconds
n = 0
while time.time() < end:
    for i in range(burst):
        s.sendto(b"\xff" * 30, (pico, 4211))  # Not a valid header: rx_bad.
    n += 1
    if n % 5 == 0:
        ok = subprocess.run(["ping", "-c1", "-W1", pico], capture_output=True).returncode == 0
        print("%s burst %d, pico %s" % (time.strftime("%T"), n, "ok" if ok else "SILENT"), flush=True)
        if not ok:
            break
    time.sleep(period)
