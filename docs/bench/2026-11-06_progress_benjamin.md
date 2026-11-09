# Bench notes, 6 Nov 2026: Benjamin

Progress so far on `docs/bench/2026-11-06_hardware_testing.md`. Everything ran on `master`. The Pico was built from `2832233`; the only difference to `e7a07d7` is a formatting change in tile code (`playback_control.c`), so the Pico image is functionally the same. Tile images were built after a `git pull`; the exact hash was not confirmed in the session, so run `git rev-parse --short HEAD` on the build laptop before quoting it.

## Results table

| Card | Status | Deciding line or number | Evidence path | master hash | Time |
|---|---|---|---|---|---|
| H1 router and fixed IP | PASS | Same IP 192.168.1.154 on 3 boots, `state=2` after each | `docs/bench/6nov/h1_pico_ip.log` | master-2832233 | 14:06 |
| H2 tiles on master | PARTIAL, run in progress | 4 of 5 tiles. `state=8` from 14:52:54, `online=0x00f`, `ready=0x00f`, `send_fail=0` on every line seen so far. 10 minute result not yet in | `docs/bench/6nov/h2_pico_5tile.log` | master-2832233 (Pico) | started 14:52:54 |
| H3 pico_tap calibration | NOT RUN | | | | |
| H4 baseline skew, 2-tile | NOT RUN | | | | |
| H4 baseline skew, 3-tile | NOT RUN | | | | |
| H5 IMU interrupt pin | NOT RUN | | | | |
| H6 tap feasibility | BLOCKED | waiting for `tools/imu_tap_log/` (still absent on master) | | | |
| H7 rotation, one tile | NOT RUN | | | | |

H2 is PARTIAL for three reasons: tile 5 was not powered (no spare port), the tiles were powered from several USB sources and not the demo charger, and the 10 minute result is still to be written.

## What we did

1. Set up the repo on the build laptop in WSL on `master`, wifi secrets copied and edited (git-ignored), commit hook installed, `docs/bench/6nov/` created.
2. Confirmed GPIO4 is broken out on the Waveshare ESP32-S3-Matrix header (right edge, 4th hole up from the bottom, between pins 3 and 5). The holes have no header pins fitted, so a wire needs a way to grip. This answers open item B1 for the location; the wiring photo is still to be taken in H3.
3. H1: router is an ASUS RT-AC1200G+. Professional tab set (AP isolation off, DTIM 1). WAN is not connected, which suits the offline demo. Built and flashed the coordinator Pico, found it as PICO-Main at 192.168.1.154 (MAC 2C:CF:67:B7:EB:C3), saved a DHCP reservation, power cycled it 3 times, same IP each time.
4. Flashed tiles 1 to 4 with board numbers 1 to 4 and confirmed beacons arrive (`BCN` lines, `TN_STATS dropped=0`). Tile 5's flash was not confirmed.
5. H2: ran the dashboard from Windows Python (laptop on the router network), built a 24x24 test GIF, mapped tiles 1 to 4 in a 3x3 layout, uploaded, started. See the Pico log for the 10 minute run.

Tile MACs recorded: tile 2 B8:F8:62:D8:09:F8, tile 3 B8:F8:62:D8:05:EC, tile 4 9C:13:9E:F1:2E:B0, tile 5 9C:13:9E:F1:2F:70. Tile 1's MAC was not noted.

## What we found

1. **Pico does not retry the router join.** It tries for 10 seconds, then sits in FAULT (`state=11`) until it is power cycled. It also fell into FAULT after the router restarted when the DHCP reservation was saved (`send_fail=1`, then stuck). After any router change, power cycle the Pico.
2. **SSID is case sensitive.** `MATRIX` versus `Matrix` cost us time; the firmware and the router must match exactly, and a firmware rebuild is needed after any change to `wifi_secrets.h`.
3. **The dashboard laptop must be on the router's network.** Our first runs failed with "no TELEMETRY yet" because the laptop was on a different WiFi. Whether the dashboard also works from WSL on the router's network was not tested.
4. **An upload cannot be resumed.** `upload` always starts at node 1 and stops at the first failure. If tile 1 already has the asset, retrying fails with `0 of 14 chunks ACKed` and never reaches the others. Repeated uploads put tile 1 into `QUEUE_OVERFLOW`, and a tile in that state ignores uploads until it is power cycled. New session numbers alone did not fix it.
5. **Dashboard and Pico can disagree.** The dashboard showed MONITORING PLAYBACK while the Pico still reported READY and tiles were idle (`display=1`). The Pico ignores a repeated session ID and any command with a non-newer sequence number, so restarting the dashboard with the same `--session` is risky.
6. **What worked:** power the Pico first, wait for `state=2` and `online=0x00f`, power cycle all tiles, restart the dashboard with a fresh `--session` number, run `config`, `build`, `upload` once, `status`, then `start`. The Pico log went 2, 3, 5, 7, 8.
7. **Faults after upload.** Tiles reported `MISSED_DEADLINE` (and one `QUEUE_OVERFLOW`) after the upload, before `start`. Check whether these grow during a run.
8. **Power.** Tile 4 dropped and rejoined once while powered from the second laptop. Keep that laptop awake and on its charger, or use a wall charger or power bank.

## Doc gaps to fix (for the doc owner)

1. The doc flashes tiles from WSL (`/dev/ttyACM*` after `usbipd attach`) but logs with `--port COM<n>`. A board attached to WSL disappears from Windows, so pick one side for logging.
2. H7 says orient_filter needs about 300 ms of stability. The code needs 26 samples at 20 ms, about 520 ms (`ORIENT_FILTER_STABLE_SAMPLES`, `OR_PERIOD_MS`). The 2 s hold in H7 still covers it.
3. H2 says "WiFi power save off" without saying how. On Windows it is the adapter's Power Management setting plus the power plan.
4. The router fixed channel step: setting it while the WiFi was changing made the network vanish for a while; the channel was left as is.

## What to look out for next time

1. Power the Pico first and keep it out of FAULT before any tile boots.
2. Start the Pico serial logger before `start` so the evidence covers the whole run.
3. Do not retype `config` or restart the dashboard during a run. If an upload fails, power cycle the tiles and use a new `--session`.
4. Record exactly which USB source powers each tile.
5. Use `ls -l /dev/serial/by-id/` to tell the Pico from the tiles when more than one port is attached.
6. Never commit `wifi_secrets.h` (it is git-ignored) and keep passwords out of these notes.

## Still to do

H2 final result (10 minutes, then `status` and stop the logger), a 5 tile rerun if a port frees up, and H3, H4, H5 and H7. H6 stays blocked until `tools/imu_tap_log/` exists. Order from the doc: H3, then H4; H5 and H7 any time after H2.
