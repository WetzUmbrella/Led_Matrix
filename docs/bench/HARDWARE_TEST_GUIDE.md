# Hardware test guide: Led_Matrix LED video wall

Version for the bench on 9 Nov 2026. Written so that an AI assistant with no access to the repository, the chat history or the team can guide a person through every hardware test and collect the results.

---

## 0. Instructions to the AI, and rules for every card

**You are guiding a tester through hardware tests on a small LED video wall.** You cannot see the hardware or the code. The tester has the hardware on a bench, runs the commands you give, and reads you the output or the log lines. You decide each result by comparing what the tester reports against the pass rule on the card. You do not decide it by how the wall looks in general.

How to run the session:

1. **Ask for one result at a time.** Ask for the single line, number or observation the card names. Do not ask for a whole log at once.
2. **Never skip the pass or fail check.** Before you move on, compare the reported line with the pass rule on the card. If the line is missing or unclear, ask for it again. Do not assume it passed.
3. **Never invent or fill a gap.** A result you have not seen is NOT RUN, not PASS. A test with an unexpected but explained result is PARTIAL or FAIL, with the reason written down.
4. **Keep the results table (section 5) up to date** after each card, and show it to the tester when a card closes.
5. **Give only the commands in this file.** Where a command or value says "(check: <file>)", ask the tester to open that file and read the value back to you. Do not guess flags.
6. **Do not change code.** If a build fails, stop that card. Collect the full error output and hand it back using section 7. Do not suggest a code fix as a result.
7. **Use the triage table (section 6)** when something goes wrong, before you decide a cause.

Rules that apply to every card:

- **Never commit `wifi_secrets.h`.** Both copies are git-ignored. Do not paste the router password into any file that goes into git, and keep it out of the results table.
- **Power the coordinator Pico first, then the tiles.** Wait for the Pico to show its state before you power the tiles.
- **Power-cycle the coordinator Pico after any router change** on master. On the feature branch, F1 tests whether the Pico recovers by itself.
- **Only one coordinator Pico on the router at a time.** A second beaconing Pico corrupts every tile's clock. The pico_tap Pico is fine, because it does not join WiFi.
- **Only one program may open a COM port at a time.** Close the serial logger before you start `idf.py monitor` on the same port, and the other way round.
- **Use one tile and Pico firmware family per wall.** Never mix firmware from `master` and `feature/whack-a-mole` on one wall. The message sizes differ, so the failure looks like a broken network rather than a version mismatch.
- **Between boards, connect only signal and GND wires.** Never join 3V3 or 5V pins.
- **Never run `idf.py erase-flash` or esptool `erase_flash`.** It wipes the board number in NVS. If it happens, rerun `set_board_number.sh` for that board.
- **Some tiles reboot into download mode after a software reset.** If a tile sits at `boot:0x0 (DOWNLOAD...)`, unplug and replug its USB cable instead of pressing RESET.
- **Record which USB source powers each tile** (for example "demo charger port 2" or "laptop USB"). Power sources change the result of H2 and F5.
- **Start the serial logger before step 1 of a card**, and stop it (Ctrl+C) after the last step. Decide pass or fail from the log lines the card names, not from how it looked.
- **Keep the evidence.** Every file goes in `docs/bench/<date>/`. For runs on 9 Nov, use `docs/bench/9nov/`. A failure is still evidence: keep the log and mark the card FAIL with a one-line reason.
- **Check which branch is checked out** before each build or flash. Use `git branch --show-current` and `git rev-parse --short HEAD`, and record the short hash.

The serial logger, used on all cards:

```
python pc/tools/serial_log.py --port COM<n> --out <evidence path> --test <card> --who <name> --tile <board>/<MAC> --image <branch>-<short hash> --net A --pico A
```

Use the same header fields on every card. `<board>` is the tile's board number, and `<MAC>` is its WiFi MAC address, or `pico` for the coordinator.

---

## 1. System overview

**The wall.** Five tiles now and nine later, each a Waveshare ESP32-S3-Matrix board. Each tile is a small LED panel with an ESP32-S3 controller. Each tile has a permanent board number from 1 to 9, stored in its NVS flash. The tile's WiFi hostname is `ESP-<board number>`. Tiles show an uploaded GIF, and in game mode they draw a mole and report taps.

**The coordinator.** A Pico W named `PICO-Main` runs the coordinator. It joins the router, sends beacons, sends the session configuration and assets, and starts and stops playback. On the feature branch it also runs the whack-a-mole game. It prints status lines over USB serial. Its fixed IP comes from a DHCP reservation on the router. On 6 Nov it was 192.168.1.154 (check: the value your Pico logs on `Joined <router>, ip=...`).

**The PC.** Two front ends run on the laptop, and you use only one at a time (check: `pc/web.py` before running both):

- `pc/dashboard.py` is an interactive shell. It builds and uploads assets and starts playback.
- `pc/web.py` is a small website on port 8080 over the same shell. It is for the feature branch only.

**The router.** An ASUS router on the 2.4 GHz band, with WPA2-Personal (AES), AP isolation off, DTIM interval 1, and no internet uplink. The laptop must be on this router's network. A laptop on another WiFi gives "no TELEMETRY yet" (see section 6).

**The tap sensor.** Each tile has a QMI8658 motion sensor (IMU) on I2C, with SDA on GPIO11 and SCL on GPIO12. A tap is detected from the sensor's tap interrupt. The firmware assumes the interrupt is on INT1, GPIO10 (`OR_TAP_INT_LINE` set to 1 in `panel_sync/main/orientation.c`). INT2 on GPIO13 is the other option. Card H5 answers which pin is wired, and the answer is not yet known.

