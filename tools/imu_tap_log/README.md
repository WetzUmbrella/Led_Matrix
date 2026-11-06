# imu_tap_log — H6 tap-feasibility logger

Bench logger for hardware test card H6 ("Tap feasibility data"), see
docs/bench/2026-11-06_hardware_testing.md section "H6". It logs how hard
taps on the tile are, so tap thresholds can be chosen.

Target: Waveshare ESP32-S3-Matrix (ESP-IDF target `esp32s3`).

## Build and flash (WSL)

    get_idf
    cd tools/imu_tap_log
    idf.py set-target esp32s3
    idf.py build
    idf.py -p /dev/ttyACM0 flash monitor

The board's native console port is the USB-Serial-JTAG (`/dev/ttyACM0`).

## Serial line formats

One line per sample/event, all comma-separated:

- `H,board=<n|MAC aa:bb:cc:dd:ee:ff>,range_g=16,odr_hz=1000,window_ms=10`
  Header, printed once at boot before any `T` line. `board` is the NVS
  number from namespace "board" key "number" (u8), or the WIFI_STA MAC
  fallback when the key cannot be read.
- `T,<ms since boot>,<peak |a| in mg>`
  One per 10 ms window; ms is the window start; peak |a| is an integer.
- `MARK,<ms since boot>,<char>`
  One per character typed in the serial monitor (`\r`/`\n` ignored).
- `E,imu_not_found`
  Printed once a second while the QMI8658 is not found; init is retried.

## Range and ODR

- Range: +/-16 g — the widest QMI8658 range, so a firm finger tap does not
  saturate. Scale: 2048 LSB per g (mg = LSB * 1000 / 2048).
- ODR: 1000 Hz accelerometer, sampled by polling STATUS0 bit 0.

## Saving the log

    idf.py -p /dev/ttyACM0 monitor | tee h6_tile<n>.log

Replace `<n>` with the tile/board number.

## H6 procedure (MARK keys)

Type the key in the monitor, then do that action for the agreed count:

- `c` — centre taps
- `e` — edge taps
- `k` — board knocks
- `b` — Blu Tack presses
- `n` — noise floor

Typing in the monitor does not touch the board, so the marks do not disturb
the data; each MARK line is stamped with ms since boot and lines up with the
`T` lines in the saved log.

## Summarising a saved log

    python summarise.py h6_tile<n>.log

Prints, per segment between MARK lines (labelled by the MARK character, plus
a "before first MARK" segment when it has T lines): the label, number of T
lines, max peak and the 99th-percentile peak in mg (nearest-rank). Exits 1
when the file has no T lines.
