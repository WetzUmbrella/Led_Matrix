# SE15 hardware testing: whack-a-mole re-plan, base checks

These are the hardware checks that have to pass before the game work can be trusted on real tiles. They cover step 1 (base) and the data-collection half of step 2 (tap detection) of the re-plan. Each card says what to run, what counts as a pass, and where the evidence goes.

**Everything here runs on `master`.** Do not flash anything built from `feature/whack-a-mole`. That branch changes the SESSION_CONFIG and STATUS message sizes, so its tiles and Pico cannot talk to master's, and the failure looks like a broken network rather than a version mismatch.

## 0. Before you start

### Setup (once per laptop)

- Prerequisites, WSL, ESP-IDF v5.3, Pico SDK 2.3.1 and usbipd: [README.md, Prerequisites](../../README.md#prerequisites-one-time-per-machine).
- Clone the repo inside the WSL filesystem, not under `/mnt/c`, and stay on `master`:

  ```
  git clone https://github.com/WetzUmbrella/Led_Matrix.git ~/Led_Matrix && cd ~/Led_Matrix && git checkout master
  ```

- WiFi credentials (git-ignored, never commit them):

  ```
  cp ledwall_sync/wifi_secrets.example.h ledwall_sync/wifi_secrets.h
  cp panel_sync/main/wifi_secrets.example.h panel_sync/main/wifi_secrets.h
  ```

  Edit both files to set `ROUTER_SSID` and `ROUTER_PASS` for the demo router.
- Install the commit hook: `cp docs/tools/commit-msg .git/hooks/commit-msg`.

### Rules that apply to every card

- **Never run `idf.py erase-flash` or esptool `erase_flash`.** It wipes the board number stored in NVS. If it happens, rerun `panel_sync/set_board_number.sh <n>` with that board's number.
- **Only one coordinator Pico on the router at a time.** A second beaconing Pico corrupts every tile's clock (see [docs/week10/skew_2tile/bench_log.md](../week10/skew_2tile/bench_log.md), problem 1). The pico_tap Pico is fine, because it doesn't join WiFi.
- **Power the coordinator Pico first,** then the tiles.
- **Some tiles reboot into download mode after a software reset** (ESP-4 does). If a tile sits at `boot:0x0 (DOWNLOAD...)`, unplug and replug its USB instead of pressing RESET.
- **Between boards, connect only signal and GND wires.** Never join 3V3 or 5V pins.
- **Logging:** start the logger before step 1 of a card and stop it after the last step (Ctrl+C). Use the same tool and header as the 5 Nov bench:

  ```
  python pc/tools/serial_log.py --port COM<n> --out <evidence path> --test <card> --who <name> --tile <board>/<MAC> --image master-<short hash> --net A --pico A
  ```

- Decide pass or fail from the log lines each card names, not from how it looked. A failure is still evidence: keep the log and mark it FAIL with a one-line reason.

### Flash commands (used by several cards)

- **Tile** (WSL, board attached with `usbipd attach --wsl --busid <BUSID>`):

  ```
  get_idf
  cd panel_sync
  idf.py set-target esp32s3            # first time only
  ./set_board_number.sh <n>            # first time per board only, 1 to 9, unique
  idf.py -p /dev/ttyACM0 flash monitor
  ```

  Expect `I am ESP-<n>`, then `joined <router>`, then `Pico found at ...`. Exit the monitor with `Ctrl+]`.
- **Coordinator Pico:**

  ```
  mkdir -p ledwall_sync/build && cd ledwall_sync/build && cmake .. && make -j4
  ```

  Hold BOOTSEL, plug it in, and copy `ledwall_sync/build/ledwall_sync.uf2` onto the RPI-RP2 drive.
- Or build every image at once into flashable folders with [tools/build_bench_images.sh](../../tools/build_bench_images.sh). Flash a tile from its folder with `python -m esptool --chip esp32s3 -p COM<n> -b 460800 write_flash @flash_args`.

### Evidence

Put everything under `docs/bench/6nov/`, with your notes in `docs/bench/6nov/<name>.md`, using the results-table format from [docs/bench/5nov/TEMPLATE.md](5nov/TEMPLATE.md). Commit to a branch `bench/<name>-6nov` and push it. Don't push to master.

---

## H1. Router and the Pico's fixed IP (15 min)

**Why:** the re-plan uses a dedicated router instead of the Pico's soft AP, and gives the Pico a DHCP-reserved IP so the PC and website always find it.

**Steps:**

1. On the router's admin page, set the 2.4 GHz band exactly as in [README.md, Router setup](../../README.md#router-setup-one-time):
   - WPA2-Personal (AES)
   - AP isolation **off**
   - DTIM interval **1**
   - fixed channel (1, 6 or 11), 20 MHz width
2. Disconnect the router's WAN/internet cable. The demo runs with no uplink.
3. Flash and power the coordinator Pico. Note the IP and MAC from its log line `Joined <router>, ip=...`.
4. On the router, add a DHCP reservation for that MAC.
5. Power-cycle the Pico 3 times and log each boot.

**Pass:** the Pico shows the same reserved IP on all 3 boots.

**Evidence:** `docs/bench/6nov/h1_pico_ip.log`. In the notes, record the router settings and a screenshot of the DHCP reservation page.

## H2. Five tiles on master, video playback (30 min)

**Why:** this is the baseline that shows boards, power and network work before any new code is involved. Every later card assumes it passes.

**Steps:**

1. Power all 5 tiles and the coordinator Pico from the **one multi-port USB charger** that will be used at the demo. Start the Pico first and log its serial.
2. Each tile must already have a unique board number (1 to 5). If the Pico logs `IGNORED ... board number` or `IGNORED ... already`, fix it with `set_board_number.sh`.
3. Start the dashboard on the PC, with WiFi power save off:

   ```
   python3 pc/dashboard.py --pico <reserved IP from H1>
   ```

   Then in the dashboard shell (any small GIF works; master accepts 1x1, 2x2 or 3x3 layouts only):

   ```
   config <your.gif> 3x3 1,2,3,4,5,0,0,0,0 pause 8
   build
   upload
   status
   start
   ```

4. Leave it playing for **10 minutes**.

**Pass:**

- The Pico's `CL_STATS` line shows `online=0x01f`, meaning all 5 tiles are online.
- No `LEAVE`/`JOIN` pairs on the Pico after the start.
- No fresh-boot lines in any tile log. A reboot means a power or brightness problem.
- All 5 tiles play the GIF the whole time.

**Evidence:** `docs/bench/6nov/h2_pico_5tile.log`, plus a 10 s video of the wall.

**If it fails:** check the [README troubleshooting table](../../README.md#troubleshooting) and [section 9 of the 5 Nov plan](2026-11-05_plan.md#9-when-something-goes-wrong). If tiles reboot only when all 5 are on the charger, try them on separate USB power to see whether the charger is the cause, and write down what you found.

## H3. pico_tap: flash and calibrate (15 min)

**Why:** the spare Pico W is our logic analyser. [tools/pico_tap/README.md](../../tools/pico_tap/README.md) says it **has not run on hardware yet**, so nothing it measures counts until it passes calibration.

**Steps:** follow [tools/pico_tap/README.md](../../tools/pico_tap/README.md) exactly. In short:

1. Build it (`PICO_SDK_PATH` set):

   ```
   cd tools/pico_tap && mkdir -p build && cd build && cmake .. && make
   ```

   Hold BOOTSEL and copy the `.uf2` from `tools/pico_tap/build/` onto RPI-RP2. Power it from the laptop that records.
2. Wire **one** tile's GPIO4 to **both** GP2 and GP3, and join that tile's GND to the pico_tap GND. The tile must be running and joined, as in H2.
3. Record 60 s:

   ```
   python pc/tools/pico_tap_capture.py --port COM<n> --channels 2 --seconds 60 --out docs/bench/6nov/calib.csv
   python pc/tools/skew_analyse.py docs/bench/6nov/calib.csv --ch-a "Channel 0" --ch-b "Channel 1" --marker --out docs/bench/6nov/calib_pairs.csv > docs/bench/6nov/h3_calibration_report.txt
   ```

**Pass (all three):**

- max skew **under 0.05 ms**
- `TICK IDENTITY CHECKED` printed
- `dropped=0` in the capture summary

**If it fails:** the tool is wrong, not the tile. Don't use pico_tap for evidence. Tell Ahsanul. The fallback is the gusmanb LogicAnalyzer firmware on the same Pico.

**Also record (open item B1):** which header pin GPIO4 is on, and a photo of the wiring. [docs/HANDOVER.md](../HANDOVER.md) still lists GPIO4 on the header as unconfirmed.

## H4. Baseline sync skew on master (40 min)

**Why:** the evaluation needs a before-and-after against the under-5 ms target. This card is the "before".

**Wiring** ([pico_tap README wiring table](../../tools/pico_tap/README.md#wiring)):

| pico_tap pin | Channel | Connect to |
|---|---|---|
| GP2 | Channel 0 | tile A GPIO4 |
| GP3 | Channel 1 | tile B GPIO4 |
| GP4 | Channel 2 | tile C GPIO4 |
| GP5 | Channel 3 | tile D GPIO4 (optional) |
| GND | | **a separate ground wire from each probed tile** |

Probe 3 or 4 tiles at a time, never more.

**Steps:**

1. All tiles playing as in H2, and JOINed, before the capture starts.
2. 2-tile run, 600 s:

   ```
   python pc/tools/pico_tap_capture.py --port COM<n> --channels 2 --seconds 600 --out docs/bench/6nov/h4_skew_2tile.csv
   python pc/tools/skew_analyse.py docs/bench/6nov/h4_skew_2tile.csv --ch-a "Channel 0" --ch-b "Channel 1" --marker --out docs/bench/6nov/h4_pairs_2tile.csv --plot docs/bench/6nov/h4_plot_2tile.png > docs/bench/6nov/h4_report_2tile.txt
   ```

3. 3-tile run: the same commands with `--channels 3` and `_3tile` in the file names. Analyse A–B, A–C and B–C by changing `--ch-a`/`--ch-b`.
4. Log the Pico and at least one tile during each run (the `DE ticks= missed=` lines).

**Pass:**

- max absolute skew **≤ 5 ms**
- no pair ≥ 16.67 ms
- `TICK IDENTITY CHECKED` printed

Record the median, p99 and max of each pair in your notes.

**If max > 5 ms:** keep the evidence. Check every tile was JOINed and synced before the capture, then rerun **once**, and write down what you changed.

## H5. Is the IMU interrupt pin wired? (15 min, no firmware)

**Why:** the re-plan detects a tap with a QMI8658 interrupt. If the Waveshare ESP32-S3-Matrix doesn't route the IMU's INT1 or INT2 pin to an ESP32 GPIO, tap detection has to poll instead, and the firmware design changes. This needs to be known before anyone writes tap code.

**Steps:**

1. Open the Waveshare ESP32-S3-Matrix schematic. Find the QMI8658 and follow INT1 and INT2. The IMU's I2C bus is SDA GPIO11 and SCL GPIO12 ([panel_sync/main/orientation.c](../../panel_sync/main/orientation.c)), so it sits near those.
2. If the schematic shows a route, confirm it with a multimeter continuity check on one board, with the board unpowered.

**Result to record:** "INT1 → GPIO<n>", "INT2 → GPIO<n>", or "not routed", plus a screenshot of the schematic section. There's no pass or fail here; the answer itself is the result.

## H6. Tap feasibility data (45 min)

**Status: blocked until a tap logger build exists.** Master's orientation code only reads X and Y acceleration at 50 Hz, which is too slow to see a tap. When the logger is in the repo, it will be at `tools/imu_tap_log/` with its own README. Run this card then.

**Why:** this is the riskiest item in the re-plan. Cardboard and Blu Tack both soak up and pass on vibration, and nothing but real taps on the real board shows how much. The numbers decide the tap threshold, and whether the 50 ms highest-peak dedupe is enough or the GPIO button fallback is needed.

**Setup:** all 5 tiles on the cardboard board with Blu Tack, laid out the way they'll be at the demo, with the logger running on every tile.

**Steps (log every tile for each step):**

1. 20 deliberate taps at the **centre** of each tile, about 1 s apart. Write down which tile you tapped and when.
2. 20 deliberate taps at the **edge** of each tile.
3. 10 knocks on the board edge, away from all tiles.
4. Press each tile back into its Blu Tack 5 times.
5. 30 s with no one touching anything (noise floor).

**Result to record**, in a table per tile:

- the lowest peak from a deliberate tap on that tile
- the highest peak that tile saw while a **neighbour** was tapped
- the highest peak from board knocks and Blu Tack presses

There's no pass or fail here; the gap between the first number and the other two is the result. Keep the raw logs at `docs/bench/6nov/h6_tap_<tile>.log`.

## H7. Rotation check on one tile (20 min)

**Why:** the re-plan applies rotation on the tile from gravity (orient_filter, about 300 ms of stability required). The 90° and 270° remap in [panel_sync/main/display_frame.c](../../panel_sync/main/display_frame.c) has to be checked on a real tile at each angle.

**Steps:**

1. Make an asymmetric 8×8 test GIF, such as an arrow or the letter F, where any rotation or mirror is obvious.
2. Play it on one tile with layout `1x1` and map `1,0,0,0,0,0,0,0,0`.
3. Hold the tile at 0°, 90°, 180° and 270° in turn. Keep it still for at least 2 s at each angle, then photograph it with the tile's edges in frame.
4. Log the tile's serial for the whole card.

**Pass:** the image is upright and not mirrored at all 4 angles. If one angle is wrong, record which one and how it's wrong (rotated by how much, or mirrored). Don't change the code; that's Jia Jun's subsystem.

**Evidence:** `docs/bench/6nov/h7_rot_<angle>.jpg` and `docs/bench/6nov/h7_rotation.log`.

---

## Results table

Copy this into `docs/bench/6nov/<name>.md` and fill one row per card:

| Card | Status (PASS / FAIL / PARTIAL / NOT RUN / BLOCKED) | Deciding line or number | Evidence path | master hash | Time |
|---|---|---|---|---|---|
| H1 router and fixed IP | | | | | |
| H2 5 tiles on master | | | | | |
| H3 pico_tap calibration | | | | | |
| H4 baseline skew, 2-tile | | | | | |
| H4 baseline skew, 3-tile | | | | | |
| H5 IMU interrupt pin | | | | | |
| H6 tap feasibility | BLOCKED | waiting for `tools/imu_tap_log/` | | | |
| H7 rotation, one tile | | | | | |

Order: H1 → H2 → H3 → H4. H5 and H7 can go at any point after H2.