**The test pin.** GPIO4 on the tile header is the test pin (`DE_TEST_PIN` in `panel_sync/main/display_engine.h`). It is on the right edge, 4th hole up from the bottom, between pins 3 and 5 on the header. No header pins are fitted, so the wire needs a way to grip. As the code stands, GPIO4 carries only the tick pulses: 1 ms pulses, and a 4 ms pulse every 64th tick.

**The pico_tap logger.** A spare Pico W with five input channels (GP2 to GP6), used as an edge recorder. It timestamps the edges on each tile's GPIO4. `pc/tools/pico_tap_capture.py` records them, and `pc/tools/skew_analyse.py` reads the result. It has not run on hardware before 9 Nov, so calibrate it first (card H3).

**The branches.**

| Branch | Use | Notes |
|---|---|---|
| `master` | Cards H1 to H7 (baseline) | Wire format is the old one. |
| `feature/whack-a-mole` | Cards F0 to F8 (new code) | Layout as rows and columns, website, game, rotation work. Never compiled on the bench as of 9 Nov. |
| `origin/tools/imu-tap-log` | Card H6 (tap feasibility logger) | A separate firmware for one board at a time. Flash it over the panel firmware, then reflash the panel firmware afterwards. |

**The game (feature branch).** Rules v1, from `ledwall_sync/game_rules.h`:

- One mole at a time, on a random active tile. The same tile never comes up twice in a row, unless only one tile is active.
- A mole stays up for 1200 ms at the start of a 30 s round, shrinking linearly to 500 ms at the end.
- A 300 ms gap separates one mole from the next.
- A hit (a tap on the mole tile while it is up) scores +1 and ends the mole at once.
- A tap on any other active tile, or any tap while no mole is up, scores -1.
- A mole that times out with no tap scores 0.
- Taps from several tiles within 50 ms count once. The highest peak wins.
- A tap that arrives up to 100 ms after the mole hides still counts as a hit.

**Targets** (from the re-plan):

- Sync skew under 5 ms between any two tiles.
- Tap to hit flash under 100 ms.
- Zero double-counted taps over 50 deliberate hits.

---

## 2. Status as of 9 Nov

Recorded by the developer on 9 Nov. Treat these as the starting state, and confirm it with the tester before the first card.

| Card | State on 9 Nov |
|---|---|
| H1 router and fixed IP | PASS |
| B1 GPIO4 location | Answered: GPIO4 is on the header, right edge, 4th hole up from the bottom, between pins 3 and 5. No header pins fitted. |
| H2 five tiles on master | PARTIAL: 4 tiles seen, and the 10 minute result is missing. |
| H3 pico_tap calibration | NOT RUN |
| H4 baseline skew | NOT RUN |
| H5 IMU interrupt pin | NOT RUN |
| H6 tap feasibility | Unblocked: the logger is now on `origin/tools/imu-tap-log`. |
| H7 rotation on one tile | NOT RUN |
| F0 to F8 (feature branch) | NOT RUN. The feature-branch firmware has never been compiled on ESP-IDF or the Pico SDK. |

The H2 partial run is recorded in `docs/bench/6nov/h2_pico_5tile.log`. Tile 5 was not powered at the time, and the tiles were powered from several USB sources.

---

## 3. Part A: master baseline

Run these on `master`. Check the branch first with `git branch --show-current`, and record the hash with `git rev-parse --short HEAD`.

