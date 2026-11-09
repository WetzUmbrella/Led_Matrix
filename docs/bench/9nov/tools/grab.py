import sys, time, serial, threading
def grab(port, secs, out):
    s = serial.Serial(port, 115200, timeout=0.2)
    s.dtr = False; s.rts = True; time.sleep(0.1); s.rts = False  # reset into app
    end = time.time() + secs
    with open(out, "wb") as f:
        while time.time() < end:
            f.write(s.read(4096))
    s.close()
secs = float(sys.argv[1]); ts = []
for a in sys.argv[2:]:
    port, out = a.split("=")
    t = threading.Thread(target=grab, args=(port, secs, out)); t.start(); ts.append(t)
for t in ts: t.join()
