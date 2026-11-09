"""Timed dashboard run: telemetry check, config, build, upload, optional start."""
import sys, time
sys.path.insert(0, "pc")
import dashboard as db
pico, session, gif, do_start = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4] == "start"
sh = db.DashboardShell(pico, session)
def run(line):
    t0 = time.time(); print(">>> %s" % line, flush=True)
    sh.onecmd(line); sh.postcmd(False, line)
    dt = time.time() - t0; print("<<< %.1f s" % dt, flush=True); return dt
for _ in range(6):
    sh._flush(); time.sleep(0.5)
run("status")
run("config %s 1x2 4,5,0,0,0,0,0,0,0 pause 8" % gif)
run("build")
up = run("upload")
for _ in range(10):
    sh._flush(); time.sleep(0.5)
run("status")
if do_start:
    run("start")
    for _ in range(16):
        sh._flush(); time.sleep(0.5)
    run("status")
print("UPLOAD_SECONDS %.1f" % up)