Tile flashing (from Windows, on the board's COM port, from the ESP-IDF 5.3 prompt). Do not attach the board to WSL, because it then disappears from Windows and its COM port cannot be used:

```
cd panel_sync
idf.py set-target esp32s3            # first time only
./set_board_number.sh <n> COM<n>     # first time per board only, unique number 1 to 9; run in Git Bash
idf.py -p COM<n> flash monitor
```

Expect `I am ESP-<n>`, then `joined <router>`, then `Pico found at ...`. Exit the monitor with `Ctrl+]`. If flashing hangs at "Connecting...", hold BOOT, tap RESET, release BOOT, and retry (check: `README.md`, Build and flash).

Coordinator Pico:

```
cd ledwall_sync
mkdir -p build && cd build
cmake .. && make -j4
```

Hold BOOTSEL, plug in the Pico, and copy `build/ledwall_sync.uf2` onto the RPI-RP2 drive. The serial log should show `Joined <router>, ip=... bcast=...`.

Evidence on 9 Nov: a flashed Pico log, and tile logs, as the card says.

### H1. Router and the Pico's fixed IP (15 min)

Status on 9 Nov: PASS. Repeat only if the router is changed.

**Steps:**

1. On the router's admin page, set the 2.4 GHz band as in the README router setup: WPA2-Personal (AES), AP isolation off, DTIM interval 1, 20 MHz width. Leave the channel as it is. Changing the channel while WiFi was being edited made the network vanish for a while.
2. Disconnect the router's WAN (internet) cable.
3. Flash and power the coordinator Pico. Note the IP and MAC from `Joined <router>, ip=...`.
4. On the router, add a DHCP reservation for that MAC.
5. Power-cycle the Pico 3 times and log each boot.

**Pass:** the Pico shows the same reserved IP on all 3 boots.

**Evidence:** `docs/bench/<date>/h1_pico_ip.log`. Record the router settings and a screenshot of the DHCP reservation page.

### H2. Five tiles on master, video playback (30 min)

**Why:** this is the baseline. It shows that the boards, power and network work before any new code is involved. Every later card assumes it passes.

**Steps:**

1. Power the coordinator Pico first, and start its serial log. Wait for the Pico to show state 2 (`WAITING_FOR_SESSION`).
2. Power all 5 tiles from the **one multi-port USB charger** that will be used at the demo. Record the port each tile uses. Each tile must already have a unique board number from 1 to 5. If the Pico logs `IGNORED ... board number` or `IGNORED ... already`, fix the board with `set_board_number.sh`.
3. Set the PC's WiFi power save off. On Windows that means both of these:
   - the Wi-Fi adapter's Power Management tab in Device Manager: untick "Allow the computer to turn off this device to save power";
   - the power plan: Advanced power settings, Wireless Adapter Settings, Power Saving Mode, set to Maximum Performance.
4. Start the dashboard on the laptop, on the router network. Use a fresh session number, different from the last run, or leave `--session` out to get a fresh one:

   ```
   python3 pc/dashboard.py --pico <reserved IP from H1>
   ```

5. In the dashboard shell, run these, one at a time. Any small GIF works. Master accepts 1x1, 2x2 or 3x3 layouts only:

   ```
   config <your.gif> 3x3 1,2,3,4,5,0,0,0,0 pause 8
   build
   upload
   status
   start
   ```

6. Start the Pico log before `start`, and leave the wall playing for **10 minutes**.

**Pass (all four):**

- The Pico's `CL_STATS` line shows `online=0x01f`, meaning all 5 tiles are online. Bit 0 is tile 1, so 0x01f is tiles 1 to 5.
- No `LEAVE` or `JOIN` pairs on the Pico after the start.
- No fresh-boot lines in any tile log. A reboot means a power or brightness problem.
- All 5 tiles play the GIF the whole time.

**Watch (not a pass condition):** `MISSED_DEADLINE`. Record whether the `missed=` count on the `DE ticks=` lines grows during playback.

**Evidence:** `docs/bench/9nov/h2_pico_5tile.log`, plus a 10 second video of the wall.

**If it fails:** check the README troubleshooting table, then section 6 of this guide. If tiles reboot only when all 5 are on the charger, try them on separate USB power to see whether the charger is the cause. Write down what you found.

**Lessons from 6 Nov to apply:**

- Wait for the Pico to show state 2 and every tile online before running `config`.
- Power-cycle the tiles, and restart the dashboard with a fresh session, before a new run.
- Run `config`, `build` and `upload` once. Do not retype `config` or restart the dashboard during a run.
- Use `ls -l /dev/serial/by-id/` (on WSL or Linux) to tell the Pico from the tiles when more than one port is attached.

### H3. pico_tap: flash and calibrate (15 min)

**Why:** the spare Pico W is the logic analyser for sync timing. Its README says it has not run on hardware yet, so nothing it measures counts until it passes calibration.

**Steps:**

1. Build it (`PICO_SDK_PATH` must be set, check: `README.md` Prerequisites for the SDK version):

   ```
   cd tools/pico_tap && mkdir -p build && cd build && cmake .. && make
   ```

   Hold BOOTSEL, plug in the spare Pico, and copy the `.uf2` from `tools/pico_tap/build/` onto the RPI-RP2 drive. Power it from the laptop that records.
2. Wire **one** tile's GPIO4 to **both** GP2 and GP3. Join that tile's GND to the pico_tap GND. The tile must be running and joined, as in H2.
3. Record 60 seconds:

   ```
   python pc/tools/pico_tap_capture.py --port COM<n> --channels 2 --seconds 60 --out docs/bench/9nov/calib.csv
   python pc/tools/skew_analyse.py docs/bench/9nov/calib.csv --ch-a "Channel 0" --ch-b "Channel 1" --marker --out docs/bench/9nov/calib_pairs.csv > docs/bench/9nov/h3_calibration_report.txt
   ```

**Pass (all three):**

- max skew **under 0.05 ms**
- `TICK IDENTITY CHECKED` printed
- `dropped=0` in the capture summary

**If it fails:** the tool is wrong, not the tile. Do not use pico_tap for evidence. Tell the developer (Ahsanul). The fallback is the gusmanb LogicAnalyzer firmware on the same Pico.

**Also record:** a photo of the wiring on the tile header, with the evidence.

### H4. Baseline sync skew on master (40 min)

**Why:** the evaluation needs a before and after against the under-5 ms target. This card is the "before".

**Wiring** (pico_tap pins to tile test pins):

| pico_tap pin | Channel | Connect to |
|---|---|---|
| GP2 | Channel 0 | tile A GPIO4 |
| GP3 | Channel 1 | tile B GPIO4 |
| GP4 | Channel 2 | tile C GPIO4 |
| GP5 | Channel 3 | tile D GPIO4 (optional) |
| GND | | a separate ground wire from each probed tile |

Probe 3 or 4 tiles at a time, never more.

**Steps:**

1. All tiles playing as in H2, and joined, before any capture starts.
2. Two-tile run, 600 seconds:

   ```
   python pc/tools/pico_tap_capture.py --port COM<n> --channels 2 --seconds 600 --out docs/bench/9nov/h4_skew_2tile.csv
   python pc/tools/skew_analyse.py docs/bench/9nov/h4_skew_2tile.csv --ch-a "Channel 0" --ch-b "Channel 1" --marker --out docs/bench/9nov/h4_pairs_2tile.csv --plot docs/bench/9nov/h4_plot_2tile.png > docs/bench/9nov/h4_report_2tile.txt
   ```

3. Three-tile run: the same commands with `--channels 3` and `_3tile` in the file names. Analyse A to B, A to C and B to C by changing `--ch-a` and `--ch-b` to "Channel 0", "Channel 1" and "Channel 2".
4. Log the Pico and at least one tile during each run, so the `DE ticks= missed=` lines are kept.

**Pass (all three):**

- max absolute skew **at or below 5 ms**
- no pair at or above 16.67 ms
- `TICK IDENTITY CHECKED` printed

Record the median, the 99th percentile and the max of each pair in the notes.

**If max is over 5 ms:** keep the evidence. Check that every tile was JOINed and synced before the capture. Rerun **once**, and write down what you changed. Do not change code.

### H5. Is the IMU interrupt pin wired? (15 min, no firmware)

**Why:** tap detection uses the QMI8658 interrupt. If the Waveshare ESP32-S3-Matrix does not route the sensor's INT1 or INT2 pin to an ESP32 GPIO, tap detection has to poll instead, and the firmware design changes. This must be known before anyone relies on the tap code.

**Steps:**

1. Open the Waveshare ESP32-S3-Matrix schematic. Find the QMI8658 and follow its INT1 and INT2 pins. The sensor's I2C bus is SDA GPIO11 and SCL GPIO12, so the interrupt pins sit near those.
2. If the schematic shows a route, confirm it with a multimeter continuity check on one board. The board must be unpowered.

**Result to record:** "INT1 to GPIO<n>", "INT2 to GPIO<n>", or "not routed". Add a screenshot of the schematic section. There is no pass or fail here. The answer is the result.

**Also check:** the firmware default is INT1 on GPIO10 (`OR_TAP_INT_LINE` 1, check: `panel_sync/main/orientation.c`). If the answer is INT2, the firmware setting needs changing, and that is for the developer.

### H6. Tap feasibility data (45 min)

**Status on 9 Nov:** unblocked. The logger is on `origin/tools/imu-tap-log` at `tools/imu_tap_log/`.

**Why:** this is the riskiest item. Cardboard and Blu Tack both soak up and pass on vibration, and only real taps on the real board show how much. The numbers set the tap threshold, and show whether the 50 ms highest-peak dedupe is enough or the GPIO button fallback is needed.

**Setup:** all 5 tiles on the cardboard board with Blu Tack, laid out as they will be at the demo.

**Get the logger.** Check out `origin/tools/imu-tap-log` in a separate worktree, so that `feature/whack-a-mole` stays checked out (check with the developer before you use `git worktree`). The README is `tools/imu_tap_log/README.md` on that branch.

**Build and flash the logger on each tile** (Windows, on the board's COM port). This replaces the panel firmware on that board, so reflash the panel firmware afterwards if the board must join the wall:

```
cd tools\imu_tap_log
idf.py set-target esp32s3
idf.py build
idf.py -p COM<n> flash
```

The first `idf.py build` is the real compile check, because the C file has not been built on the bench before. A compile error here is a finding. Collect the full output and hand it back (section 7).

**Log every tile, one file per tile, for the whole run:**

```
python pc\tools\serial_log.py --port COM<n> --out docs\bench\9nov\h6_tap_<tile>.log --test H6 --who <name> --tile <tile> --tag bench-9nov --minutes 8
```

Use the board number as `<tile>`. One log per tile. A log that holds two boots is refused, so start a new file after a reset.

**Steps (log every tile for each step):**

1. Keep the board still for about 5 seconds before the first tap. The alignment uses the first strong peak, so the first action must be the first tap.
2. 20 centre taps on each tile, about 1 second apart.
3. 20 edge taps on each tile, about 1 second apart.
4. 10 knocks on the board edge, away from all tiles.
5. Press each tile back into its Blu Tack 5 times.
6. 30 seconds with no one touching anything. Write no rows for this stretch. It is the quiet baseline.

**Write the taps CSV as you go**, one row per action, with the local wall time of the action, the tile and the kind. Save it as `docs/bench/9nov/h6_taps.csv`:

```
wall,tile,kind
10:14:02.000,3,c
10:14:03.120,3,c
10:14:21.500,board,k
10:15:40.000,5,b
```

- `tile` is the board number, exactly as in the log's `board=` field. For a tile with no NVS number, use the MAC string as it appears in `board=`.
- `tile` is `board` for knocks, and only for knocks.
- Kinds: `c` centre tap, `e` edge tap, `k` knock, `b` Blu Tack press.
- Lines starting with `#` are comments.

**Analyse the logs:**

```
python tools\imu_tap_log\marks.py --taps docs\bench\9nov\h6_taps.csv docs\bench\9nov\h6_tap_*.log
python tools\imu_tap_log\summarise.py --taps docs\bench\9nov\h6_taps.csv docs\bench\9nov\h6_tap_*.log
```

`marks.py` aligns each tile's clock. A residual over 500 ms means a mistyped time, or a first peak that was not the first tap. Fix the CSV or the log and run again.

**Check the sample source.** The logger's `H,` line shows `src=fifo` or `src=poll`. In the `S,` lines, a rate well under 1000 with `src=fifo` means the FIFO register sequence probably does not match this board. Stop, and ask the developer. The fix named in the README is to set `ITL_SOURCE_FIFO` to 0 in `main/imu_tap_log.c` and rebuild. That is a code change, so it is for the developer.

**Result to record**, in a table per tile:

- the lowest peak from a deliberate tap on that tile
- the highest peak that tile saw while a **neighbour** was tapped
- the highest peak from board knocks and Blu Tack presses
- the threshold suggested by `summarise.py`, and whether any threshold met its targets (the tool prints this itself)

There is no pass or fail here. The gap between the first number and the other two is the result. The suggested threshold is a starting point, not a decision.

### H7. Rotation check on one tile (20 min)

**Why:** rotation is applied on the tile from gravity. The filter needs about 520 ms of stability (26 samples at 20 ms each, `ORIENT_FILTER_STABLE_SAMPLES` in `panel_sync/main/orient_filter.h`, with the orientation sampled every second tick of a 10 ms period, check: `panel_sync/main/orientation.c`). The 90 and 270 degree remap in `display_frame.c` must be checked on a real tile at each angle.

**Steps:**

1. Make an asymmetric 8x8 test GIF, such as an arrow or the letter F, where any rotation or mirror is obvious.
2. Play it on one tile with layout `1x1` and map `1,0,0,0,0,0,0,0,0`:

   ```
   config <test.gif> 1x1 1,0,0,0,0,0,0,0,0 pause 8
   build
   upload
   start
   ```

3. Hold the tile at 0, 90, 180 and 270 degrees in turn. Keep it still for at least 2 seconds at each angle, then photograph it with the tile's edges in frame.
4. Log the tile's serial for the whole card. Note any `ORIENT changed` lines that appear as you turn it.

**Pass:** the image is upright and not mirrored at all 4 angles. If one angle is wrong, record which one and how it is wrong (rotated by how much, or mirrored). Do not change the code.

**Evidence:** `docs/bench/9nov/h7_rot_<angle>.jpg` and `docs/bench/9nov/h7_rotation.log`.

---

## 4. Part B: feature branch (`feature/whack-a-mole`)

Start only after Part A has passed H2 at least. Check out the feature branch, and record the hash. The feature firmware has never been compiled on the bench, so F0 comes first.

**Dashboard shell commands** (feature branch; check: `pc/dashboard.py` for the exact help text):

| Command | Use |
|---|---|
| `config GIF LAYOUT MAP pause\|continue BRIGHTNESS` | Choose a GIF, layout, node map and pause policy. Example: `config demo.gif 2x2 1,2,3,4,0,0,0,0,0 pause 8` |
| `brightness N` | 1 to 8 only. Changing it after assets exist means rebuilding and re-uploading. |
| `grid ROWS COLS` | Each 1 to 9, and rows x cols at most 9. |
| `place SLOT` | Then tap the physical tile for that slot within 30 seconds. |
| `build` | Build every tile asset and send the session configuration. |
| `upload` | Upload every tile asset not yet acknowledged. Reports the tiles that failed. |
| `publish` | Rebuild, send the session configuration and upload what is pending. |
| `status` | The Pico's status, its state and score, and each tile's fault flags. |
| `game start` or `game abort` | Game mode, and the wall READY, needed to start. |
| `mode video` or `mode game` | Only while stopped. |
| `start`, `pause`, `resume`, `stop`, `quit` | Playback control. |

The node map always has 9 entries, read row by row over rows x cols. A 0 means blank. Bit n-1 in each bitmap is board number n.

### F0. Build all three images (expect possible compile errors)

**Why:** the feature firmware has not been compiled on the bench. Compile errors are likely the first time, in `tile_net.c`, `display_engine.c`, `game_tile.c` and `coordinator_loop.c`.

**Steps:**

1. Set up the build environment (WSL or Linux): ESP-IDF v5.3 exported, and `PICO_SDK_PATH` set to the Pico SDK 2.3.1 folder (check: `README.md` Prerequisites).
2. From the repo root, build the network-A image set. Use the exact router SSID (SSID is case sensitive) and password:

   ```
   SSID='<router SSID, exact case>' PASS='<router password>' ./tools/build_bench_images.sh A out/bench-9nov
   ```

   The script writes both `wifi_secrets.h` files from the example, builds the tile images and Pico images, and copies them into flashable folders. Network A also builds the stress and suppression images and the pico_tap image. Check the output folder is outside git, or git-ignored (check: `.gitignore`), before you use a folder inside the repo.
3. If the script stops early, the full log is `out/bench-9nov/build_A.log`. Collect the full log, and hand it back (section 7). Do not fix the code.
4. Run the host tests (check: `ledwall_sync/test/Makefile` and `panel_sync/test/Makefile` for the run command). Record each result.

**Pass:** every expected folder and `.uf2` is present, and the log has no errors. Record the compiler warning count that the script prints. A warning is not a failure, but record it.

**Flashing a tile from its folder** (run inside the tile folder, Windows, on the board's COM port):

```
python -m esptool --chip esp32s3 -p COM<n> -b 460800 write_flash @flash_args
```

Never run `erase_flash`.

**Evidence:** `docs/bench/9nov/f0_build_log.txt` (the copied log), and the warning count.

### F1. WiFi rejoin (restart the router mid-run)

**Why:** the coordinator must recover from a lost router without a power cycle. On master it did not. On the feature branch it retries the join on a backoff of 1, 2, 4, 8 and then 10 seconds, and it rejoins after a link loss.

**Steps:**

1. Power the coordinator Pico first and wait for state 2. Power the tiles, and wait until every tile is online.
2. Run a short playback on the wall, with the Pico log running: `config`, `build`, `upload`, `status`, `start`.
3. Restart the router during the run: power it off, wait about 30 seconds, and power it on.
4. Watch the Pico log. Note the `CL_EVENT` lines, the `COORDINATOR FAULT: router join not done in ... ms; retry in ... ms` lines, and the `Joining <router>` lines.
5. Wait until the Pico reports joined again. Do not power-cycle the Pico.

**Pass:**

- The Pico rejoins the router by itself, with no power cycle.
- The retry gaps follow the backoff: 1000, 2000, 4000, 8000, then 10000 ms.
- A joined coordinator leaves FAULT and starts again from state 2 (`WAITING_FOR_SESSION`), as the coordinator header states. Record that the wall stopped. Playback restarts only when the tester sends the session again (check: developer's expected flow before you judge the restart).

**Record:** the time from router-up to the Pico's joined line, and each retry gap seen.

**Evidence:** `docs/bench/9nov/f1_rejoin.log`.

### F2. Upload resume and QUEUE_OVERFLOW recovery

**Why:** on master an upload always started at node 1 and stopped at the first failure. A tile that had the asset would then fail and block the others. The feature branch skips tiles that already acknowledged the asset, and a failed tile does not stop the others.

**Steps:**

1. Set up the wall with all tiles online, as in F1 step 1.
2. Run `config`, `build` and `upload`. Expect every tile to acknowledge.
3. Test the resume. Unplug one tile's USB (for example tile 3) at the start of the next `upload`. Expect that tile to be reported as failed, while the others finish.
4. Plug the tile back in, and run `upload` again.
5. Test QUEUE_OVERFLOW. Run `upload` several times in a row without power cycles, and watch each tile for the `QUEUE_OVERFLOW` fault (fault code 2 in `pc/telemetry.py`). Check the `status` output after each run.

**Pass:**

- In step 4, only the failed tile is sent again. The tiles that already acknowledged are skipped.
- No tile shows `QUEUE_OVERFLOW` during the repeated runs.

**If a tile shows `QUEUE_OVERFLOW`:** record how many reruns it took, and whether the tile recovers without a power cycle. Mark the card PARTIAL with that finding, not an automatic FAIL. Tell the developer.

**Evidence:** `docs/bench/9nov/f2_upload.log`, and the `status` output after each step.

### F3. Website

**Why:** the website lets someone change the grid, place tiles, switch the slots on and off, change mode and run the game, from a browser on the router network.

**Start the website** on the laptop, on the router network. Do not run `dashboard.py` at the same time (check: `pc/web.py`):

```
python3 pc/web.py --pico <reserved IP> [--session <n>] [--port 8080]
```

Open `http://<laptop-ip>:8080` from another device on the router network, such as a phone on the router WiFi. Find the laptop's IP with `ipconfig` on Windows (check: the value on the router's device list if this differs).

The page polls the dashboard about every 250 ms. There are no WebSockets.

**Steps, one at a time:**

1. Change the grid size, for example to 1 row by 5 columns. Check the Pico's `status` shows the new shape after the rebuild and re-upload.
2. Try an impossible grid, such as 4 rows by 4 columns (more than 9 slots). Expect a refusal.
3. Tap-to-place: arm a slot on the page, then tap the physical tile that should fill it, within 30 seconds. Check the slot now shows that tile's board number.
4. Turn one slot off, then on again. Check the node map changes and the tile drops from and returns to the map.
5. Change mode to game while the wall is playing. Expect a refusal (HTTP 409). Stop the wall, then change the mode. Expect it to work.
6. Start a game from the page, and check the live score updates on the page.
7. Abort a game from the page.

**Pass:** each step does what it says, the page reflects the Pico's state, and a refused action gives the refusal in the page. Status codes from the code: 409 when the wall is playing or not ready, 500 when the Pico does not acknowledge, 400 for bad input.

**Evidence:** `docs/bench/9nov/f3_web.log` (the server output), and one screenshot per step.

### F4. Rotation on one tile (0, 90, 180, 270)

**Why:** this repeats H7 on the feature firmware. The feature branch changes the layout to rows and columns, so the test must run on the new build.

**Steps:** use the same steps as H7, with the feature firmware:

```
grid 1 1
config <test.gif> 1x1 1,0,0,0,0,0,0,0,0 pause 8
build
upload
start
```

Hold the tile at each angle for at least 2 seconds, and photograph it.

**Pass:** the same as H7. The image is upright and not mirrored at all 4 angles.

**Evidence:** `docs/bench/9nov/f4_rot_<angle>.jpg` and `docs/bench/9nov/f4_rotation.log`.

### F5. Video on 1x5, 2x2 and 5 tiles in a 3x3 grid

**Why:** video must keep sync on any layout. The later stage is 3x3 and 1x9, and that comes in F8.

**Layouts and commands** (each is a separate run). The GIF is centre-cropped to the layout's shape by the asset builder (check: `pc/asset_builder.py`):

```
grid 1 5
config <gif> 1x5 1,2,3,4,5,0,0,0,0 pause 8
build
upload
start
```

```
grid 2 2
config <gif> 2x2 1,2,3,4,0,0,0,0,0 pause 8
build
upload
start
```

```
grid 3 3
config <gif> 3x3 1,2,3,4,5,0,0,0,0 pause 8
build
upload
start
```

**Sync skew:** for each layout, run the H4 capture with pico_tap on 3 or 4 tiles at a time. Use the same wiring rules.

**Pass, for each layout:**

- all tiles play the GIF, and no tile reboots
- max absolute skew **at or below 5 ms**, and no pair at or above 16.67 ms
- `TICK IDENTITY CHECKED` printed

**Note the power source** of each tile, from the rules in section 0.

**Evidence:** `docs/bench/9nov/f5_<layout>_skew.csv`, `..._report.txt`, and one video per layout.

### F6. Game: start, abort, scoring and the wall

**Why:** this is the first full test of the game. Its rules come from `ledwall_sync/game_rules.h`.

**Steps:**

1. Set the wall to a layout with all its tiles online. Run `mode game` (only while stopped). Wait for the wall to show READY.
2. Run `game start`. The round lasts 30 seconds.
3. During the round, watch the lit tile. Tap it. Then tap a wrong active tile. Then let a mole time out without tapping. Then tap while no mole is showing.
4. Let the round end. Check the final score drawn across the wall, and the score in `status`.
5. Start a second round and run `game abort` halfway through. Check that the events stop and taps are ignored.
6. Check the website shows the live score, and the last score after the round ends (see F3).
7. Check `TN_STATS` in each tile log, for `tap_unsynced=0`.

**What you should see** (the colours are from the re-plan, check: `panel_sync/main/game_tile.c` for the exact colours):

| Event | Expected on the tile | Expected score change |
|---|---|---|
| Mole shows | A solid fill in one colour (amber) on the mole tile | none |
| Tap on the mole tile while it is up | Green flash | +1 |
| Tap on another active tile | Red flash on the tapped tile | -1 |
| Tap while no mole is showing | Red flash on the tapped tile | -1 |
| Mole times out, no tap | Mole goes off | 0 |
| Round ends | Final score drawn across the wall | none |

Check that the mole never comes up on the same tile twice in a row, unless only one tile is active.

**Pass (all of these):**

- every row in the table is seen at least once
- the score on the wall, in `status` and on the website match each other at the end
- abort stops the round at once
- `tap_unsynced` stays 0 on every tile log

**Evidence:** `docs/bench/9nov/f6_game.log`, the Pico log, the tile logs, and one photo of the final score.

### F7. Measurements against the targets

**Why:** this is the evidence for the report. Each target needs its own method.

**Sync skew (under 5 ms):** use the H4 method on the layout in use. Record the median, the 99th percentile and the max. Pass: max at or below 5 ms.

**Tap to hit flash (under 100 ms):** **BLOCKED on the firmware.** The re-plan says each event gets its own pulse width on GPIO4, one for the tap interrupt and one for the hit flash. The code I checked does not do this yet. GPIO4 currently carries only tick pulses (1 ms, and 4 ms every 64th tick, check: `panel_sync/main/display_engine.c`). Do not run a latency measurement until the developer confirms the event pulses exist. Record F7 latency as BLOCKED with that reason.

**Zero double-counted taps (50 deliberate hits):**

1. Tap a mole 50 times across a set of rounds. Count each deliberate hit.
2. From the Pico log (`CL_EVENT` lines) and the scores, count how many times each hit scored.
3. Pass: each deliberate hit scores once, so the number of +1 hits matches the number of hits you made, and no hit scores twice.

Check whether the dedupe counters are printed (check: `ledwall_sync/coordinator_loop.c`). If they are not printed, record the counts from the log lines you can see.

**False triggers on neighbour tiles:** during a round, tap a tile and log the neighbours' `TAP result=` lines (from `panel_sync/main/orientation.c`). A neighbour that reports a tap while only another tile was tapped is a false trigger. Count them. Compare with the threshold from H6.

**Evidence:** `docs/bench/9nov/f7_measurements.md`, with the number, the method and the log file for each target.

### F8. Scale to 9 tiles

**Why:** the demo has 9 tiles. This card runs when the rest of the boards arrive.

**Steps:**

1. Give each new board a unique board number from 1 to 9, with `set_board_number.sh`. Check each number is unique (a duplicate gives `IGNORED ... already`).
2. Flash each new board with the feature tile image, and check it joins.
3. Check `status` shows all 9 online. Expect `online=0x1ff`, meaning bits 0 to 8.
4. Test 3x3 and 1x9 in video and game mode. Use the F5 method for video, and F6 for the game.
5. Run the sync capture (pico_tap on 3 or 4 tiles at a time) on each shape.

**Pass:** all 9 tiles online, each shape plays and sync holds (max skew at or below 5 ms), and the game scores correctly on 9 tiles.

**Evidence:** `docs/bench/9nov/f8_nine_tile.log` and the captures.

---

## 5. Results table

Copy this table into `docs/bench/9nov/results.md`, with one row per card. Status is one of PASS, FAIL, PARTIAL, NOT RUN or BLOCKED. Use one line for each deciding number.

| Card | Status | Deciding number or line | Evidence path | Git hash | Time |
|---|---|---|---|---|---|
| H1 router and fixed IP | PASS (9 Nov) | Same IP on 3 boots | `docs/bench/6nov/h1_pico_ip.log` | master-(check) | (check) |
| H2 five tiles on master | | `online=` value, LEAVE/JOIN count | | | |
| H3 pico_tap calibration | | max skew, TICK IDENTITY, dropped | | | |
| H4 baseline skew, 2 tile | | max skew, pairs at 16.67 ms or more | | | |
| H4 baseline skew, 3 tile | | max skew | | | |
| H5 IMU interrupt pin | | INT1 or INT2 to GPIO n, or not routed | | n/a | |
| H6 tap feasibility | | Lowest tap peak, highest neighbour peak | | | |
| H7 rotation, one tile | | Angles upright and not mirrored | | | |
| F0 build all images | | Errors, warning count | | | |
| F1 WiFi rejoin | | Rejoined without power cycle, retry gaps | | | |
| F2 upload resume | | Only failed tile resent, QUEUE_OVERFLOW count | | | |
| F3 website | | Steps passed out of 7 | | | |
| F4 rotation, feature | | Angles upright and not mirrored | | | |
| F5 video 1x5, 2x2, 5-in-3x3 | | Max skew per layout | | | |
| F6 game | | Rows of the event table seen, tap_unsynced | | | |
| F7 measurements | | Skew, latency (BLOCKED), double counts | | | |
| F8 nine tiles | | online bitmap, max skew | | | |

---

## 6. Triage: symptom, likely cause, action

Work through this table before you decide a cause. Confirm the cause with the tester before you change anything.

| Symptom | Likely cause | Action |
|---|---|---|
| Pico shows state 11 (FAULT) | The router join failed, or the link dropped. The Pico is in FAULT. | On master: power-cycle the Pico after the router change. On the feature branch: wait for the backoff retries (1, 2, 4, 8, 10 s). If it stays in FAULT, check the router, the SSID case and the password, then power-cycle and log what you did. |
| Dashboard says "no TELEMETRY yet" | The laptop is on a different WiFi, or `--pico` has the wrong IP, or the tiles are not reporting. | Check the laptop is on the router's network. Check the `--pico` IP matches the Pico's reserved IP. Check the tile logs show `TN_STATS` lines. |
| `0 of N chunks ACKed` on upload | On master, an upload starts at node 1 and stops at the first failure. Tile 1 may already have the asset, or it is in `QUEUE_OVERFLOW`. | On master: power-cycle the tiles, then restart the dashboard with a fresh session and run the upload once. On the feature branch: see F2. |
| `QUEUE_OVERFLOW` on a tile | Too many uploads were sent to one tile. A tile in this state ignores uploads on master. | On master: power-cycle that tile. On the feature branch: record the rerun count and whether it recovers (F2). |
| Dashboard shows MONITORING PLAYBACK, but the Pico shows READY and the tiles are idle | The dashboard and the Pico disagree. The Pico ignores a repeated session ID and any command with a non-newer sequence number. | Trust the Pico's `status` output. Do not restart with the same session. Restart the dashboard with a fresh `--session` (1 to 255, different from the last run). |
| Pico rejects the join, SSID not found, or the tiles never join | SSID is case sensitive. `MATRIX` and `Matrix` are different. The firmware and the router must match exactly. | Fix the SSID in both `wifi_secrets.h` files, then rebuild the firmware. Reflashing the old build is not enough. |
| Pico log `IGNORED ... board number` | That board has no board number in NVS. | Run `set_board_number.sh` for that board, with a unique number from 1 to 9. |
| Pico log `IGNORED ... already` | Two boards share a number. | Give one board a new unique number. |
| Tile sits at `boot:0x0 (DOWNLOAD...)` | The tile rebooted into download mode after a software reset. This happens on ESP-4. | Unplug and replug the tile's USB. Do not press RESET. |
| Flashing hangs at "Connecting..." | The board is not in download mode. | Hold BOOT, tap RESET, release BOOT, and retry. |
| `/dev/ttyACM0: No such file` | The board is not attached to WSL. | Flash and log from Windows instead, on the board's COM port. Do not attach boards to WSL for logging. |
| Repeated LEAVE or JOIN on the Pico | Check-ins are being lost: signal, channel or DTIM. | Check DTIM is 1, AP isolation is off, and the tile is close to the router. Keep the channel as it is. |
| Fresh boot in a tile log | The tile rebooted. This is a power or brightness problem. | Power the tiles from the demo charger, one port each. Record the power source. Check brightness is at the 8/255 cap. |
| `MISSED_DEADLINE` grows during playback | Frames are late. This is a watch item on H2. | Record the growth rate. Tell the developer if it grows quickly. |
| Tile reports a tap while a neighbour was tapped | A false trigger. The threshold is too low, or the knock was strong. | Compare with the H6 threshold table. Record the count. The developer tunes the threshold. |
| `tap_unsynced` above 0 in `TN_STATS` | A tap was dropped because the tile had no clock yet. The counter is part of the tile's stats (check: `panel_sync/main/tile_net.c`). | Wait until the tile is synced, then tap again. If it happens during a synced round, that is a finding for the developer. |
| Tile shows nothing after upload | The upload was not acknowledged, or the wall is not started. | Run `status`. Check the tile's ACK count. Run `upload` again, then `start`. |
| pico_tap calibration fails | The tool is wrong, or the wiring is wrong. | Do not use pico_tap for evidence. Check the wiring (one tile to both GP2 and GP3). Tell the developer. |
| A build fails with compile errors | The feature code does not compile yet. | Stop the card. Collect the full log, and hand it back (section 7). Do not fix the code. |
| Game score differs between the wall, `status` and the website | The score did not reach every view. | Record the three numbers and when each was read. Tell the developer. |

---

## 7. What to hand back to the developer

At the end of the session, give the developer these items. Do not leave any out. Send them as files in `docs/bench/9nov/`, and say which branch and hash each run used.

1. The completed results table (section 5), with every row filled in as PASS, FAIL, PARTIAL, NOT RUN or BLOCKED.
2. For every FAIL or PARTIAL: the one-line reason, the log file name, and the exact lines that decide it.
3. For every compile error: the full build log (for example `out/bench-9nov/build_A.log`), and the compiler warning count.
4. The git hashes used for `master` and for `feature/whack-a-mole`, and the branch that was checked out for each run.
5. The answer to H5 (INT1, INT2 or not routed), with the schematic screenshot.
6. The H6 threshold table and the summary output of `summarise.py`.
7. For each tile: the board number, the WiFi MAC, the power source, and any reboots or power cycles.
8. Any run that was repeated, and what was changed before the rerun.
9. Photos: the GPIO4 wiring, the router settings, the DHCP reservation, the rotation angles.
10. Open questions the tester found. These include anything marked "(check: ...)" that you could not confirm, the GPIO4 latency pulses (F7), and any mismatch between this guide and the bench.

Also note the tester's name and the time each card started and ended.
