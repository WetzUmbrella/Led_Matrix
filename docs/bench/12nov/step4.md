# Step 4 bench, 12 Nov 2026: no-video rounds and a restarted PC

Branch `feature/whack-a-mole`. Pico `…E66430A64B595138` (172.20.10.2) flashed with `f1a8805` (step 4a `898826f`, step 4b `f1a8805`); tiles 4 (172.20.10.4) and 5 (172.20.10.5) still on `73ad599`, on sponge, 1x2 wall, phone hotspot. Each round ran `docs/bench/11nov/game_round.py 172.20.10.2 20 - 4,5,0,0,0,0,0,0,0 --no-video --level easy --threshold 300 --tap-test 20` (layout only, a 20 s tap test, then ARM; the operator's tap on the start sprite starts the countdown). Serial logs by `serial_log.py`; the ports change after a reconnect, so each log is named by the board's USB serial number, not the port.

| Check | Result | Notes |
|---|---|---|
| Build and gate | PASS | At `f1a8805`: C host tests and ASan (C28 for 4a), barr_check 0 findings, pytest 441 passed. |
| Round 6 (4a: no video, Easy, 300 mg) | **PASS** | The PC sent the layout alone (no build, no upload). The Pico went from WAITING FOR TILE READINESS to READY once game mode was set, both tiles online, no faults. Tap test lit the tapped tile each time; start sprite, countdown and results seen, no flicker (operator). 46 of 46 moles, 4 wrong, 326 ms reaction, 3 stars. The 4 wrong taps were the operator picking the tiles up as the round started (moving a tile sets off taps, as in step 3), not echoes. `round6_*.log` |
| Round 7 (4b: PC restart, same session 20) | **PASS** | A new process with the same session: the Pico ignores the repeated SESSION_CONFIG, so only the saved seq makes the commands count. `pc/.last_seq` held 5 after round 6 and 74 after round 7 (the new process started 64 past the saved value). Every command took effect: mode, tap test, threshold, ARM (ARMED 0.3 s after the command), countdown on the start tap, round, results. Start sprite, countdown and results seen as in round 6 (operator). 39 of 39 moles, 2 wrong (the operator's own), 432 ms reaction, 3 stars; no faults. Before 4b the Pico would have dropped all of these as stale and the start sprite would never have shown. `round7_*.log` |

Found on the bench:
- GAME IDLE showed part of the last round. At the start of round 7 the Pico still held round 6's RESULTS; the mode command took the round state to GAME IDLE, where STATUS hides the score but still sent wrong=4, shown=46 and reaction=326 until the next ARM cleared them. Fix: a mode command clears the counts (`game_rules_clear()`), checked in host test C27. Pico change only; not yet flashed or run on the wall.
- The tap test ran over round 6's results (operator, round 7). The results list stays on a tile until the next ARM or START (DS-09), and neither the mode command nor the tap test clears it, so the tapped tile lit up on top of the looping score and stars while STATUS said GAME IDLE. Not designed either way: open, to decide with step 4c.

## Result

**Steps 4a and 4b pass on the wall 12 Nov.** A game round needs no video (A14), and a restarted PC's commands count. Next: step 4c (DS-12 upload fixes), 4d (sparse grid), 4e (website).

# Step 4c bench, 12 Nov 2026: upload fixes

Built for 4c (DS-12) and the results decision above (decided with the operator: the wall goes blank on the tap test and on a mode switch; the results keep looping otherwise):

- **A1** the uploader waits up to 3 s for the tile's TELEMETRY header to show the new session before its MANIFEST (`pc/dashboard.py`, `pc/telemetry.py`).
- **A2** a round in which the tile ACKs nothing ends with the MANIFEST sent again and every chunk counted as missing; while nothing is ACKed yet the round stops after its first silent window (`pc/uploader.py`).
- **B** a MANIFEST for the asset already published opens a held upload: every chunk is ACKed as a repeat, nothing is written (`asset_rx`, `asset_store`; host test A6).
- **C** when the rounds run out, a tile that the Pico's STATUS (received after the question) counts ready is uploaded.
- **Speed** the next window goes as soon as every chunk of this one is ACKed, not after the full 0.25 s.
- **Results** a mode command or the tap test sends every tile a picture CLEAR and clears the counts (C26, C27). A new SESSION_CONFIG ends a tap test (C29).
- **Display clock jump** (found below) a tick deadline more than 100 ms ahead is a clock step back: the tick is dropped and the next one after now is shown, counted as `jumps=` in the `DE ticks` line, no fault.

Gate at each change: C host tests and ASan, barr_check 0 findings, pytest 451 passed.

| Check | Result | Notes |
|---|---|---|
| Round 8 (session 21) | **Upload PASS**, run interrupted | Both tiles took the first upload (57 of 57, 5.19 s each); before 4c tile 4 failed the first upload of nearly every new session. The run was stopped at the tap test after the operator replugged tile 5; that left the tap test on in the Pico. `round8_*.log` |
| Round 8b (session 22) | **PASS** | First upload 57 of 57 on both, no resends, 5.19 s each. 41 of 41, 0 wrong, 406 ms, 3 stars. Taps during the upload flashed green with no hole: the Pico still had round 8's tap test on, the tiles did not (both had restarted). No tap was logged in the 20 s tap test itself. `round8b_*.log` |
| Tap test only (session 22) | **PASS** | 30 s tap test, no round (`tap_only.py`). Both tiles showed the hole and flashed green on taps, the hole coming back after (operator). The first try was refused by the PC: the script asked before any STATUS (fixed in the script). `taponly_*.log` |
| Round 9 (session 22 again, new process) | **PASS (B)** | Both tiles logged `already held` and wrote nothing (flash counters unchanged); 57 of 57 ACKed on both. Still 5.19 s each: the uploader waited the full 0.25 s per window (fixed: speed). 41 of 41, 1 wrong, 402 ms, 3 stars. `round9_*.log` |
| Round 10 (session 23) | **Not a test: Pico dead** | After `picotool load -x -f` with the speed fix and C29, the Pico sent no serial, answered no ping and sent no beacons; the tiles stayed in session 22, so every upload was "0 of 57". A replug brought it up with the same firmware. `round10_*.log` |
| Round 10b (session 23) | Upload **PASS**; **bug: LEDs frozen** | 57 of 57 in 2.43 s and 2.81 s (speed fix). Both tiles printed no `DE ticks` line all run and the start sprite never showed (arm timed out). Cause: the Pico restart (the replug) stepped the corrected clock back by its old uptime when Time Sync re-seeded, so the display's next deadline lay minutes ahead and the gptimer waited it out. Also explains the dark tiles this morning (the Pico was flashed after the tiles) and why a replugged tile recovered. Fix: the display clock-jump guard. `round10b_*.log` |
| Pico replug with the guard | **PASS** | Tiles flashed with the guard (no replug), then the Pico replugged under them: both kept ticking, `jumps=1` on each. |
| Round 10c (session 24) | **PASS** | Tile 5 57 of 57 in 2.57 s. Tile 4 5.44 s, 58 resent: its first window was not ACKed within 0.25 s after the 1 s settle (most likely still erasing), so A2 sent the MANIFEST again and the whole asset once more; correct but slow. 52 of 52, 1 wrong, 249 ms, 3 stars; no faults; both displays ticked throughout. `round10c_*.log` |

Open from 4c:
- The first window after a MANIFEST should wait longer (about 1 s) before A2 counts the tile as deaf (round 10c).
- A tile that restarts is not sent the tap test or the round state again (round 8: a replugged tile stays dark in the tap test).
- The Pico `…595138` once did not start after `picotool load -x -f`; a replug fixed it. Cause unknown.
- The display freeze was invisible to every check (no fault, display state normal): a display that stops ticking should raise DISPLAY.

## Result (4c)

**Step 4c done 12 Nov.** The upload race is gone (the first upload passed on both tiles in every new session), a tile that holds the asset answers the PC, uploads take about 2.5 s per tile, and the results go blank on the tap test and a mode switch. Found and fixed on the wall: tile LEDs freezing after a Pico restart. Next: 4d (sparse grid), 4e (website).

# Step 4d bench, 12 Nov 2026: sparse grid

Branch `feature/whack-a-mole`. Pico `…E66430A64B595138` (172.20.10.2) on the 4d build (same code as `2593a48`); tiles 4 (172.20.10.4) and 5 (172.20.10.5) on the 4c build with the display clock-jump guard (same code as `dc71a04`; 4d does not change the tiles), on sponge, phone hotspot. Run from a second laptop (Windows) on the same hotspot: the scripts are unchanged; only the port lookup differs (pyserial's USB serial numbers instead of `udevadm`), and each log is still named by board. Health check before V1: both tiles printed `DE ticks` with `missed=0`, the Pico `CL_STATS` with `online=0x018 sync=0x018`.

| Check | Result | Notes |
|---|---|---|
| V1 video, 3x3 corners (session 25) | **PASS** | `video_check.py ... numbered_cells_3x3.gif 3x3 4,0,0,0,0,0,0,0,5 15`. Both uploads 57 of 57, resent 0: tile 4 3.33 s, tile 5 3.20 s; `ASSET ready` with the PC's CRCs (`0x96b418c2`, `0x0c609d07`), flash 58 writes, 1 erase. Pico READY, COORDINATING PLAYBACK, READY again 15.1 s later at STOP. Operator: tile 4 showed 1 red LED top left, tile 5 showed 9 (top row and one of the second), the same green counter moving in sync on both, then blank after STOP (designed: a stopped tile shows no frame). No faults. `v1_*.log` |
| Round 11 game, 3x3 corners (session 26) | **PASS** | `game_round.py ... 4,0,0,0,0,0,0,0,5 --layout 3x3 --no-video --level easy --threshold 300 --tap-test 20`. Layout only, no upload (flash counters unchanged). Operator: hole and green flash in the tap test; start arrow on **tile 4** (the first cell wins the tie); countdown and results small and whole on each tile (2 of 9 cells is under half). 34 of 35 moles, 1 missed, 6 wrong (made on purpose to check wrong taps), 490 ms, 3 stars; score 34 = hits. Pico: 35 moles, 40 tap groups, 40 FEEDBACK. `faults=none`. `round11_*.log` |
| Round 12 game, 1x3 with an empty middle (session 27) | **PASS** | Same, map `4,0,5`, `--layout 1x3`. Operator: start arrow on tile 4; countdown and results drawn big across the 3 cells, each tile showing its outer third (2 of 3 cells is at least half); **reads well enough for a child, so decision 3 stands**. 36 of 37 moles, 1 missed, 5 wrong (on purpose), 450 ms, 3 stars. Pico: 37 moles, 43 tap groups, 41 FEEDBACK; the other 2 taps (tile 4, 2.1 s and 2.4 s into the countdown) were ignored as designed (DS-08). `faults=none`. `round12_*.log` |

All three runs: no crash words in any log; both tiles ticked throughout (15, 24 and 24 `DE ticks` lines), `jumps=0`. `prep_max_us` reached 505 µs in game drawing, against 272 µs in video.

Found on the bench:
- Uploads took 3.2 to 3.3 s per tile, not the predicted 2.5 s (4c rounds: 2.43 to 2.81 s). Nothing was resent, so it was not retries. The only known difference is the laptop; cause not established.
- Each tile shows `missed=1` in every run: 0 in the health check, 1 at the start of the V1 log, unchanged through all three runs. The one missed tick came while the wall was idle, before V1; cause not established.

Open:
- The optional regression round (1x2 with video, session 28) was not run.
- The upload time and the single missed tick above.
- The open items from 4c stand (HANDOFF_12nov.md section 4, step 2).

## Result (4d)

**Step 4d passes on the wall 12 Nov (V1, rounds 11 and 12).** A tile placed anywhere in a 3x3 grid plays its own section of the video, game rounds run on a sparse grid with no upload, and wall pictures switch between big and small by how full the grid is. Next: 4e (website).

# Matrix bench, 12 Nov 2026 evening: the wall on the dedicated router

The bench moved from the phone hotspot to the Matrix router (DS-17). The Pico and both tiles were rebuilt with the Matrix credentials and reflashed (Pico `make` exit 0; tiles `idf.py build` exit 0 after `idf.py set-target esp32s3`; esptool and UF2 writes verified). Addresses: Pico 192.168.1.154, tile 4 192.168.1.6, tile 5 192.168.1.14. The laptop stays on its normal WiFi and reaches Matrix by Ethernet (setup in `HANDOFF_12nov_night.md` section 6).

| Check | Result | Notes |
|---|---|---|
| Join with the router in AP mode | **Not a test: no DHCP** | The Pico logged `router join not done in 10000 ms` every 12 s; the laptop's Ethernet got 169.254.x. Switched to Wireless router mode, and the cable moved from WAN to a LAN port: all three boards and the laptop got addresses. |
| Tap test only (session 28) | **Not a test: firewall** | "the Pico did not acknowledge the mode change". The laptop's Ethernet was set to Private, and Windows Firewall allows Python inbound on Public only. Set back to Public, a mode command was ACKed and STATUS arrived. `tap_only.py` cannot run on a freshly booted Pico anyway: the tap test needs READY, which needs a layout. `taponly_matrix_*.log` |
| Round 13, 1x2 on Matrix (session 28) | **PASS** | `game_round.py 192.168.1.154 28 - 4,5 --layout 1x2 --no-video --level easy --threshold 300 --tap-test 20`. Layout only, no upload; Pico VALIDATING SESSION, SYNCHRONISING, READY, COORDINATING GAME, READY. Operator: hole and green flash in the tap test (tiles logged 30 and 38 taps); start arrow started the countdown; countdown and results big across both tiles. 28 of 29 moles, 1 missed (a mole left untapped on purpose), 5 wrong (on purpose), 481 ms, 3 stars. Pico: 29 moles, 33 tap groups, 33 FEEDBACK. `faults=none`, no crash words, both tiles ticked throughout. `round13_*.log` |

Found on the bench:
- The tiles' `DE ticks` counters rose while the network was being changed, not during any run: `missed=102971` and `100167`, and `jumps` 42 and 44 at the tap-test attempt, 47 and 49 before round 13, flat through round 13. Most likely the Pico's restarts and rejoins re-seeding the shared clock (the clock-jump guard's case); the 5 extra jumps in the idle gap before round 13 are not explained.

## Result (Matrix)

**The wall runs a full round on the Matrix router 12 Nov (round 13).** Next: 4e (website).

# 9-tile bench, 12 Nov 2026 night: all nine tiles on Matrix

The 4 new tiles arrived, making 9. Every tile was flashed with the same Matrix build (code of `2593a48`) over USB with esptool, one port at a time; each board's number was read from its NVS (0x9000) first. Wall: 3x3 in reading order (1 2 3 / 4 5 6 / 7 8 9). Addresses and MACs in `HANDOFF_12nov_night.md` section 6.

| Check | Result | Notes |
|---|---|---|
| Flash and number tiles 1 to 9 | **PASS** | Tiles 1, 2, 3 already held numbers 1, 2, 3. Tiles 6, 7, 8, 9 held **no number** (boot: `ESP-0, node 0 (no board number: run set_board_number.sh)`), so they never registered; an NVS image with `board:number` was written at 0x9000 for each, as `set_board_number.sh` does. Every write hash-verified. Tile 5 was reflashed twice with the same image by a faulty flash script (fixed); no effect. Pico `online=0x1ff sync=0x1ff`. |
| V2 video, 3x3, 9 tiles (session 29) | **Not a test: Pico receive buffer** | All 9 uploads 57 of 57 (2.20 to 3.42 s each, about 23 s one after another; 3 resent on tile 1). The Pico logged `CRC_MISMATCH` for node 8 (0xd6ba339a) and node 9 (0x0c609d07), both the CRCs the PC built, and stayed at `ready=0x07f`; the PC refused START ("not ready [8, 9]"). Cause: `CL_RX_MAX_BYTES` is 48, SESSION_CONFIG is 53 B since 4d, and the decoder reads the last 5 bytes from past the buffer (`HANDOFF_12nov_night.md` section 3). `v2_9tiles_*.log` |
| Round 14 game, 3x3, 9 tiles (session 30) | **PASS** | `game_round.py 192.168.1.154 30 - 1,2,3,4,5,6,7,8,9 --layout 3x3 --no-video --level easy --threshold 300 --tap-test 20`. Operator: looked right (start arrow on tile 5, countdown and results big across the wall). 27 of 28 moles, 1 missed, 9 wrong, 681 ms, 3 stars. Moles on every tile (2 to 4 each). Pico: 58 taps in 36 judged groups (27 hits, 9 wrong), 36 FEEDBACK. `faults=none` on all 9, no crash words. `round14_9tiles_*.log` (Pico and tiles 4, 5, 9 on USB) |

Found on the bench:
- The Pico receive-buffer bug above. Game rounds are not affected (CRCs unused, cells inside the first 48 bytes); every 9-tile video is.
- During round 14 the logged tiles' `DE ticks` counters grew while the round ran: `missed` by about 1,200 and `jumps` by 3 each (tile 4 105,579 to 106,756; tile 5 7,157 to 8,370; tile 9 2 to 1,154). On 2 tiles they stayed flat. Cause not established.
- The dim blue LED 0 (bottom right) on tiles 6 to 9 was the designed "no asset" status; it went off after the V2 upload (operator).

Open:
- Fix the receive buffer, then rerun V2 and a 9-tile game regression.
- The display misses with 9 tiles.

## Result (9 tiles)

**All nine tiles run a game round on the Matrix router 12 Nov (round 14).** The 9-tile video waits for the Pico receive-buffer fix.

# Pico receive buffer and time sync on Matrix, 12 Nov 2026 late (9 tiles)

Main laptop (Linux). The laptop stays on its WiFi for the agent; a USB Ethernet adapter on a LAN port of the Matrix router reaches the wall (`ipv4.never-default` on the wired profile, so the two networks are not bridged). Both `wifi_secrets.h` files name Matrix. 9 tiles, 3x3 in reading order (map `1,2,3,4,5,6,7,8,9`), Pico 192.168.1.154.

Built:
- **Pico receive buffer** (the bug found by run V2, `HANDOFF_12nov_night.md` section 3): `CL_RX_MAX_BYTES` is now `PROTO_SESSION_CONFIG_BYTES` (53 B, was a fixed 48 B sized for the 46 B message before step 4d); a compile-time check that every type the Pico decodes fits; `cl_enqueue()` drops and counts (`rx_bad`) a datagram longer than the buffer instead of letting the decoder read past the copy. Host test **C31**: a 9-tile SESSION_CONFIG through the receive path, every tile then counts ready with its own CRC32, and a datagram one byte too long is dropped. C31 failed on the old code (readiness check) and passes with the fix.
- **Time sync, re-seed only after 3 off samples in a row** (decided with Cheston, option A): one or two beacons more than 0.5 s off the estimate are dropped (`TS dropped` log line, `timesync_dropped_count()`); the third in a row re-seeds, so a real Pico restart is still caught about 200 ms later. Host test extended: with the old one-sample rule the new late-beacon checks fail (3 re-seeds, 0 dropped); with the fix they pass and the "another Pico" case re-seeds on the third beacon. Owner: Ahsanul (time sync) and Benjamin (coordinator) defend these.
- Gate passes; Pico and tiles built with exit 0; every tile flashed one at a time directly on USB (a hub with 3 tiles and the Ethernet adapter cut out twice and once stayed dead; see "Found").

| Check | Result | Notes |
|---|---|---|
| Laptop on Matrix by cable | **PASS** after two fixes | The adapter's name follows the USB port (`enp0s13f0u3u2`, later `enp0s13f0u2u2`); the firewall rule allowing the boards' UDP is per interface name, so after a port change packets arrived (90 in 5 s) but none reached the scripts until a rule for the new name was added. Tiles 1 to 3 were silent until reset: their boot logs showed a normal Matrix join afterwards (stuck from an earlier power-up). |
| V2b video, 3x3, 9 tiles (session 31), Pico with the buffer fix | **PASS** | All 9 uploads 57 of 57, resent 0, 2.09 to 3.24 s each (about 22 s one after another). No `CRC_MISMATCH`; `ready=0x1ff` (was `0x07f` in V2); START accepted; COORDINATING PLAYBACK then READY about 15 s later. Operator: each tile showed its own cell number, the counter in sync on all nine, all blank after STOP. `faults=none` on all nine before and after. `v2b_9tiles_*.log` |
| Round 15 game, 3x3 (session 32) | **Not a test: refused to arm** | Eight tiles reported `MISSED_DEADLINE`. Cause: on Matrix some beacons arrived up to 514 ms late; time sync took each one as another Pico clock and re-seeded (tile 5: 21 re-seeds in 200 s, `jumps` 5 to 106, `missed` 44,227 to 218,060, each step a burst of missed ticks). On the hotspot the worst beacon was 360 ms late. Fixed by the time-sync change above. `round15_9tiles_*.log` |
| 2-minute check after flashing all 9 | **PASS (not exercised)** | Tile 9: no late beacon in 2 minutes, so the new rule was not exercised; 0 re-seeds, 0 drops, 0 missed. `soak_timesync_*.log` |
| Round 15b game, 3x3 (session 33), all 9 on the time-sync build | **PASS**, with a timing finding | Arrow on tile 5, countdown and results big across the wall (operator). 19 of 22 moles, 3 missed, 7 wrong (the operator's own), 690 ms, 3 stars; `faults=none` on all nine before and after; tile 5: 0 re-seeds, 0 drops, 0 missed ticks, 0 jumps. **Tap arrival latency** (the Pico's `GAME_TAP` value, arrival minus tap time): tiles 1, 2, 3, 5, 9 15 to 45 ms; **tiles 4, 6, 7, 8 730 to 920 ms**, shrinking during the round. Operator: those four were slow, then came back on track by themselves. In round 14 (old time sync) the same tiles were 11 to 160 ms. `round15b_9tiles_*.log` |

Time sync on Matrix, simulated on the host (`tools/timesync_congestion_sim.c`): a 6 s congestion period in which the beacon delay ramps up to a peak and back, on the committed code, the one-sample rule and the new three-sample rule.

| Peak delay | Worst clock error | Time more than 50 ms off | Re-seeds |
|---|---|---|---|
| 400 ms | 315 ms | 7.8 s | 0 |
| 600 ms | 456 ms | 17.5 s | 0 |
| 900 ms | 658 ms | 37.6 s | 0 |

The same for all three builds. The estimate is the smallest offset of the last 16 beacons (1.6 s), so a congestion period long enough to make every beacon in the window late moves the estimate with it; the published offset follows at the slew limit (100 us per tick, 6 ms/s) and takes minutes to come back. A late start (the first 20 beacons 850 ms late) recovers in about 0.3 s with the new rule. So the slow tiles in round 15b match congestion on the router, not the new rule; this is a model, not a measurement on those tiles (the operator chose not to read tile 8's log).

Found on the bench:
- The Pico receive buffer (fixed, C31).
- Re-seed storms from single late beacons on Matrix (fixed, the 3-in-a-row rule).
- A USB hub carrying 3 tiles and the Ethernet adapter cut out twice (kernel: the hub and everything behind it disconnected at once); after the second time no port saw it, and tiles plugged directly into the laptop worked. Flash tiles one at a time on a laptop port; keep the adapter on its own port.
- Event fault bits (`MISSED_DEADLINE`, `QUEUE_OVERFLOW`) stay set until the PC acknowledges a TELEMETRY; after reflashing, acknowledge for a few seconds (as the dashboard does) before reading faults.

Open:
- **Time sync follows sustained network delay** (the simulation above): tiles can be hundreds of ms off for tens of seconds after congestion. It hurts tap grouping (an echo with a wrong time stamp is judged as a separate wrong tap), reaction times and video sync. Design decision for the user (Ahsanul's module): a longer window, or hold the estimate when the window minimum suddenly rises.
- The bench script refuses to arm on event faults, not only on condition faults (proposed to the user, not decided).

## Result (receive buffer and time sync)

**The 9-tile wall plays video and a full game round on Matrix 12 Nov (V2b, round 15b).** The Pico receive-buffer bug from step 4d is fixed, and single late beacons no longer re-seed time sync. Time sync under sustained congestion is the open item.
