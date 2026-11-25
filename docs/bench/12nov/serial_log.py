#!/usr/bin/env python3
"""Log several serial ports to files for a while, without resetting the
boards (12 Nov 2026 bench). DTR is held high so the Pico's USB serial sends
(TinyUSB CDC only writes while the host asserts DTR); the ESP32-S3 USB
serial does not reset on DTR alone. Usage:
    .venv/bin/python docs/bench/12nov/serial_log.py SECONDS PORT=FILE ...
"""
import sys
import threading
import time

import serial


def log(port: str, seconds: float, path: str) -> None:
    link = serial.Serial()
    link.port = port
    link.baudrate = 115200
    link.timeout = 0.2
    link.dtr = True
    link.rts = False
    link.open()
    end = time.time() + seconds
    with open(path, "wb") as out:
        while time.time() < end:
            data = link.read(4096)
            if data:
                out.write(data)
                out.flush()
    link.close()


def main() -> None:
    seconds = float(sys.argv[1])
    threads = []
    for arg in sys.argv[2:]:
        port, path = arg.split("=", 1)
        thread = threading.Thread(target=log, args=(port, seconds, path))
        thread.start()
        threads.append(thread)
    for thread in threads:
        thread.join()


if __name__ == "__main__":
    main()
