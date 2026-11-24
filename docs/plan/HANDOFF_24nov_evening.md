# Session handoff, 24 Nov 2026 evening (NR-07 measured, drift fix on tiles 4 to 9)

Written by the agent on the Windows laptop for the agent that takes over (a teammate's) starting cold. **This file supersedes `HANDOFF_24nov.md` for "where things stand".** Bench setup and method: `HANDOFF_24nov.md` sections 2, 4 and 5.6, with the changes in section 2 below.

Read before acting, in this order:
1. This file.
2. `docs/bench/24nov/freeze_old_router.md`, from "Camera reader validation" to the end: the evening's bench record (reader check, NR-07 by camera, time-sync replay, the fix, the WiFi problem and its cause, NR-07 with the fix).
3. `docs/final/requirements_check_24nov.md`: the NR-07 row and sections 1 and 6.

## 1. Where things stand (24 Nov, 21:15)

| Item | State |
|---|---|
| Branch | `feature/whack-a-mole`. **Not pushed:** `937653d`, `c7d884e`, `cbe7763` and the commit that adds this file exist only on the laptop (origin at `89ced05`). The laptop's agent cannot push; the user runs `git push origin feature/whack-a-mole`. Pull before working. |
| NR-07 (video sync under 5 ms) | **Met** on tiles 4 to 9 with `cbe7763`, by 240 fps camera: 7-8 (twice), 8-9 and 5-6 have no real step at or over 5 ms, medians -0.38 to +0.31 ms. Before the fix 7-8 failed (up to 8.6 ms). Limits in the requirements row. |
| Cause and fix | The tiles' drift estimate (two 8-beacon minima only 0.8 s apart) read beacon delay as a clock rate, median about 1,500 ppm. **`cbe7763` limits it to +-50 ppm** (`TIMESYNC_DRIFT_MAX_PPB`, `timesync.c` `window_add()`), host test first (failed on the old code), full gate green. Replay of 19 min of logged beacons: skew p99 9.0 ms before, 0.4 ms after. |
| Tiles 4 to 9 | **`cbe7763`**, built on the laptop (19:26) with the corrected WiFi name. Online, playing `sync_3x3.gif`. |
| Tiles 1, 2, 3 | **Old firmware `6b1e446`**, unpowered: their power adapter is failing (user, not a tile fault). Website slots 0 to 2 off. They need `cbe7763` before the demo. |
| Pico | `f9d313c` (bench machine build), 192.168.1.154, laptop USB COM3. Watchdog restart at about 19:08 (`wd_boot=1`), recovered by itself. |
| Open finding | A whole-wall re-seed at about 19:06:50: three beacons over 500 ms late, every tile re-seeded on the same beacon and its display clock jumped 667 ms (a visible jump of the video on the whole wall, not a skew). Not addressed. |
| Gate (at `cbe7763`) | clang-format 18.1.8 clean on 104 files, barr_check 0 findings, host suites plain and ASan (panel_sync all groups incl. "24 Nov drift limit", proto 8 of 8, coordinator 43 of 43, pico_tap 4 of 4), pytest 509 passed 3 skipped (Windows), cross-lang 3 of 3, both firmwares build. |

### Boards on the bench

| Board | Firmware | Notes |
|---|---|---|
| Pico `…E66430A64B595138` | `f9d313c` | 192.168.1.154 |
| Tile 1 `9C:13:9E:F1:30:70` | `6b1e446` | Off (adapter) |
| Tile 2 `B8:F8:62:D8:09:F8` | `6b1e446` | Off (adapter) |
| Tile 3 `B8:F8:62:D8:05:EC` | `6b1e446` | Off (adapter) |
| Tile 4 `9C:13:9E:F1:2E:B0` | `cbe7763` | Flashed about 20:44 |
| Tile 5 `9C:13:9E:F1:2F:70` | `cbe7763` | Flashed about 20:44 |
| Tile 6 `9C:13:9E:F1:2F:E4` | `cbe7763` | Flashed about 20:44 |
| Tile 7 `9C:13:9E:F1:2C:70` | `cbe7763` | Re-flashed 19:27 |
| Tile 8 `9C:13:9E:F1:2B:D0` | `cbe7763` | Re-flashed 19:27 |
| Tile 9 `B8:F8:62:D8:0E:D8` | `cbe7763` | Re-flashed 19:27; now at 192.168.1.5 (was .219) |

## 2. Bench setup changes

- **WiFi name: the old router broadcasts `MATRIX` (capitals).** Any machine that builds firmware needs `ROUTER_SSID "MATRIX"` in `panel_sync/main/wifi_secrets.h` (tiles) and `ledwall_sync/wifi_secrets.h` (Pico). On the laptop both said `Matrix`, so the first `cbe7763` image could not join (19:08); the tile copy is fixed, the Pico copy still says `Matrix`. Check yours before building: a tile that prints only `ASSET_FLASH` once a minute and no `BCN` lines is not on the WiFi.
- Laptop (Windows): `.venv\Scripts\python.exe`; make, gcc, ESP-IDF and the Pico SDK in WSL (`wsl.exe -d Ubuntu-20.04 -- ...`); clang-format 18.1.8 at `C:\Users\ahsan\AppData\Local\Packages\PythonSoftwareFoundation.Python.3.11_qbz5n2kfra8p0\LocalCache\local-packages\Python311\Scripts\clang-format.exe`.
- Flashing tiles from Windows (in `panel_sync/build`): `..\..\.venv\Scripts\python.exe -m esptool --chip esp32s3 -p COMxx -b 460800 --before default-reset --after hard-reset write-flash --flash-mode dio --flash-size 4MB --flash-freq 80m 0x0 bootloader/bootloader.bin 0x8000 partition_table/partition-table.bin 0x10000 panel_sync.bin`. NVS (0x9000) is not touched. Match ports to tiles by USB serial number (= MAC, table above), never by port number. Linux: `docs/bench/12nov/tools/flash_tiles.sh`.
- Laptop network: Ethernet static `192.168.1.228/24`, **no gateway, no DNS** (otherwise Windows used the router for DNS and the internet failed); WiFi stays on SIT-WIFI.
- Website: `.venv\Scripts\python.exe pc\web.py --pico 192.168.1.154 --layout 3x3 --gif sync_3x3.gif` (create `pc/gifs` and regenerate the GIF with `docs/bench/24nov/tools/sync_gif.py 3 3 pc/gifs/sync_3x3.gif` on a fresh checkout). `POST /api/slot {slot, on}` re-uploads and stops playback: wait for READY, then `POST /api/video/play`.

## 3. Next, in order

1. **Push** the four commits (user).
2. **Tiles 1 to 3:** when their power is fixed, flash `cbe7763` (built with `MATRIX`), turn slots 0 to 2 back on one at a time (`POST /api/slot {"slot": N, "on": true}`, waiting for each re-upload), check READY with readiness 0x1FF, and film one pair from the top row (for example 1-2 and 2-3) the same way to cover all nine tiles.
3. Optional NR-07 strengthening: 5-6 again with the card and fingers clear of tile 6 (only 3 usable pieces), 4-5 on the new firmware, and longer recordings if the phone allows.
4. **Whole-wall re-seed jump** (667 ms at about 19:06:50): decide with the user whether to act (for example re-seed only after a longer run of off samples, or slew instead of step when the new offset is within a second).
5. Fix `ledwall_sync/wifi_secrets.h` on the laptop (`MATRIX`) before any Pico build there.
6. From `HANDOFF_24nov.md` section 3: NR-03 by camera (tap to flash under 300 ms), Week 13 evidence, defence docs (NR-14, now also the drift limit and why), the smaller list.

## 4. Method notes from tonight

- **NR-07 by camera:** play `sync_3x3.gif`, film **two tiles side by side, square-on, straight, in the same row**, about 150 to 210 px wide in the picture, phone braced, fingers off the LED faces. Six tiles in one picture (`IMG_3431`) did not read: tilted tiles and small, over-exposed LEDs.
- **Check the file before analysing:** iPhone Slo-mo 240 fps arrives as a 30 fps file with every frame a 240 fps capture: about **2 minutes long for 15 s of filming** (8x). A file about 1 minute long (4x) holds only 120 frames per real second (iPhone 15 Pro files tonight: rendered at quarter speed on the way). One 120 fps frame is 8.33 ms, too coarse for the 5 ms rule. The analysis also shows it: about 63 counter steps per 10 s piece at 240 fps, about 125 at 120 fps. Send originals (AirDrop, or Share, Options, All Photos Data, or a USB copy with Keep Originals).
- Analysis: `tools/segments.py` (one row) or `tools/segments2.py VIDEO 10 960 XA0,XA1,YA0,YA1 XB0,XB1,YB0,YB1 OUTDIR` (any layout; `SEG_FPS=120` for 120 fps files): 10 s pieces, boxes from the LED extent, `--threshold 200` (cards about 156, LEDs 240 and up). Use pieces within 2 % of 240 fps. Then `tools/outliers.py VIDEO START BOX_A BOX_B OUTDIR TAG` for every usable piece: missed steps per tile and every pair over 4 ms with decoded values and crop strips.
- Reading the outliers: **misread** = both tiles show the new value on the same frame, one tile unreadable for a few frames; **granularity** (4.0 to 4.9 ms) = one tile has a part-changed frame, both clean on the same frame; **real skew** = one tile shows each new value a whole frame earlier, on repeated steps. The GIF's 13-value loop jump is not a missed step.
- `tools/tile_capture.py PORT LOG SECONDS` logs a tile's USB serial without resetting it (DTR and RTS low); `tools/ts_replay.py LOG...` replays the tile time-sync filter on the `BCN` lines (`DRIFT_MODE=none` or `clamp50` for counterfactuals).

## 5. Where the session stopped (24 Nov, about 21:15)

Nothing half done. NR-07 is recorded as met, the fix is on tiles 4 to 9 and committed, the records and this file are committed. The wall was left playing `sync_3x3.gif` on tiles 4 to 9 with the website running on the laptop.

Untracked on the laptop, not to commit: `docs/tribunal1/` (the user's), `panel_syncmainwifi_secrets.h` (empty, from a Git Bash command that dropped backslashes; delete), `tools/pico_tap/test/test_tap_ring` and `test_tap_ring_asan` (test build output; delete or add to `.gitignore`). In `C:\Users\ahsan\Downloads`, `IMG_4536.MOV` and `IMG_4537.MOV` are truncated copies (the `(2)` files are complete). The videos themselves (`IMG_3429` to `IMG_3437`, `IMG_4536` to `IMG_4538`) are in the user's Downloads, not in the repo.
