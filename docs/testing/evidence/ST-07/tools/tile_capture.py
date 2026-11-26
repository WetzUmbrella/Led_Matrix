"""Append a tile's USB serial log to a file, each line stamped with the
laptop's clock (to line the logs up with each other and the video).
DTR and RTS are held low so opening the port does not reset the ESP32-S3.
Usage: tile_capture.py PORT LOG SECONDS"""
import sys
import time

import serial

port, log, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
end = time.time() + seconds
with open(log, "a", buffering=1, encoding="utf-8", errors="replace") as out:
    while time.time() < end:
        try:
            s = serial.Serial()
            s.port, s.baudrate, s.timeout = port, 115200, 0.5
            s.dtr, s.rts = False, False
            s.open()
            buf = b""
            while time.time() < end:
                buf += s.read(4096)
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    out.write("%.3f %s\n" % (time.time(),
                              line.decode("utf-8", "replace").rstrip("\r")))
            s.close()
        except (serial.SerialException, OSError) as e:
            out.write("%.3f # capture: %s, retrying\n" % (time.time(), e))
            time.sleep(1.0)
