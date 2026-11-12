# Session handoff, 12 Nov 2026 night (whack-a-mole: Matrix router, 9 tiles, Pico receive-buffer bug open)

For Cheston (or the next agent) starting cold. It carries on from `HANDOFF_12nov.md` (the evening version), which stays as it was. This session ran on a second laptop (Windows) and followed that file: step 4d passed on the wall, then the bench moved to the Matrix router and grew to 9 tiles. The 9-tile video found a real step 4d bug on the Pico; fixing it is the next job (section 3).

Read in this order before doing anything:
1. This file, all of it.
2. `HANDOFF_12nov.md` sections 2 and 2a: the method (decisions one at a time, "go" before every round, records, git). Unchanged, and followed this session.
3. `docs/bench/12nov/step4.md`: the "Step 4d bench", "Matrix bench" and "9-tile bench" sections.
4. `docs/plan/game_plan_v2.md` DS-15 (sparse grid, with the 12 Nov step 4d decisions) and DS-17 (demo network).

If you cannot see the user's personal files, ignore every mention of them; nothing here depends on them.

## 1. Where things stand

| Item | State |
|---|---|
| Branch | `feature/whack-a-mole` in `Led_Matrix` |
| Step 4d | **Passed on the wall 12 Nov** (V1, rounds 11 and 12; decision 3 stands), committed `d2a7911` |
| Network | **Matrix router** (DS-17), Wireless router mode. Pico 192.168.1.154. Details in section 6. |
| Tiles | **9 tiles**, numbers 1 to 9, all online and clock-synced on the Pico (`online=0x1ff sync=0x1ff`). Tiles 6 to 9 had no board number; numbers written this session. |
| Firmware on every board | The code of `2593a48` (no firmware, PC or protocol change since), rebuilt with the Matrix credentials. Pico: `make` exit 0. Tiles: `idf.py build` exit 0 after `idf.py set-target esp32s3`. Every write hash-verified. |
| 9 tiles, game | **Round 14 passes** (3x3, no video). |
| 9 tiles, video | **Blocked by a Pico bug** (section 3): tiles 8 and 9 never count ready, so START is refused. |
| Gate | Not run on this laptop (no `game_handoff_bundle` here). Nothing in the code changed, so the 4d gate result still stands. Run it before the fix's commit. |
| Working tree | Clean after the docs commit with this file. Commit and push only when the user asks. |

## 2. How to work

Unchanged: `HANDOFF_12nov.md` sections 2 and 2a. Added this session:
- **Log every board that is on USB**, not only nodes 4 and 5. Name logs by board (USB serial number), never by port.
- **Read the Pico's `online=`, `ready=` and `sync=` masks** in `CL_STATS`: bit n-1 is Node n (`0x1ff` is all nine). A tile with no board number never shows up there.
- **Grep the Pico log for `CRC_MISMATCH`** after any upload. It names the node and the CRC the tile reported.
- **Dry-run any script that flashes boards** and check what it would touch before running it for real. This session's first flash script reflashed tile 5 twice with the same image (harmless, but a mistake).
- **Tile USB serial drops lines**: a tile's boot lines are often lost if the logger starts after the boot. To see the boot, reset the tile with the logger running.

## 3. Next: fix the Pico receive buffer (step 4d bug, found on the wall)

### 3.1 What goes wrong

