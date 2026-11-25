"""Append the Pico's USB serial log to a file; reopen after a reboot.
Usage: pico_capture.py PORT LOG SECONDS"""
import sys
import time

import serial

port, log, seconds = sys.argv[1], sys.argv[2], float(sys.argv[3])
end = time.time() + seconds
with open(log, "ab", buffering=0) as out:
    while time.time() < end:
        try:
            with serial.Serial(port, 115200, timeout=0.5) as s:
                while time.time() < end:
                    out.write(s.read(4096))
        except (serial.SerialException, OSError):
            out.write(b"\n# capture: port lost, retrying\n")
            time.sleep(1.0)
