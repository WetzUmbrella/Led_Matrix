"""Tap test only (bench 12 Nov 2026): game mode, tap test on for N s, off."""
import sys
import time

sys.path.insert(0, "pc")
import dashboard as db  # noqa: E402

pico, session, seconds = sys.argv[1], int(sys.argv[2]), float(sys.argv[3])
sh = db.DashboardShell(pico, session)
# Wait for a STATUS: the PC refuses the tap test until it knows the wall
# is READY.
end = time.time() + 5
while (sh.model.status is None) and (time.time() < end):
    sh._flush()
    time.sleep(0.25)
for line in ("mode game", "tap on"):
    print(">>> %s" % line, flush=True)
    sh.onecmd(line)
    sh.postcmd(False, line)
print("tap test on for %.0f s" % seconds, flush=True)
end = time.time() + seconds
while time.time() < end:
    sh._flush()
    time.sleep(0.25)
print(">>> tap off", flush=True)
sh.onecmd("tap off")
sh.postcmd(False, "tap off")
