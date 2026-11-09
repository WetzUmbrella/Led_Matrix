# Feasibility bench, 9 Nov 2026 (phase 2 of `docs/plan/game_plan_v2.md`)

Answers section 7 of the plan (F-01 to F-08) before design starts. Branch `feature/whack-a-mole` @ `12f2f8d`. Linux laptop, joined to the "Matrix" router.

## Already done from the laptop (9 Nov)

| Check | Result |
|---|---|
| F-01 compile: tile firmware (ESP-IDF 5.3, esp32s3) | PASS, 0 warnings |
| F-01 compile: Pico firmware (Pico SDK 2.3.1, pico_w) | PASS, 0 warnings, `ledwall_sync.uf2` |
| Tap logger `origin/tools/imu-tap-log` compile | PASS, 0 warnings |
| Host gate (C tests, ASan, barr_check, pytest) | All PASS, pytest 423 passed, 1 skipped |
| F-03 config | **PSRAM is off**: `CONFIG_SPIRAM is not set` in `panel_sync/sdkconfig`. Whether the chip has PSRAM is checked in step 1. |
| Orientation settle time (from code) | 26 samples at 50 Hz = **520 ms** of steady gravity before a turn is accepted (`orient_filter.h`) |

Python tools run from the project venv: `.venv/bin/python` (pyserial, Pillow, pytest; made 9 Nov, git-ignored locally).

## Setup (once per terminal)

```
cd ~/SIT/Y2/inf2004/project/Led_Matrix
. ~/esp/esp-idf/export.sh
export PICO_SDK_PATH=~/pico/pico-sdk
```

You need a USB hub so that 2 or more tiles can be on the laptop at once (step 3). Write down which board number sits at which `/dev/ttyACM<n>` as you plug them in.

## Step 1: flash the branch firmware, boot check, PSRAM (F-01, F-03), 20 min

1. Pico: hold BOOTSEL, plug it in, then:
   ```
   cmake -S ledwall_sync -B ledwall_sync/build -DPICO_BOARD=pico_w && cmake --build ledwall_sync/build -j8
   cp ledwall_sync/build/ledwall_sync.uf2 /run/media/$USER/RPI-RP2/
   ```
2. Each tile, one at a time on USB:
   ```
   esptool.py --chip esp32s3 -p /dev/ttyACM0 chip_id      # record the "Features:" line
   (cd panel_sync && idf.py -p /dev/ttyACM0 build flash monitor)
   ```
   Leave the monitor running for about 10 s, then press Ctrl+] to exit.
3. Record for each tile, from the monitor:
   - the `Features:` line: does it say `Embedded PSRAM 2MB`?
   - `ORIENT imu ready` or `not found`
   - any `TAP interrupt not available` or `TAP engine not configured`
   - the `HEAP largest_internal=... largest_spiram=...` line
4. Power the Pico and all tiles from the charger. Then run:
   ```
   .venv/bin/python pc/dashboard.py --pico 192.168.1.154 --session 6
   status
   ```
   Every tile should show online.

**Record:** PSRAM yes or no per tile, the IMU line, any TAP error line, the `online=` value.

**Decision it feeds:** D-09 (every tile stores the whole wall). If the chip has PSRAM, enabling it is a config change. If not, the design has to read frames from flash instead of RAM.

## Step 2: does the tap interrupt fire? (F-02), 10 min

1. One tile on USB with the branch firmware: `(cd panel_sync && idf.py -p /dev/ttyACM0 monitor)`.
2. Tap the tile 10 times with a finger, about 1 s apart.
3. Each tap should print `TAP result=<r> t_us=... peak=...`. Result codes:
   - 0 = accepted
   - 1 = bad status
   - 2 = too soon after the last tap
   - 3 = too weak

**Record:** how many of the 10 taps printed a line, and the result codes.

- **No line at all:** the interrupt is not on INT1/GPIO10. Do not change code yourself; tell me, and I will build the INT2/GPIO13 variant (`OR_TAP_INT_LINE 2`).
- **Mostly result 3:** the tap threshold is too high, which step 5 settles.

## Step 3: turn timing, one tile and the whole board (F-07), 20 min

Branch firmware on 2 or more tiles, all on USB to the laptop. Power the Pico from the charger, not the laptop, so its port is not in the list. Log every tile at once:

```
for p in /dev/ttyACM*; do .venv/bin/python pc/tools/serial_log.py --port $p --out docs/bench/9nov/f07_$(basename $p).log --test F07 --who Cheston --minutes 6 & done; wait
```

**a) One tile.**
1. Turn the tile 90° and stop it firmly.
2. Tap it once straight away. The tap is the time marker.
3. Wait 2 s.
4. Repeat 5 times each way.

The gap between each `TAP result=` line and the next `ORIENT changed` line is the settle time.

**b) Whole board.**
1. Turn the whole board 90° clockwise in one movement, then hold it still for 3 s.
2. Turn it back.
3. Then 90° anticlockwise, and 180°. Do each 3 times.

The spread of the `ORIENT changed` lines across the tiles, for one turn, sets the E3 window (planned 1.5 s). Note whether a 180° turn passes through 90 first.

**Record:** the log files. I will read the times out of them.

## Step 4: upload time (F-04), 15 min

Wall on the branch firmware, all 5 tiles, Pico on the charger. In the dashboard:

```
.venv/bin/python pc/dashboard.py --pico 192.168.1.154 --session 7
config docs/bench/9nov/noise_1x5_600.gif 1x5 <map> pause 8
build
upload
```

- `<map>` is your 5 board numbers left to right, then four zeros, for example `1,2,3,4,5,0,0,0,0`.
- Time the `upload` with a stopwatch, from Enter to the `failed: none` line.
- Each tile gets 600 frames x 96 B = 57.6 KB.
- Then run the same with `docs/bench/9nov/numbered_1x5.gif` (100 frames) and `--session 8`. That run also checks video still plays on this branch: type `start`, and the tiles should show their numbers left to right.

**Record:** both times, any `failed:` tiles, and whether the numbers play in sync.

**Decision it feeds:** NR-13 and the frame cap for D-09. A 3x3 whole-wall asset is 9 times the data per tile.

## Step 5: tap strength data with the logger (F-05), 45 min

Run this after steps 1 to 4, because it replaces the panel firmware.

1. Flash the logger on every tile:
   ```
   git worktree add ../taplog origin/tools/imu-tap-log
   (cd ../taplog/tools/imu_tap_log && idf.py set-target esp32s3 && idf.py -p /dev/ttyACM<n> build flash)
   ```
2. Put all tiles on the board as at the demo.
3. Log all tiles, with the same loop as step 3:
   ```
   for p in /dev/ttyACM*; do .venv/bin/python pc/tools/serial_log.py --port $p --out docs/bench/9nov/f05_$(basename $p).log --test F05 --who Cheston --minutes 10 & done; wait
   ```
   Run it from `Led_Matrix`. It logs every tile on the laptop. Use the hub, with as many tiles as you can.
4. Do the actions below. In `docs/bench/9nov/f05_taps.csv`, write one row per action: the wall time, the tile, and the kind. The tile is the board number. Write `board` for knocks.

| Kind | Action | Count |
|---|---|---|
| `c` | **Soft** fingertip tap, the weakest a 7-year-old would do | 20 per tile |
| `e` | **Open-palm slap**, the hardest an excited child would do | 20 per tile |
| `k` | Knock on the board edge, away from all tiles | 10 |
| `b` | Press the tile back into its Blu Tack | 5 per tile |

Then leave 30 s with no one touching anything, and write no rows for it.

Put `# c = soft tap, e = palm slap` as the first line of the CSV. The tools read `c` and `e` as deliberate taps, so this keeps the meaning clear.

5. Analyse:
   ```
   .venv/bin/python ../taplog/tools/imu_tap_log/marks.py --taps docs/bench/9nov/f05_taps.csv docs/bench/9nov/f05_*.log
   .venv/bin/python ../taplog/tools/imu_tap_log/summarise.py --taps docs/bench/9nov/f05_taps.csv docs/bench/9nov/f05_*.log
   ```

Check the `H,` line says `src=fifo` and the `S,` lines show about 1000 samples a second. If not, stop and tell me.

**Decision it feeds:** the NR-01 and NR-02 thresholds, and whether a hard slap on one tile looks like a tap on its neighbour.

## Step 6: what a turn looks like to the sensor (F-06), 15 min

Logger still on the tiles. Use a new log and CSV, so turns are not mixed with Blu Tack presses:

1. Run the step 5 logging loop again with `f06_` in place of `f05_`.
2. Turn one tile 90° and back, 10 times, on 2 tiles. Write each turn as kind `b` in `docs/bench/9nov/f06_taps.csv`.
3. Add 5 soft taps (`c`) on each of those tiles, so the clocks can be aligned.
4. Run `summarise.py` on the f06 files. The "highest press" column is now the highest turn peak.

**Decision it feeds:** G5. If turn peaks reach the soft-tap peaks from step 5, taps must be blocked while a tile turns.

## Step 7: pivot trial (F-08), 15 min

1. Mount one tile on a pivot (bottle cap, or a pin through a coaster at the centre).
2. Leave enough slack in the USB cable for a quarter turn each way.
3. Do 20 turns each way at child speed.
4. Then tap it 10 times.

**Record:** does it turn easily, stop at 90°, stay put when tapped, and keep the cable in? Take a photo.

## Finish

Reflash the branch panel firmware (step 1.2) on every tile you used for the logger, so the wall is back to normal.

Hand back: this file with the Record lines filled in, the `f05_*`, `f06_*` and `f07_*` logs, both CSVs, the summarise output, and the photo.

## Results

| Check | Result | Notes |
|---|---|---|
| F-01 flash and boot | PASS on 1 Pico + 2 tiles (nodes 4, 5) | Branch firmware flashed 9 Nov; IMU ready on both; tiles join Matrix and REGISTER; Pico `online=0x018 sync=0x018`. Other 3 tiles with a teammate. |
| F-02 tap interrupt | Interrupt fires on INT1/GPIO10 | Node 5 printed `TAP result=3 ... status1=0x02 tap=0x11` at boot (rejected as weak, peak 58). Step 2 (log `f02_f07_node*.log`, desk, not the board): about 20 taps per tile gave 12 interrupts on node 4 and 9 on node 5, so roughly half the taps never reached the firmware (tap count to confirm). Soft taps rejected as weak at peaks 1,078 to 2,024 (threshold 2,048). One tap on node 4 (peak 13,354) was also accepted on node 5 (2,942) 2 ms apart; dedupe picks the right tile. Repeat interrupts 100 to 150 ms after a tap are dropped by the refractory time. |
| F-03 PSRAM | Chip: 2 MB PSRAM on both tiles; firmware: off | `esptool chip_id`: `Embedded PSRAM 2MB (AP_3v3)`. Boot: `largest_spiram=0`, `largest_internal` 139-156 KB. Enabling is a config change (`CONFIG_SPIRAM`). |
| Network (9 Nov, 16:00) | Wall runs on a phone hotspot | The Matrix router was in use by a teammate. The home router (WPA2/WPA3 mixed) refused the Pico W: every join failed, and selecting mixed WPA2/WPA3 in the firmware did not help. On the hotspot (2.4 GHz, WPA2) the Pico `…3B6838` joined in 3 s and both tiles registered (`online=0x018 sync=0x018`). Beacon loss 0.75% and 1.25% over 400 beacons. A phone hotspot may cap the number of clients: check before 9 tiles. |
| F-04 upload time | 5.2 s per tile for 57.6 KB (600 frames x 96 B), about 11 KB/s, 0 resends. Paced by the uploader, not the network. Two bugs found. | Logs `f04_upload_*.log`. The pace is set by `pc/uploader.py`: windows of 4 chunks (the tile's Upload Queue length), 0.25 s ACK wait, 1 s manifest settle. Tiles are uploaded one after another. D-09 estimate for a 3x3, 600-frame whole-wall asset: 506 chunks, about 37 s per tile, about 5.5 min for 9 tiles one by one; about 40 s if all tiles upload at once. Needs parallel uploads, a bigger window (PSRAM) or a frame cap. **Bug A (session race):** right after SESSION_CONFIG the first tile gets its MANIFEST before a beacon carries the new session; the tile drops it as `upload_wrong_session`, and the uploader resends chunks but never the MANIFEST, so it fails after 20 rounds (90 s). A retry in the same session works at once (5.2 s). This is most likely the 5 Nov "re-upload hang". **Bug B:** one upload ended at 55 of 57 ACKed, while the Pico still saw the tile READY (lost CHUNK_ACKs, known gap R1-8). Playback started on nodes 4 and 5 (`COORDINATING PLAYBACK`). |
| F-05 tap peaks | Partial (game firmware, upright board, round 2 `r2_node*.log`) | 20 taps per tile: the sensor signalled 17 (node 4) and 16 (node 5); accepted 13 and 14; the rest were weak (974 to 1,970, under 2,048). So about 70% accepted against the 95% target. Neighbours: every tap that shook the board was also accepted on the other tile, 2 to 3 ms apart. In 2 of 4 such pairs the untapped tile had the higher peak (for example 7,165 against 5,201), so the highest-peak rule picks the wrong tile. Turning node 5 alone gave 8 false taps on node 4, which was never touched (peaks up to 20,617). Soft and firm are not separated (start times not noted); the logger run (step 5) does that. **Logger run** (`f05_tap_*.log`, `f05_taps.csv`, `f05_summary.txt`; tiles upright against one shared box; logger fixed first: baseline reseed after 200 ms, polling at 978 samples/s, because node 5's FIFO data was garbage). Soft taps: 17 of 20 over 300 mg on node 4 and 20 of 20 on node 5. Palm slaps: all over 359 mg. Quiet: max 105 mg (node 4) and 994 mg (node 5). Neighbours: a soft tap usually leaves the other tile under 300 mg (31 of 40). A palm slap always reaches it, often as hard as a real tap (node 5 median 11.6 g when node 4 was slapped). **Highest-peak rule: right in 41 of 49 cases where both tiles fired (84%), wrong in 8.** "First tile over the threshold" cannot be judged here: tile alignment from PC receive times is only good to about 100 ms; it needs the Pico clock. A shared box passes slaps strongly: mounting isolation (foam) is a design option. |
| F-06 turn peaks | Turns look like a medium tap: G5 is needed | `f06_turn_*.log`, `f06_taps.csv`, `f06_summary.txt`; 20 turns per tile, upright against the shared box. The turned tile peaks at 0.9 to 2.2 g (median 1.3 to 1.5 g). That is above the weakest soft taps (157 to 326 mg) and below most firm taps (5 to 17 g), so no single threshold separates them. The other tile: up to 0.75 g and 1.0 g; over 300 mg in 2 of 20 and 13 of 20 turns, over 1 g in 1 of 40. Design input: ignore taps on a tile while it turns (G5), and on its neighbours for the same time. Telling them apart by shape is possible: a tap is a short spike, a turn a slow swell over many 10 ms windows. |
| F-07 settle and board-turn spread | Partial | One tile: the turn is reported 0.2 to 0.6 s after the tile stops (3 turns with a marker tap; the other marker taps were missed). Whole board: node 5 reported all 18 turns correctly, and a 180° turn went straight to 180 with no stop at 90. Node 4 reported no turn in part C (cause open), so the spread across tiles is not measured yet. Round 2: node 5 got 10 of 10 single turns. Where a marker tap registered, the turn was reported 0.4 to 0.8 s after it (the code's 520 ms rule; the replan said about 300 ms). Whole board: node 5 got 3 of 3 clockwise, 2 of 3 anticlockwise (one missed, or only 2 done) and 3 of 3 at 180°. Node 4 again reported nothing. Round 4 (`r4_node*.log`, both held vertical by hand): node 4 alone got 4 of 4 turns; both together got 4 of 4 turns on both tiles, the same direction, and the two tiles' reports were at most 0.2 s apart (the PC time stamps have about 0.2 s resolution). So the 1.5 s E3 window has plenty of margin. Cause of the earlier misses: node 4 was held tilted back toward face-up. The filter ignores a sample with less than 0.5 g of gravity across the tile's face, so a tile must stay at least about 30° from flat (constraint for the board). |
| F-08 pivot | NOT RUN | Physical trial still to do: one tile on a pivot, 20 turns each way, then 10 taps. |
| Colour order on this branch | FAIL as predicted | The numbered pattern showed the Node ID green and the frame counter red, but the pattern draws the Node ID red (`pc/tests/numbered_pattern.py`). Confirms D-13 (bring in the 5 Nov R, G, B fix). Both tiles changed together by eye. Sync skew not measured (no pico_tap board). |
| Pico `…3B6838` WiFi | Unreliable | Hung while joining 4 times (`[CYW43] do_ioctl ... timeout`, or "Joining" and then nothing). A power cycle cleared it each time. The 5 Nov Pico `…595138` never hung. Use `…595138` for the demo unless `…3B6838` proves stable. |

## How this bench differed from the plan

- **2 tiles** (nodes 4 and 5), not 5. The other tiles were with a teammate.
- **Network:** first Matrix, then a phone hotspot (172.20.10.0/28, 2.4 GHz, WPA2). The Matrix router was in use by a teammate's wall, and two coordinators on one router make tiles lock onto whichever Pico they hear first (`coord_lock`, `foreign=` counter). The home router (WPA2/WPA3 mixed) refused the Pico W.
- **Mounting:** no cardboard board. Rounds 1 to 4 held the tiles by hand; steps 5 and 6 stood them upright against one shared box. Neighbour numbers on the real board will differ.
- **Tap logger fixed before use** (`tools/imu_tap_log_fixes.patch`, local to `origin/tools/imu-tap-log`, not committed): (1) the baseline was seeded from the first sample and the 100 mg hold never released, so the logger reported a constant 16 to 23 g at rest; now it reseeds after a hold of 200 samples. (2) Node 5's FIFO data was garbage, so both tiles use polling (`ITL_SOURCE_FIFO 0`, 978 samples/s, 0 overruns). After the fix: rest median 8 and 13 mg.
- **Times:** steps 5 and 6 used `prompter.py`, which writes the CSV itself. Rounds 1 to 4 had no CSV, so actions were matched by the gaps in the logs. `marks.py` alignment failed for step 5 (soft taps under its 300 mg alignment threshold), so `tools/f05_analyse.py` aligns on the PC receive times instead (good to about 100 ms between tiles, enough for peaks, not for "who was first").

## Evidence index (`docs/bench/9nov/`)

| Check | Files |
|---|---|
| F-01, F-02 boot and first tap | `raw/f01_boot_node4.log`, `raw/f01_boot_node5.log`, `raw/f01_boot_pico_595138.log` |
| F-03 PSRAM | `raw/f03_chip_id.txt`; HEAP lines in the boot logs |
| F-02, F-05, F-07 game firmware rounds 1 to 4 | `f02_f07_node*.log` (round 1), `r2_node*.log` (round 2), `r3_*.log` (identity check), `r4_node*.log` (round 4) |
| F-04 upload | `f04_upload_600.log`, `f04_upload_100.log`, `f04_upload_100_retry.log`, `f04_upload_100_sameSession.log`, `raw/f04_node4_wrong_session.log`; GIFs `noise_1x2_600.gif`, `numbered_1x2.gif` |
| F-05 taps | `f05_tap_4.log`, `f05_tap_5.log`, `f05_taps.csv`, `f05_summary.txt`; logger faults `raw/f05_logger_*.log` |
| F-06 turns | `f06_turn_4.log`, `f06_turn_5.log`, `f06_taps.csv`, `f06_summary.txt` |
| Network and Pico | `raw/net_*.log`, `raw/pico_3B6838_*.log` |
| Tools | `prompter.py`; `tools/relog.py`, `tools/grab.py` (serial capture), `tools/upload_run.py`, `tools/upload_run2.py` (timed dashboard runs), `tools/f05_analyse.py`, `tools/f06_analyse.py`, `tools/imu_tap_log_fixes.patch` |

Network names and passwords are replaced by `<network>` in the raw logs.

## Design inputs from this bench

1. Taps: detect in software from polled samples at about 1 kHz, not the QMI8658 tap engine at 100 Hz; threshold 100 to 300 mg (to tune on the real board).
2. Neighbours: "highest peak wins" alone is right 84% of the time. Add foam mounting, tap times on the Pico clock, and the rule that only the target tile can score.
3. Turns: ignore taps on the turning tile and its neighbours while it turns (G5); a turn is a slow swell, a tap a short spike.
4. Turn detection: works; the E3 window of 1.5 s has margin (tiles at most 0.2 s apart); the 520 ms settle makes NR-04 (500 ms) tight; the wall must stay at least about 30 degrees from flat.
5. Upload: fix the session race (resend MANIFEST, or wait for the tile's beacon session); upload tiles in parallel and use PSRAM for D-09; R1-8 lost ACKs still open.
6. Bring in the R, G, B colour order fix (D-13).
7. Network: a dedicated 2.4 GHz WPA2 router for the demo; the Pico W cannot join WPA2/WPA3 mixed networks; one coordinator per network.
