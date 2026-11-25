#!/usr/bin/env python3
"""Re-upload the chosen GIF to the whole wall again and again from the
website and count Pico freezes (bench 24 Nov 2026): a freeze is a STATUS
gap over 3 s; the run waits for the wall to be READY again after each one.

Usage (from Led_Matrix, website running): upload_soak.py RUNS [LOG]
"""
import json, sys, time, urllib.request

URL = "http://127.0.0.1:8080"


def state():
    return json.load(urllib.request.urlopen(URL + "/api/state", timeout=3))


def post(path, body):
    req = urllib.request.Request(URL + path, json.dumps(body).encode(),
                                 {"Content-Type": "application/json"})
    return urllib.request.urlopen(req, timeout=10).status


def main():
    runs = int(sys.argv[1])
    log = open(sys.argv[2], "a") if len(sys.argv) > 2 else sys.stdout
    freezes = 0
    for run in range(1, runs + 1):
        gif = state()["gif"]
        started = time.time()
        post("/api/gif", {"name": gif})
        froze, worst = False, 0.0
        while True:
            s = state()
            age = s["status_age_s"] or 0.0
            worst = max(worst, age)
            froze = froze or age > 3.0
            if (s["job"] is None and s["pico_state"] == "READY"
                    and s["readiness"] == 511):
                break
            if time.time() - started > 180:
                print("run %d: not READY after 180 s" % run, file=log)
                break
            time.sleep(0.1)
        freezes += froze
        secs = [u.get("seconds") for u in s["uploads"]]
        print("%s run %d: %.1f s to READY, slowest tile %s s, max STATUS gap "
              "%.1f s%s" % (time.strftime("%T"), run, time.time() - started,
                            max((x for x in secs if x), default=None), worst,
                            ", FREEZE" if froze else ""), file=log, flush=True)
    print("freezes: %d of %d" % (freezes, runs), file=log, flush=True)


main()
