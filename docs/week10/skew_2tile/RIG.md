# Two-tile skew rig (T-B4-03)

Owner: Ahsanul (support: Jia Jun, Cheston, Benjamin). Plan row: T-B4-03 in `SE15_Implementation_Plan.md` section 1. Inventory: W10-4 ("Run the two-tile skew rig"), A6 ("Two-tile GPIO-pulse rig on a logic analyser, then three tiles"), W6D-1c ("NFR2 amended: target 5 ms (SE15), hard limit stays 16.67 ms"), IFM-5c ("inter-tile skew target 5 ms or less, always under 16.67 ms (A6)").

This file is preparation only. No capture has been made; every measured value below is left for the bench run.

## 1. What the plan asks for (copied from the T-B4-03 row)

- Exact behaviour: "Two-tile GPIO-pulse rig: each tile's test pin pulses at tick n, both pins on one logic analyser. Rig build only: origin = 0 in Pico time, so ticks fall on Pico-time multiples of 1,000,000 / 60 us (test configuration, replaced by START_AT in T-B4-04)."
- Acceptance test: "10 min capture (W6 section 7 A6, "at least 10 minutes"): maximum absolute skew over all tick pairs at most 5 ms (SE15 target, W6D-1c) and no pair at or above 16.67 ms (NFR2 hard limit); median, p99 and max reported. Pass = both. A maximum above 5 ms starts T-B4-07."
- Evidence: "Capture file; skew CSV and plot", in `docs/week10/skew_2tile/`.

## 2. Tasks that must land before the first capture

The Depends-on column of T-B4-03 lists four tasks: T-B5-01, T-B4-02, T-B3-03, T-B2-02. T-B4-02 (`timesync`) is Ahsanul's own task. The other three, owned by other buddies, are:

| Task | Owner | Why the rig needs it |
|---|---|---|
| T-B5-01 | Jia Jun | gptimer one-shot tick at 60 fps and the test pin that the ISR drives each tick; records the spare GPIO used as test pin |
| T-B3-03 | Cheston | tile Network Task and the Time Sync queue that delivers beacons to `timesync` |
| T-B2-02 | Benjamin | Pico beacon broadcast with the relative schedule and early timestamp |

The plan lists all four as dependencies; the three above plus T-B4-02 must all be merged into the build flashed on both tiles (and T-B2-02 on the Pico).

## 3. Wiring

```
 Tile A (ESP32-S3)                Logic analyser               Tile B (ESP32-S3)
 test pin (T-B5-01) ---------->   CH0 / D0
                                  CH1 / D1   <---------------- test pin (T-B5-01)
 GND ------------------------->   GND        <---------------- GND
```

- Test pin: the spare GPIO that T-B5-01 records in its log ("a spare GPIO as test pin (pin number recorded in the log)"). Jia Jun confirms the pin number against T-B5-01 before wiring. It must not be GPIO14, which drives the WS2812B data line (`panel_sync/main/render.c:15`, PostTri RMT row). The same GPIO number on both tiles keeps one firmware image.
- Common ground: tile A GND, tile B GND and the analyser GND are joined. Without a shared ground the analyser reads the pins against a floating reference.
- Channel order: tile A on analyser channel 0 (Saleae "Channel 0", sigrok `D0`), tile B on channel 1 (Saleae "Channel 1", sigrok `D1`). Skew in the output is t_b minus t_a.
- Levels: the ESP32-S3 drives 3.3 V logic; confirm the analyser's input threshold accepts 3.3 V before connecting.
- Both tiles power up from separate USB supplies or one hub; either way the ground link above is required.

## 4. Sample rate

Choice: **1 MHz (1 MS/s) (plan choice)**. Reason: it resolves the 5 ms threshold with a margin of more than three orders of magnitude, it is a rate the fx2lafw driver lists (`SR_MHZ(1)` in libsigrok `src/hardware/fx2lafw/api.c`), and at 1 MHz the sigrok `time=true` column is in whole microseconds. On Saleae Logic 2, pick 1 MS/s, or the lowest digital rate the device offers above it.

Resolution arithmetic:

