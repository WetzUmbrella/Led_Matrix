# Led_Matrix: Distributed LED Matrix Video Wall

[![Build firmware](https://github.com/WetzUmbrella/Led_Matrix/actions/workflows/build.yml/badge.svg)](https://github.com/WetzUmbrella/Led_Matrix/actions/workflows/build.yml)

INF2004 Embedded Systems, team SE15.

**Demo video: https://youtu.be/YIwOyPXFfyE**

A wall of up to 9 ESP32-S3 tiles (8x8 WS2812B LEDs each), coordinated over
WiFi by a Raspberry Pi Pico W. The wall plays a GIF in sync across every tile
and runs a whack-a-mole game for children. Both are driven from a website on
the PC.

- **Video mode:** the PC cuts a GIF into one piece per tile and uploads each
  piece. The Pico keeps the master clock. Each tile copies its video into
  RAM and starts on the same 60 Hz tick as the others.
- **Game mode:** moles pop up on the tiles. A tap is detected by the tile's
  accelerometer, sent to the Pico and scored there. The countdown, score and
  stars are drawn across the whole wall.

---

## Contents

1. [Architecture](#1-architecture)
2. [Repository layout](#2-repository-layout)
3. [Hardware](#3-hardware)
4. [Setup](#4-setup)
5. [Running the demo](#5-running-the-demo)
6. [Tests](#6-tests)
7. [Documentation](#7-documentation)
8. [Known limits](#8-known-limits)
9. [Troubleshooting](#9-troubleshooting)
10. [Team](#10-team)
11. [AI use](#11-ai-use)

---

## 1. Architecture

```
   [PC: website pc/web.py]                    [WiFi router, 2.4 GHz]
     |  SESSION_CONFIG, commands  -> Pico 4212        |
     |  MANIFEST, ASSET_CHUNK     -> tiles 4214       |
     |  <- TELEMETRY (broadcast 4215), STATUS         |
     |                                                |
   [Pico W coordinator]  -- BEACON every 100 ms, commands, game msgs -> tiles 4210
     ^                   <- REGISTER, heartbeat, ASSET_READY, TAP ----- tiles 4211
     |
   [Tile 1] [Tile 2] ... [Tile 9]   ESP32-S3, same firmware on every tile
```

| Part | Role |
|---|---|
| Pico W (`ledwall_sync/`) | Master clock (beacon every 100 ms), session and readiness checks, START_AT and PAUSE/RESUME, game rules and scoring |
| ESP32-S3 tiles (`panel_sync/`) | Time sync to the Pico clock, video storage in RAM, 60 Hz display, orientation and tap detection (1 kHz IMU) |
| PC (`pc/`) | Website, GIF cutting and resampling to 60 fps, upload, telemetry dashboard |
| Shared codec (`common/proto/`, `pc/proto_layout.py`) | One bit-packed message layout for C and Python ([`protocol/layout_table.md`](protocol/layout_table.md)) |

All messages are UDP. Every important message has a sequence number and an
ACK, and is resent until it is ACKed. Repeats are ACKed but applied only once.

## 2. Repository layout

```
Led_Matrix/
├── panel_sync/      tile firmware (ESP-IDF), the same on every tile
├── ledwall_sync/    Pico W coordinator (Pico SDK)
├── common/proto/    shared C message codec, with host tests
├── pc/              website, GIF builder, uploader, dashboard (Python)
├── protocol/        message layout table and cross-language test vectors
├── tools/           test runner, coding-standard checker, bench tools
├── boardtest/       hardware smoke test for one tile (ESP-IDF)
├── docs/            design defence notes, test report, bench records
└── .github/         CI: builds both firmwares and runs every host test
```

`build/`, `sdkconfig`, `wifi_secrets.h`, `pc/gifs/` and `pc/logs/` are local
only and git-ignored.

## 3. Hardware

| Item | Count | Notes |
|---|---|---|
| Waveshare ESP32-S3-Matrix (8x8 WS2812B, QMI8658 IMU) | 1 to 9 | LED data on GPIO14 |
| Raspberry Pi Pico W | 1 | the coordinator |
| WiFi router, 2.4 GHz | 1 | settings below; keep only the wall on it |
| USB power for every board | 1 per board | power banks or a multi-port charger; a flat power bank shows up as tile dropouts |
| PC or laptop | 1 | on the same router, by WiFi or Ethernet |

## 4. Setup

Developed on Linux and on Windows with WSL2 (Ubuntu). Clone into the Linux
filesystem, not under `/mnt/c` or a cloud-synced folder.

### 4.1 Toolchains (once per machine)

**ESP-IDF v5.3** (tiles):

```bash
mkdir -p ~/esp
git clone -b v5.3 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
~/esp/esp-idf/install.sh esp32s3
echo "alias get_idf='. $HOME/esp/esp-idf/export.sh'" >> ~/.bashrc
```

Run `get_idf` in every new terminal before `idf.py`.

**Pico SDK 2.3.1** (coordinator), with CMake 3.17 or newer and the ARM toolchain:

```bash
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib build-essential python3
mkdir -p ~/pico && cd ~/pico
git clone -b 2.3.1 --recurse-submodules https://github.com/raspberrypi/pico-sdk.git
echo 'export PICO_SDK_PATH=$HOME/pico/pico-sdk' >> ~/.bashrc
```

**Python 3.12** (PC tools and tests), from the repository root:

```bash
python3 -m venv .venv
.venv/bin/pip install pillow==11.0.0 pytest==8.3.3 clang-format==18.1.8
```

On Windows, pass each board's USB port into WSL with
[usbipd-win](https://github.com/dorssel/usbipd-win): `usbipd list`, then
`usbipd attach --wsl --busid <BUSID>`.

### 4.2 Router (once)

On the router's admin page, for the 2.4 GHz band (the ESP32-S3 and the Pico W
have no 5 GHz):

- Security **WPA2-Personal (AES)**. WPA3-only does not work.
- **AP isolation off**, or the tiles cannot reach the Pico. Do not use a guest network.
- **DTIM interval 1**, or broadcasts are held back and arrive in bursts.
- **A fixed channel (1, 6 or 11) at 20 MHz.** On auto channel the wall missed
  ticks. Turn off extras such as Turbo QAM, beamforming, TX bursting, airtime
  fairness and roaming assist (with them on, the Pico froze about once a round).
- A DHCP reservation for the Pico, so its address does not change.

### 4.3 WiFi credentials

```bash
cp ledwall_sync/wifi_secrets.example.h ledwall_sync/wifi_secrets.h
cp panel_sync/main/wifi_secrets.example.h panel_sync/main/wifi_secrets.h
# edit both: ROUTER_SSID and ROUTER_PASS ("" for an open network)
```

The SSID is case-sensitive and must match in both files. Never commit
`wifi_secrets.h`. Without it the build uses placeholders (that is what CI does).

### 4.4 Tiles: give each a number, then flash

Each tile needs a permanent number from 1 to 9. It is the tile's Node ID and
its default place in the wall (1 is top left, in reading order). It is stored
in NVS, so it survives reflashing; `idf.py erase-flash` wipes it.

```bash
get_idf
cd panel_sync
idf.py set-target esp32s3            # first time only
./set_board_number.sh 1              # once per tile: 1, 2, ... 9, each unique
idf.py -p /dev/ttyACM0 flash monitor
```

The boot log shows `tile_net: ESP-<n>, node <n>`, then beacon and `TN_STATS`
lines once the Pico is up. Exit the monitor with `Ctrl+]`. If flashing hangs
at "Connecting...", hold **BOOT**, tap **RESET**, release **BOOT** and retry.
Label each tile with its number.

### 4.5 Pico W: build and flash

```bash
cd ledwall_sync
mkdir -p build && cd build
cmake .. && make -j4
```

Hold **BOOTSEL**, plug in the Pico and copy `build/ledwall_sync.uf2` onto the
**RPI-RP2** drive. Its USB serial log shows `COORDINATOR up: ip=...`. Note
that IP address for the website.

### 4.6 Why no ready-made firmware images are committed

The WiFi name and password are compiled into both images, and anyone who has
the file can read them back out of it. So the repository ships no `.uf2` or
`.bin`. Build them on your own machine with your own `wifi_secrets.h`
(section 4.3):

| Board | Built image | Flash it with |
|---|---|---|
| Pico W | `ledwall_sync/build/ledwall_sync.uf2` | hold BOOTSEL, plug in, copy the file onto the RPI-RP2 drive |
| Each tile | `panel_sync/build/panel_sync.bin` (with its bootloader and partition table) | `idf.py -p /dev/ttyACM0 flash` from `panel_sync/` |

CI also uploads `ledwall_sync.uf2` and `panel_sync.bin` for every push, but
those carry the placeholder credentials from the example files, so they
prove the build and cannot join a network.

## 5. Running the demo

1. Power the router, then the Pico, then the tiles. Arrange the tiles in
   number order (1 top left, reading order).
2. Put a GIF in `pc/gifs/` (any size; it is resampled to 60 fps and cut per
   tile), for example `mkdir -p pc/gifs && cp docs/demo/test.gif pc/gifs/`, or
   upload one later from the website.
3. Start the website on the PC, from the repository root:

   ```bash
   .venv/bin/python pc/web.py --pico <PICO_IP> --layout 3x3 --gif <NAME>.gif --policy continue
   ```

   `--layout` is rows x columns (for example `1x1`, `2x2`, `3x3`). `--map`
   gives the tile numbers in reading order when they are not 1, 2, 3 and so on.
   `--policy continue` keeps the show going when a tile drops out (see known limits).
4. Open **http://localhost:8080** (or `http://<PC_IP>:8080` from a phone on
   the same network). The scoreboard for a second screen is at `/score.html`.
5. **Video:** wait until every tile shows ready, then press Play. Pause,
   Resume and Stop work at any time.
6. **Game:** switch to game mode (only while stopped), pick a level and press
   Arm. Holes appear on every tile and a start sprite on one; tap it to start
   the 3-2-1 countdown and a 30 s round. The tap test shows every tap and its
   peak, and the tap threshold can be set from the website without reflashing.

## 6. Tests

Every host test suite, with logs kept as evidence:

```bash
./tools/run_all_tests.sh             # writes docs/testing/results/<today>/
```

By requirement (the map from each FR and NFR to its tests is
[`docs/testing/TRACEABILITY.md`](docs/testing/TRACEABILITY.md)):

```bash
.venv/bin/python tools/traceability.py --list      # every requirement and its tests
.venv/bin/python tools/traceability.py FR14        # run only FR14's tests
.venv/bin/python tools/traceability.py all         # every requirement, one by one
```

Each requirement prints its host test cases with PASS or FAIL, then the
system tests on the wall that cover it, with their recorded verdict.

Or one suite at a time:

| Suite | Command |
|---|---|
| Tile modules (fault word, telemetry, asset receive, playback, display, tap detect, game) | `make -C panel_sync/test run` |
| Pico coordinator and game rules | `make -C ledwall_sync/test run` |
| Shared C codec | `make -C common/proto/test run` |
| Python (codec twin, cross-language vectors, website, uploader) | `.venv/bin/python -m pytest -q tools/tests pc/tests` |

Each `make` target also has an `asan` variant (AddressSanitizer and
UndefinedBehaviorSanitizer). The C tests compile the real firmware source
files with gcc on the PC; only the network, the other device and the clock are
faked. CI runs all of them on every push, plus both firmware builds, the
formatting check and the Barr-C checker (`tools/barr_check.py`).

Hardware tests (sync skew, tap-to-flash timing, game rounds, power pulls,
uploads) are the system tests ST-01 to ST-14: each has a record and its raw
logs in [`docs/testing/evidence/`](docs/testing/evidence/), summarised in the
test report.

## 7. Documentation

| What | Where |
|---|---|
| **Requirements** (FR1 to FR21, NFR1 to NFR16) | [`docs/requirements.md`](docs/requirements.md) |
| **Design**: how each part works and why | [`docs/design.md`](docs/design.md) |
| **Tribunal 1**: the feedback, the change requirement and how each subsystem handled them | [`docs/tribunal1/`](docs/tribunal1/) |
| **Test report**: every test level, the system tests on the wall, open failures, evidence gaps | [`docs/testing/TEST_REPORT.md`](docs/testing/TEST_REPORT.md) |
| **Requirement to test map**, with the result of each | [`docs/testing/TRACEABILITY.md`](docs/testing/TRACEABILITY.md) |
| Wall test records and raw logs, one folder per test | [`docs/testing/evidence/`](docs/testing/evidence/) |
| Host test logs of the submitted code, every case with its verdict | [`docs/testing/results/`](docs/testing/results/) |
| Message layout: every field, its range and its bits | [`protocol/layout_table.md`](protocol/layout_table.md) |
| Coding standard (Barr-C:2018 and the recorded deviations) | [`docs/coding_standard.md`](docs/coding_standard.md) |

## 8. Known limits

- **Failure policy:** with `--policy pause`, a tile lost for more than 5 s
  sends the Pico to FAULT, and its router re-join can hang until the 8 s
  watchdog restarts it. Use `--policy continue`: the other tiles keep playing
  and a returning tile rejoins.
- **A tile restarted in the middle of a round** shows nothing until the
  results, because the round's START is not resent to a returning tile.
- **Several players tapping at once** can be judged against each other's
  echoes; the game is tuned for one player.
- **One Pico per router:** a tile locks onto the first Pico it hears. Keep
  other teams' Picos off the wall's network.
- Tap detection rate and false taps (NR-01, NR-02) were built and observed on
  the wall but never run as formal 50-tap cards.

## 9. Troubleshooting

| Symptom | Likely cause |
|---|---|
| Tile prints no beacon lines and never registers | wrong SSID or password in `panel_sync/main/wifi_secrets.h` (the SSID is case-sensitive), or AP isolation is on |
| Pico log has no `COORDINATOR up` | wrong credentials in `ledwall_sync/wifi_secrets.h`, or the router is on 5 GHz only |
| Website: a tile never turns ready | the tile is not registered (check its power and log), or its number is not in `--layout`/`--map` |
| A tile has the wrong place in the wall | its board number; rerun `set_board_number.sh`, or pass `--map` |
| Tiles drop out one after another | a flat power bank or an overloaded USB hub |
| The wall misses ticks or the Pico freezes | router extras on or auto channel (section 4.2), or a busy WiFi area |
| Taps are not detected | the threshold is too high for soft taps; lower it on the website (default 300 mg) |
| `CMakeCache.txt ... different directory` | a build folder copied from elsewhere: delete `build/` |
| `/dev/ttyACM0: No such file` | the board is not attached (on WSL, `usbipd attach`) |

## 10. Team

| Member | Subsystem |
|---|---|
| Ong Jolyn | PC website, dashboard and media |
| Wong Kok Sheng Benjamin | Pico W coordinator and protocol |
| Cheston Leroy Ong | Tile communication, storage and game rules |
| Ahsanul | Time sync and playback control |
| Chua Jia Jun | Display, orientation and tap detection |

An earlier ESP-NOW design (one board pushing pixel data to the others) was
abandoned before 4-board testing. It is preserved at tag
[`v0-esp-now-single-node`](../../tree/v0-esp-now-single-node).

## 11. AI use

This project was built with substantial help from AI tools, which the
module's tribunal process also uses (AI ideation and an AI-vetted gap check).

- **Tribunal 1.** AI was used as a reviewer and challenger. The prompt, its
  suggestions and the team's decision on each are in the Tribunal 1
  document. [`docs/tribunal1/`](docs/tribunal1/) lists the gaps we accepted
  and fixed, and the suggestions we rejected, with reasons.
- **Code, tests and documents.** Much of the firmware, PC code, tests and
  documentation was drafted with Claude Code (Anthropic), in sessions run by
  team members. The team set the requirements, made the design decisions,
  and reviewed every change. Each change had to pass the host tests, the
  Barr-C checker and both firmware builds before it was committed.
- **Hardware results** (every wall test in
  [`docs/testing/evidence/`](docs/testing/evidence/)) were measured by the
  team on the real wall. None of them were generated.
