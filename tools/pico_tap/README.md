# pico_tap: a spare Pico W as an edge recorder

Use this when no logic analyser is available. It records every edge on up to 5 tile test pins with a microsecond timestamp. `pc/tools/pico_tap_capture.py` turns the stream into a Saleae-format CSV that `pc/tools/skew_analyse.py` reads unchanged. It is a bench tool, not shipped firmware.

It has not run on hardware yet. **Always run the calibration below before any result depends on it.**

## Wiring

| Pico pin | Channel | Connect to |
|---|---|---|
| GP2 | Channel 0 | tile A test pin (GPIO4) |
| GP3 | Channel 1 | tile B test pin |
| GP4 | Channel 2 | tile C test pin (3-tile test) |
| GP5 | Channel 3 | tile D test pin (soak) |
| GP6 | Channel 4 | tile E test pin (soak) |
| any GND | | every tile's GND, joined |

- Inputs are 3.3 V, the same as the tiles.
- Unused inputs are pulled down and read 0.
- The Pico is powered from the laptop that records.

## Flash

Hold BOOTSEL, plug the Pico in, and drag `pico_tap_<tag>.uf2` onto the RPI-RP2 drive. It's in the shared images folder, built by `tools/build_bench_images.sh`.

To build it yourself, set `PICO_SDK_PATH`, then:

```
cd tools/pico_tap && mkdir build && cd build && cmake .. && make
```

## Calibration (5 min, before the first real capture)

1. Wire **one** tile's test pin to **both** GP2 and GP3, with grounds joined, and let the tile tick (Pico beaconing, tile JOINed).
2. Record for 60 s:

   ```
   python pc/tools/pico_tap_capture.py --port COM<n> --channels 2 --seconds 60 --out calib.csv
   ```

3. Analyse it:

   ```
   python pc/tools/skew_analyse.py calib.csv --ch-a "Channel 0" --ch-b "Channel 1" --marker --out calib_pairs.csv > docs/week10/skew_2tile/calibration_report.txt
   ```

4. **Pass:** max skew under 0.05 ms, because it is the same signal on both inputs.
5. Also check that `TICK IDENTITY CHECKED` is printed, and that the capture summary shows `dropped=0`.

If it fails, the tool is wrong, not the tiles. Do not use it for evidence.

## A real capture (Part B, 10 min)

```
python pc/tools/pico_tap_capture.py --port COM<n> --channels 2 --seconds 600 --out docs/week10/skew_2tile/skew_2tile.csv
python pc/tools/skew_analyse.py docs/week10/skew_2tile/skew_2tile.csv --ch-a "Channel 0" --ch-b "Channel 1" --marker --out docs/week10/skew_2tile/skew_pairs.csv --plot docs/week10/skew_2tile/skew_plot.png > docs/week10/skew_2tile/skew_report.txt
```

- Use `--channels 3` for the 3-tile test, and `--channels 5` for the soak.
- `--from-log <file>` converts a raw log saved earlier, for example one written by `pc/tools/serial_log.py`.

## Limits

- **Accuracy:** the timestamp is taken in the GPIO interrupt. The latency of a few microseconds is the same for every channel, so the error between two channels is about 1 to 2 µs. That is far below the 5 ms target, but it is not a logic analyser's 1 µs sampling. Say so in the report.
- **Dropped events:** if the PC stops reading the USB port, events queue in a 1,024-entry ring and are then dropped. They are counted in the `# stats ... dropped=` line, and the capture script warns about them.
- **No waveform view:** it gives edge times only. For the RMT data line (F6, 4 MS/s), a real analyser is still needed.
