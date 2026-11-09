"""Log serial ports with PC time stamps; reopen a port that disappears."""
import sys, time, threading, datetime, serial
def run(port, out, end):
    with open(out, "a", buffering=1) as f:
        f.write("# %s start %s\n" % (port, datetime.datetime.now().strftime("%H:%M:%S")))
        while time.time() < end:
            try:
                s = serial.Serial(port, 115200, timeout=0.2)
                f.write("# %s open %s\n" % (port, datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]))
                buf = b""
                while time.time() < end:
                    buf += s.read(4096)
                    while b"\n" in buf:
                        line, buf = buf.split(b"\n", 1)
                        f.write("%s %s\n" % (datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3],
                                             line.decode("ascii", "replace").rstrip()))
            except (serial.SerialException, OSError):
                f.write("# %s gone %s\n" % (port, datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]))
                time.sleep(0.5)
end = time.time() + float(sys.argv[1]) * 60
ts = [threading.Thread(target=run, args=(a.split("=")[0], a.split("=")[1], end)) for a in sys.argv[2:]]
[t.start() for t in ts]; [t.join() for t in ts]
