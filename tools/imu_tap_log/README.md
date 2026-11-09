# imu_tap_log: H6 tap-feasibility logger

Bench logger for hardware test card H6 ("Tap feasibility data"), see
docs/bench/2026-11-06_hardware_testing.md section "H6". It logs how hard
taps on the tile are, so the tap threshold can be chosen from real numbers.

Target: Waveshare ESP32-S3-Matrix (ESP-IDF target `esp32s3`, ESP-IDF v5.3).

Files:

- `main/imu_tap_log.c`: the firmware (QMI8658 over I2C, serial output).
- `marks.py`: reads the operator's taps CSV and the tile logs, and puts each
  tile on one wall clock.
- `summarise.py`: per-tile rate and missed samples, peaks for taps and
  non-taps, and a threshold sweep with a suggested threshold.
- `../tests/test_imu_tap_marks.py`, `../tests/test_imu_tap_summarise.py`:
  pytest tests on synthetic logs (`python3 -m pytest -q tools/tests`).

## Build and flash (Windows)

Flash and log from Windows, on the board's COM port. A board attached to WSL
disappears from Windows, so do not flash or log through `/dev/ttyACM*` in WSL.

    cd tools\imu_tap_log
    idf.py set-target esp32s3
    idf.py build
    idf.py -p COM<n> flash

The board's native console is the USB-Serial-JTAG port, so `idf.py monitor`
is not needed for the logs; `serial_log.py` (below) reads the same port.

The C file has not been built on the bench yet. No ESP-IDF was available when
the rework was written, so the first `idf.py build` is the real compile check.

## Serial line formats

One line per event, comma-separated. Nothing is read from the port.

- `H,board=<n or MAC>,range_g=16,odr_hz=1000,window_ms=10,src=<fifo|poll>,tau_ms=500,hold_mg=<n>`
  Printed once a second, so a logger started late still sees it. `board` is
  the NVS number (namespace "board", key "number") or the WiFi STA MAC when
  the number cannot be read. `src` is the sample source (see below).
- `S,<ms>,<samples>,<overruns>`
  Once a second: the samples taken in the last second, and the overruns in it.
  The S lines give the sample rate actually achieved.
- `T,<ms>,<peak mg>`
  One per 10 ms window. `ms` is the window start, in ms since boot. The peak
  is the largest deviation from the slow baseline in the window (below).
- `E,imu_not_found`
  Once a second while the QMI8658 is not found; the init is retried.

`ms` is time since that tile's boot. Each tile has its own clock, so the tile
logs cannot be compared with each other until they are aligned (see
"Aligning the logs").

## What the numbers mean

- Range: +/-16 g, the widest QMI8658 range, so a firm finger tap does not
  saturate (2048 LSB per g).
- Sample source: the QMI8658 FIFO (`src=fifo`) is read every 10 ms by an
  esp_timer. If the S lines show a rate well under 1000 with `src=fifo`, the
  FIFO register sequence probably does not match this board's QMI8658 (it was
  written from the datasheet, not tested). Set `ITL_SOURCE_FIFO` to 0 in
  `main/imu_tap_log.c` (data-ready poll, `src=poll`) and rebuild.
- Peak: each axis has a baseline (an exponential average with a 500 ms time
  constant) that stands for gravity. The baseline is held while a tap is in
  the current or previous window, so a tap does not drag it. The peak is the
  largest |a - baseline| over the three axes in the window, in mg.

## Logging a run (H6)

Each tile is logged to its own file. From a Windows terminal in the repo:

    python pc\tools\serial_log.py --port COM<n> --out docs\bench\6nov\h6_tap_<tile>.log --test H6 --who <name> --tile <tile> --tag bench-6nov --minutes 8

Use the board number as `<tile>`. Each line of the file gets a PC time stamp
at the front, which the tools ignore. One log per tile; a log that holds two
boots is refused, so start a new file after a reset.

Run the steps from card H6 with every tile logging:

1. Keep the board still for about 5 s before the first tap. The alignment
   uses the first strong peak, so the first action must be the first tap.
2. 20 centre taps (`c`) and 20 edge taps (`e`) on each tile, about 1 s apart.
3. 10 knocks on the board edge, away from all tiles (`k`).
4. Press each tile back into its Blu Tack five times (`b`).
5. 30 s with no one touching anything. Write no rows for this stretch; it is
   the quiet baseline.

Write the taps CSV as you go: one row per action, with the local wall time of
the action (`HH:MM:SS` or `HH:MM:SS.mmm`), the tile, and the kind.

    wall,tile,kind
    10:14:02.000,3,c
    10:14:03.120,3,c
    10:14:21.500,board,k
    10:15:40.000,5,b

- `tile` is the board number, exactly as in the log's `board=` field. For a
  tile with no NVS number, use the MAC string as it appears in `board=`.
- `tile` is `board` for knocks, and only for knocks.
- Lines starting with `#` are comments.

Taps are about 1 s apart, so write the times to the nearest 0.1 s if you can.
The time stamps on the log lines come from the same PC clock, so they are a
good reference for the times you write down.

## Aligning the logs (marks.py)

    python tools\imu_tap_log\marks.py --taps taps.csv docs\bench\6nov\h6_tap_*.log

Each tile's offset is the wall time of its first centre or edge tap minus the
time of its first strong peak (a window of 300 mg or more; `--align-mg`). The
output gives, per tile, the boot time on the wall clock and the residual of
every tap: the distance between the tap's written time and the nearest strong
peak. Residuals near zero mean the CSV and the logs agree. A residual over
500 ms (`--tol-ms`) means a mistyped time, or a first peak that was not the
first tap; fix the CSV or the log and run again.

`--detect` also lists each log's peak clusters, which helps to check that the
first peak really is the first tap.

## Summarising a run (summarise.py)

    python tools\imu_tap_log\summarise.py --taps taps.csv docs\bench\6nov\h6_tap_*.log

Each window is classed by what happened within 500 ms of it: a tap on this
tile (`tap`), a Blu Tack press on this tile (`press`), a tap or press on a
neighbour (`nbr`), a board knock (`knock`), a tap on a tile that is not a
neighbour (`far`, not counted), or nothing (`quiet`). By default every other
tile is a neighbour; pass `--neighbours "3-5,3-4"` with the pairs that touch
on the board. The tool prints:

- a per-tile table: the sample rate (median and lowest), missed samples
  (overruns: samples in `src=poll`, FIFO overflow events in `src=fifo`), gaps
  in the 10 ms windows, the boot time on the wall clock, and the worst residual;
- a per-tile peak table, one value per event (the highest window within 500 ms
  of the event): the lowest and median tap peak, the highest press, neighbour
  and knock peak, and the 99th and highest quiet peak;
- a threshold sweep (50 to 1000 mg, step 25; change with `--mg-min`,
  `--mg-max`, `--mg-step`): for each tile, the number of false triggers (any
  trigger outside its own taps), and the share of taps detected. Triggers less
  than 50 ms apart count once (`--dedupe-ms`);
- the highest non-tap peak and the lowest tap peak, and a suggested threshold:
  the lowest threshold with no false trigger on any tile that still detects at
  least 95% of taps (`--detect-min`). If no threshold meets both, the output
  says so, and the table is the thing to read.

The suggestion is a starting point, not a decision. Look at the gap between
the highest non-tap peak and the lowest tap peak, and at the neighbour column
for the tile that sits next to the others.

The raw logs stay in `docs/bench/6nov/h6_tap_<tile>.log`; the taps CSV goes
alongside them as `docs/bench/6nov/h6_taps.csv`.