- The Pico copies every datagram into a fixed buffer: `#define CL_RX_MAX_BYTES 48u` (`ledwall_sync/coordinator_loop.c:121`; its comment still says "SESSION_CONFIG is 46 B"). Used by `cl_rx_entry_t.bytes[]` (`:179`), `cl_rx_cb()` (`:3196-3208`, `pbuf_copy_partial` of at most 48 bytes) and `cl_enqueue()` (`:1887-1912`).
- Step 4d made SESSION_CONFIG **53 B**. `cl_enqueue()` copies 48 bytes but keeps `len` at 53, as its comment says, "so the decoder rejects the wrong length". 53 is the right length, so the decoder accepts it and **reads bytes 48 to 52 from past the end of `bytes[]`**.
- Bits 384 and up are lost: the last bit of `crc32[7]` (Node 8) and all of `crc32[8]` (Node 9). With 7 tiles or fewer every CRC sits in the first 48 bytes, which is why 4d passed on 2 tiles.
- Seen on the wall (run V2, session 29): all 9 uploads 57 of 57; the Pico logged `CRC_MISMATCH node=8 value=3602527130` (0xd6ba339a, the CRC the PC built) and `CRC_MISMATCH node=9 value=207658247` (0x0c609d07, also the PC's); `ready=0x07f` for the rest of the run; the PC refused START ("not ready [8, 9]").
- Game rounds do not use the CRCs, and the cells (bits 38 to 118) sit inside the 48 bytes, so round 14 on 9 tiles passed. The out-of-bounds read still happens on every SESSION_CONFIG.
- The host tests decode from full-size buffers and never go through `cl_enqueue()`, so they could not see it.

### 3.2 Fix (a bug fix, not a design decision; say so to the user in one line)

1. Size the buffer from the protocol: the largest datagram the Pico accepts is SESSION_CONFIG (`PROTO_SESSION_CONFIG_BYTES`, `common/proto/proto.h:212`). Check the other types the Pico decodes (REGISTER, HEARTBEAT, ASSET_READY, SYNC_READY, TELEMETRY, TAP, ACK, GAME_CONTROL, the playback commands) and take the largest; fix the stale comment.
2. Add a compile-time check that each accepted type fits in `CL_RX_MAX_BYTES`, so a later wire change cannot bring this back silently.
3. In `cl_enqueue()` (or the dispatch), count a datagram longer than the buffer as `rx_bad` and drop it, instead of keeping the long `len` for the decoder. That removes the out-of-bounds read for good.
4. Host test that **fails without the fix** (stash the fix and see it fail, as in section 2a): a full 9-tile SESSION_CONFIG through the receive path, then check that Node 9's CRC and cell are what was sent. If `cl_enqueue()` is not reachable from `ledwall_sync/test`, expose the smallest seam the existing tests use, or use the compile-time check plus a decode test from a 48-byte copy.
5. Gate, then rebuild and reflash the Pico only (tiles unchanged). Owner: the coordinator is Benjamin's to defend; add a line to the note you send him.

### 3.3 Wall runs after the fix (start each only after the user says "go")

| Run | Command | Expect on the wall | Expect in the logs |
|---|---|---|---|
| **V2 rerun**, video, 3x3, 9 tiles | `python -u docs/bench/12nov/video_check.py 192.168.1.154 31 docs/bench/12nov/numbered_cells_3x3.gif 3x3 1,2,3,4,5,6,7,8,9 15 > docs/bench/12nov/v2b_dashboard.log 2>&1` | Tile k shows k red LEDs from its top left; the same green counter moving in sync on all nine for 15 s; blank after STOP | 9 uploads of 57 of 57 (about 23 s in all, one tile after another); no `CRC_MISMATCH`; `ready=0x1ff`; START accepted; COORDINATING PLAYBACK then READY |
| Regression, game, 3x3, 9 tiles | `python -u docs/bench/11nov/game_round.py 192.168.1.154 32 - 1,2,3,4,5,6,7,8,9 --layout 3x3 --no-video --level easy --threshold 300 --tap-test 20` | As round 14: arrow on tile 5, countdown and results big across the wall | READY with no upload, RESULTS, `faults=none` |

Record both in `step4.md` under a new "Pico receive-buffer fix" section.

### 3.4 Then look at the display misses with 9 tiles

During round 14 the tiles' `DE ticks` counters grew while the round ran: `missed` by about 1,200 and `jumps` by 3 on each logged tile (nodes 4, 5 and 9). On 2 tiles (rounds 11 to 13) they stayed flat during runs. Cause not established. Start by reading which runs add jumps (a Pico clock step) and whether misses follow them, before changing code (section 2a, "do not guess hardware faults").

## 4. Then: 4e website

As `HANDOFF_12nov.md` section 4. One measured number for the parallel-upload decision (DS-14): uploads one tile after another to 9 tiles took about 23 s (2.20 to 3.42 s per tile, run V2), against the 6 to 10 s expected in parallel.

## 5. After step 4

As `HANDOFF_12nov.md` section 5. "9 tiles on the Matrix router" is now done (game); the 9-tile video waits for section 3.

## 6. Bench setup (Matrix)

**Router:** Matrix, WPA2-Personal, 2.4 GHz, channel 1, no internet. It must be in **Wireless router mode**: in AP mode it hands out no addresses, the Pico logs `router join not done in 10000 ms` every 12 s and the tiles stay off the network. The Pico got 192.168.1.154 again (the DS-17 address); check the DHCP reservation for MAC `2c:cf:67:d7:eb:c3` if it moves.

**Boards** (USB serial number = MAC; tile addresses come from DHCP and may change):

| Board | Number | MAC / USB serial | Address on Matrix |
|---|---|---|---|
| Pico coordinator | - | `2c:cf:67:d7:eb:c3` (USB serial `E66430A64B595138`) | 192.168.1.154 |
| Tile | 1 | `9C:13:9E:F1:30:70` | 192.168.1.86 |
| Tile | 2 | `B8:F8:62:D8:09:F8` | 192.168.1.144 |
| Tile | 3 | `B8:F8:62:D8:05:EC` | 192.168.1.97 |
| Tile | 4 | `9C:13:9E:F1:2E:B0` | 192.168.1.6 |
| Tile | 5 | `9C:13:9E:F1:2F:70` | 192.168.1.14 |
| Tile | 6 | `9C:13:9E:F1:2F:E4` | 192.168.1.130 |
| Tile | 7 | `9C:13:9E:F1:2C:70` | 192.168.1.51 |
| Tile | 8 | `9C:13:9E:F1:2B:D0` | 192.168.1.75 |
| Tile | 9 | `B8:F8:62:D8:0E:D8` | 192.168.1.219 |

The wall is a 3x3 in reading order (1 2 3 / 4 5 6 / 7 8 9), map `1,2,3,4,5,6,7,8,9`.

**Board numbers:** tiles 6 to 9 arrived with none (boot line `tile_net: ESP-0, node 0 (no board number: run set_board_number.sh)`; such a tile never registers). Numbers were written as `panel_sync/set_board_number.sh` does it (an NVS image with `board:number`, written at 0x9000). Reflashing the firmware leaves it in place. A tile with no stored video shows a dim blue LED 0 (bottom right as mounted; `display_frame.c:44`): the designed "no asset" status, gone after the first upload.

**The laptop on Matrix:** Matrix has no internet, so keep the laptop's WiFi on a normal network and plug a cable into a router **LAN** port (not WAN: the WAN port hands out no address).
- Linux: make sure the default route stays on WiFi (the Ethernet link gets a gateway from Matrix); route only 192.168.1.0/24 over Ethernet. For a phone or TV to open the website, DS-17's `ufw` line.
- Windows (this session): Ethernet interface metric 9000 (`netsh interface ipv4 set interface "Ethernet" metric=9000`, admin), and leave the Ethernet network **Public**: Windows Firewall allowed Python inbound on Public only, and with Private the Pico's ACKs and STATUS were blocked ("the Pico did not acknowledge the mode change").

**WiFi credentials:** every board now runs a Matrix build. Your laptop's `wifi_secrets.h` files are probably still the hotspot: switch both to Matrix before you rebuild anything (DS-17 says where the tiles' copy is backed up). Never print or commit them.