- Sample period at 1 MHz: 1 / 1,000,000 s = 1 us.
- Each edge time is known to within 1 sample, so the difference of two edges (one skew value) is known to within 2 samples = 2 us.
- Against the pass threshold of 5 ms = 5,000 us: 2 us / 5,000 us = 0.04 %, a margin factor of 5,000 / 2 = 2,500.
- Against the 16.67 ms hard limit: 2 us / 16,670 us is about 0.012 %.
- T-B5-01 tick check on the same rig (Jia Jun's row, IFM-5c interval): "every interval 16.67 ms +/- 1 ms". The +/- 1 ms band is 1,000 samples wide at 1 MHz, and one interval is known to within 2 us, so the same rate serves that check as well. That check is T-B5-01's own 30 min acceptance test, not part of the T-B4-03 verdict.
- Data size for 10 min on sigrok: 600 s x 1,000,000 samples/s = 600,000,000 samples, stored by libsigrok at 1 byte per sample for up to 8 logic channels, compressed inside the `.sr` file. Saleae stores transitions only.

## 5. Capture procedure (10 min)

Before starting, on both tiles: flash the build that contains T-B3-03, T-B4-02 and T-B5-01 with `DE_W10_TEST_PATTERN` on (T-B5-01 row), and the Pico build with T-B2-02. Origin = 0 in Pico time (rig build only, T-B4-03 row). Let both tiles reach a stable sync state (OD-10 is not yet settled; record in the log how long you waited) before starting the capture.

Also record a slow-motion phone video of both tiles side by side for at least 10 s (plan choice: long enough to see the pixel pass several positions) during the capture: the moving pixel (LED n mod 64, T-B5-01 row) shows whether both tiles are on the same tick n (section 7).

### 5a. Saleae Logic 2

1. Enable digital channels 0 and 1 only; set the digital sample rate to 1 MS/s (section 4).
2. Capture mode: timer, duration 600 s (10 min, "at least 10 minutes").
3. Start; leave the bench untouched for the full 600 s.
4. Save the capture: `docs/week10/skew_2tile/skew_2tile_capture.sal`.
5. Options (bottom right) > Export Raw Data > channels 0 and 1, format CSV, full time range. Save as `docs/week10/skew_2tile/skew_2tile.csv`. The file header is `Time [s],Channel 0,Channel 1` (names follow the channel labels in Logic 2; if you rename the channels, pass those names to `--ch-a` and `--ch-b`).

### 5b. sigrok (FX2 board with fx2lafw), command line

Check the device and its channel names first (current libsigrok names them D0 to D15):

```
sigrok-cli --driver fx2lafw --scan
```

Capture 10 min at 1 MHz on D0 and D1 to a sigrok session file:

```
sigrok-cli --driver fx2lafw --channels D0,D1 --config samplerate=1000000 --time 600s -o docs/week10/skew_2tile/skew_2tile_capture.sr
```

Convert to CSV with a time column, channel names in the label line, and one row per change:

```
sigrok-cli -i docs/week10/skew_2tile/skew_2tile_capture.sr -O csv:time=true:dedup=true:label=channel -o docs/week10/skew_2tile/skew_2tile.csv
```

The CSV starts with `;` comment lines, including `; Samplerate: 1 MHz`, then the label line `Time,D0,D1`.

### 5c. PulseView

1. Select the fx2lafw device, enable D0 and D1 only, set the rate to 1 MHz and the sample count to 600 M (600 s at 1 MHz).
2. Run; when it stops, File > Save As `docs/week10/skew_2tile/skew_2tile_capture.sr`.
3. Convert with the same `sigrok-cli -i ... -O csv:time=true:dedup=true:label=channel ...` command as in 5b. (PulseView's own CSV export uses the same libsigrok CSV output module; the command line form fixes the options.)

If a USB FX2 board cannot sustain 1 MHz for 600 s (sigrok reports a sample loss or stops early), record the error in the log and do not lower the rate without noting the new rate and recomputing section 4.

## 6. Analysis command

From the repo root, for a Saleae export:

```
python3 pc/tools/skew_analyse.py docs/week10/skew_2tile/skew_2tile.csv --ch-a "Channel 0" --ch-b "Channel 1" --out docs/week10/skew_2tile/skew_pairs.csv --plot docs/week10/skew_2tile/skew_plot.png | tee docs/week10/skew_2tile/skew_report.txt
```

For a sigrok or PulseView export:

```
python3 pc/tools/skew_analyse.py docs/week10/skew_2tile/skew_2tile.csv --ch-a D0 --ch-b D1 --out docs/week10/skew_2tile/skew_pairs.csv --plot docs/week10/skew_2tile/skew_plot.png | tee docs/week10/skew_2tile/skew_report.txt
```

- The format is detected from the file (`--format saleae` or `--format sigrok` forces it). For a sigrok CSV without the `; Samplerate:` comment, add `--samplerate 1000000`.
- `--plot` needs matplotlib; everything else is Python standard library.
- Exit status: 0 PASS, 1 FAIL, 2 bad input. Check it with `echo $?` straight after the command (with `tee`, use `echo ${PIPESTATUS[0]}`).
- Output: one `UNPAIRED channel=<name> tick=<k> t=<s>` line per edge with no partner, a counts line, `median_abs_skew_ms`, `p99_abs_skew_ms` (nearest rank: the value at rank ceil(0.99 x N) of the sorted absolute skews, no interpolation), `max_abs_skew_ms`, the count of pairs at or above 16.67 ms, and the verdict.
- Verdict rule (T-B4-03): PASS only if max absolute skew is at most 5 ms and no pair is at or above 16.67 ms. A maximum above 5 ms starts T-B4-07.

Pairing method: each edge on tile A is paired with the nearest edge on tile B when both are each other's nearest neighbour and they lie less than half a tick period (1,000,000 / 60 / 2 us) apart. A missing edge is logged with its channel and tick and does not shift the pairing of later edges. A capture that starts with a pin already high gives no rising edge at the start (the first sample sets the level only).

## 7. Points for Ahsanul and Jia Jun to settle before the first capture

1. **Pulse or toggle.** T-B4-03 says the test pin "pulses at tick n" and the tool "pairs rising edges"; T-B5-01 says "The ISR toggles the test pin". With a toggle, a rising edge comes every second tick, so the default `--edges rising` measures every second tick. Run a second pass with `--edges both`, which treats every edge as a tick; in that mode a pair joining a rising and a falling edge is reported as a NOTE (the two pins started at different levels), not as a failure. Use `--edges both` only on a toggling pin. The plan rows differ in wording; this file does not choose between them.
2. **Whole-tick slips.** Pairing within half a tick means no pair can show an absolute skew of half a tick or more: a tile one whole tick behind looks aligned on the analyser. The 16.67 ms rule is applied to every pair as the plan states, and the slow-motion video of the moving pixel (LED n mod 64, T-B5-01) is the check that both tiles are on the same tick n. Keep the video as part of the evidence.
3. **Unpaired edges.** The verdict follows the plan row (skew only). Any `UNPAIRED` line is a missed or extra tick on one tile and belongs in the log; T-B5-01's missed-tick counter should explain it.

## 8. Evidence files (in this folder)

| File | Content |
|---|---|
| `skew_2tile_capture.sal` or `skew_2tile_capture.sr` | raw 10 min capture ("Capture file") |
| `skew_2tile.csv` | analyser CSV export |
| `skew_pairs.csv` | pairs: tick, t_a_s, t_b_s, skew_ms ("skew CSV") |
| `skew_plot.png` | skew against tick ("plot") |
| `skew_report.txt` | analyser output with median, p99, max and verdict |
| `skew_video.mp4` | slow-motion video of both tiles (section 7 point 2) |

## 9. Format sources

- Saleae, "Binary and CSV Export Formats - 2025 Update": digital CSV header `Time [s],<channel names>`, "Each row represents a time point where at least one channel changes state", "Only state transition times are included (not every sample)", values 0, 1 or X, time in seconds relative to the base time. https://www.saleae.com/support/logic-software/saving-and-exporting-data/binary-and-csv-export-formats-2025-update
- Saleae, "Exporting Data": menu path Options > Export Raw Data; "Digital channel entries are only recorded in the export file when data changes"; a new entry is always created on the final digital sample. https://www.saleae.com/support/logic-software/saving-and-exporting-data/exporting-data
- libsigrok CSV output module source, `src/output/csv.c`: options `comment` (default `;`), `header` (default true, writes `; CSV generated by ...`, `; Channels (n/m): ...` and `; Samplerate: ...`), `label` (`units` default, `channel`, `off`), `time` (default false), `dedup` (default false, forced off without time); with `label=units` the time column is labelled with its unit (for example `microseconds`) and each logic column `logic`; one row per sample unless `dedup` drops repeats. https://github.com/sigrokproject/libsigrok/blob/master/src/output/csv.c
- libsigrok `src/strutil.c`, `sr_samplerate_string()`: sample rate text such as `1 MHz`. https://github.com/sigrokproject/libsigrok/blob/master/src/strutil.c
- libsigrok fx2lafw driver, `src/hardware/fx2lafw/api.c`: rate list including 1 MHz, channel names D0 to D15. https://github.com/sigrokproject/libsigrok/blob/master/src/hardware/fx2lafw/api.c
- sigrok-cli manual page: `-O csv:dedup:header=false` option syntax, `--config samplerate=1000000` ("specifies the samplerate in Hz"), `--time 3s`, `-C/--channels`, `-o`. https://man.archlinux.org/man/sigrok-cli.1.en