**Building and flashing on Windows** (if the bench stays on this laptop): build both firmwares in WSL (ESP-IDF and the Pico SDK are there). A fresh `panel_sync` tree defaults to the plain ESP32, so run `idf.py set-target esp32s3` first. Flash tiles with esptool from the venv (`python -m esptool --chip esp32s3 -p COMx -b 460800 write_flash @flash_args` in `panel_sync/build`). Flash the Pico without the button: open its COM port at 1200 baud (it reboots to BOOTSEL), then copy `ledwall_sync/build/ledwall_sync.uf2` to the RPI-RP2 drive. The bench scripts run unchanged with Windows Python; map ports by USB serial number with `serial.tools.list_ports` instead of `udevadm`.

**Tools:** `docs/bench/12nov/tap_only.py` only works when the Pico already holds a session (the tap test needs READY, which needs a layout). After a Pico restart use `game_round.py`, which sends the layout first.

## 7. Known limits and open items

- **The Pico receive-buffer bug** (section 3): no 9-tile video until it is fixed.
- **Display misses and jumps grow during a 9-tile round** (section 3.4).
- **Echoes on a 3x3 wall:** in round 14, 58 taps formed 36 judged groups (27 hits and 9 wrong). Whether all 9 wrong taps were deliberate was not asked. This is the first wall with neighbours on every side; DS-03 asked for a re-check of "highest peak wins" with more tiles, so run it.
- **Uploads on this laptop took 3.2 to 3.3 s per tile** (V1), against 2.43 to 2.81 s on your laptop in 4c. Nothing was resent. Cause unknown.
- The open items from 4c stand (`HANDOFF_12nov.md` section 4, step 2), and the rest of its section 7.
