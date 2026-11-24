# Freeze reproducer on the old Matrix router, 24 Nov 2026

Follow-up to `docs/bench/19nov/tap_test.md` ("Next, when the old router is back").

Setup: old Matrix router (BSSID `88:d7:f6:be:85:70`, same as the 9 Nov boot logs), SSID changed to `MATRIX`, fixed channel 11. Tiles 1 to 9 on a power bank, off the laptop. Pico `…E66430A64B595138` (MAC `2c:cf:67:d7:eb:c3`) at 192.168.1.154, not on USB, firmware not reflashed (last known: the 19 Nov watchdog build, maybe without breadcrumbs `5690403`/`0007a53` and paced sends `77caae7`). Website `pc/web.py --pico 192.168.1.154 --layout 3x3 --gif numbered_cells_3x3.gif` (log `web.log`).

Link quality: 0% ping loss but slow and uneven, averaging 25 to 460 ms per tile (tiles 2 and 9 worst, up to 0.9 s) even on fixed channel 11. On the Asus with all settings tuned it was 10 to 15 ms. Right after the channel change the Pico restarted, and all 9 GIF uploads failed (0 to 40 of 57 chunks after 20 rounds); a re-upload a minute later passed (5 to 17.6 s per tile).

## Run 1 (13:55:52, session 141)

`.venv/bin/python docs/bench/19nov/tools/tap_burst.py 192.168.1.154 141 8 0.7 360` with game mode and the tap test on (`tap_burst_run1.log`).

**Froze after 127 bursts (88 s).** STATUS stopped at about 13:57:17; the website logged "Pico back: publishing again" and the Pico came back SYNCHRONISING about 16 s later (the watchdog restart pattern). No breadcrumb read (Pico not on USB).

Finding: **the freeze reproduces on the old router**, faster than on the Asus (88 s against about 2.5 min). The Asus router is not the cause. Caveats: Pico firmware not confirmed at head, and the radio link is noisier than on the tuned Asus.

## Pico reflashed, moved to a quieter spot

14:00: Pico `…595138` flashed at head (`picotool load -x -f`; before, `picotool info` showed a 5 Nov build date). CL_STATS now has `wd_prev` and `tx_drop`. Upload after the flash took 27 to 39 s per tile (link still congested).

The user then moved the bench somewhere with less WiFi traffic. While the router was off, the Pico froze while joining it and the watchdog restarted it twice (`wd_boot=1 wd_step=0x0b wd_prev=0x0b`, step 11 = connect), as on 19 Nov. After the move, pings averaged 15 ms to the Pico and 30 to 110 ms to the tiles, no loss.

## Run 2 (14:28:12, session 244)

Same reproducer, 360 s, Pico serial logged (`pico_run2.log`, `tap_burst_run2.log`).

**No freeze: 514 bursts in 6 min** (about 4,100 TAPs). STATUS age stayed at or under 0.3 s throughout; one serial session the whole time (no USB reconnect); the last CL_STATS shows `send_fail=0 tx_drop=0 tx_fail=0 rx_dropped=6`, all 9 tiles online, ready and in sync.

Finding: firmware at head + old router + quieter spot ran the full 6 min that froze the Asus bench in about 2.5 min. Two things changed since run 1 (firmware and location), so run 1 cannot be pinned on either alone. On 19 Nov the same head firmware froze on the Asus, so the router (or its radio surroundings) matters.

## Run 3 (14:37:08, session 244): invalid

Same command as run 2, same session. CL_STATS `stale` went 23 to 4,099: the Pico dropped nearly every TAP because `tap_burst.py` always numbered from 1001 and run 2 had already used 1001 to 1514 in this session (TAP seq must be newer per tile, `cl_seq_newer`). No freeze, but there was almost no TAP load. Fix: `tap_burst.py` takes an optional FIRST_SEQ (6th argument) and prints the last seq it sent.

## Run 4 (14:43:42, session 244)

`.venv/bin/python docs/bench/19nov/tools/tap_burst.py 192.168.1.154 244 8 0.7 360 3000` (`tap_burst_run4.log`, `pico_run4.log`).

**No freeze: 511 bursts in 6 min, seq 3000 to 3511.** `stale` rose only 43 (4,123 to 4,166), so the TAPs were taken. One serial session (no watchdog restart), `send_fail=0 tx_drop=0 tx_fail=0`, `rx_dropped` 6 to 7. Tiles 2 and 5 dropped off the Pico's online mask briefly 5 times (14:45 to 14:49) and came back; at the end this made the website refuse the tap-test-off once (409, wall not READY), and a retry worked.

**Verdict (runs 2 and 4): head firmware on the old router in a quiet spot passes the reproducer twice for 6 min**, where the Asus froze in about 2.5 min. Use the old router for the demo, away from busy WiFi.

## Real rounds with hard taps (14:59 to 15:03, session 212)

The user's 14:54 Pico restart (a replug; `wd_boot=0`) also cleared the TAP counters the reproducer had pushed to seq 3511 for tiles 1 to 8: until then only tile 9's real taps were taken, so the start arrow could not be tapped. **After any tap_burst run, restart the Pico before a real round.**

Three rounds, no freeze or restart (CL_STATS uptime matched the 14:54 boot, `send_fail=0 tx_drop=0 tx_fail=0`), 9 tiles online, no faults:

| Round | Level | Score | Moles | Wrong | Stars |
|---|---|---|---|---|---|
| 1 | easy | 21 | 21 of 25 | 14 | 3 |
| 2 | hard | 23 | 23 of 33 | 20 | 2 |
| 3 | hard | 25 | 25 of 34 | 18 | 2 |

Tap log: 873 reports, 246 physical taps (grouped by `t_tap_us` within 30 ms); 34 taps of 13 to 21 g, 39 felt by 8 or 9 tiles. During play 75 of 119 taps had an echo reaching the PC over 20 ms after the first report (an upper bound for the Pico, one hop earlier): the high wrong counts are most likely echoes split from their tap (open item: group by tile time stamp on the Pico). `stale=928` on the Pico since 14:54, cause not checked.

## Website: mode lost after a Pico restart (fixed)

After the 14:54 restart every ARM was refused (`GAME_REFUSED value=7`): the Pico boots in video mode; the website resent its mode right when STATUS came back, at 5.9 s, while the Pico was SYNCHRONISING (state 6). There it refuses a mode change (`cl_stopped()` leaves SYNCHRONISING out) but ACKs it, so the website believed it. Workaround then: Video, then Game.

Fix (`pc/dashboard.py`): after "Pico back" the mode is marked unsent and sent at the first fresh STATUS showing READY; a user's own mode switch clears the mark. Host tests `test_a_pico_back_gets_the_mode_only_once_ready` and `test_a_mode_switch_after_a_pico_restart_is_not_sent_twice` (both fail on the old code); pytest 505 passed.

Wall check (15:12:21, website in game mode, `picotool reboot -f`, serial from boot in `pico_restart_fix.log`): READY at t=14.1 s, no GAME_REFUSED, ARM accepted (state 12 at t=24.7 s). Also passed once at 15:10:34 (ARM accepted).

## Echo grouping (15:15 to 15:28)

Two rounds with the Pico logged (`pico_round_echo.log`; round 1 at 300 mg: 20 hits, 38 wrong; round 2 hard at 620 mg: 16 of 29, 19 wrong). Tiles 3 and 6 dropped off together at 15:13 and came back on a replug (likely their power feed). Analysis joins the Pico's GAME_TAP events (arrival, delivery time) with the website tap log's peaks (`tools/echo_sim.py`):

- Tile stamps of one tap are tight (90% within about 20 ms): the 30 ms dedupe window is right.
- Delivery to the Pico: median 50 to 64 ms, 95th percentile 285 to 385 ms, worst 1 s.
- Two failure modes: the strongest TAP arrived after the 20 ms collect wait, so the group was judged on an echo (a wrong tap, and the real one dropped as a duplicate); and an echo arriving after the next group closed was judged as a tap of its own (the Pico remembered one closed group).
- A replay of the Pico's rules matched its judgement counts (59 against 58, 37 against 35). On 84 taps: today's rules judged 36 on an echo plus 12 extra; remembering 8 groups removes the extras; a 150 ms collect leaves 5.

Fix (user's choice of 150 ms; `game_rules.h`/`.c`): `GAME_COLLECT_MS` 20 to 150, the last `GAME_WINDOWS` = 8 closed groups checked for late duplicates, `GAME_TAP_SLOTS` 16 to 32. After a hit the next mole shows about 50 ms later (MOLE_ON keeps its 200 ms lead after the later verdict). Host test C18d (fails on the old rules); C18 and C27 now state their waits by the constants. Host tests 39 of 39 (also ASan), barr_check 0 findings, firmware builds.

Wall round (15:28, hard, 620 mg, `pico_round_fix.log`): **34 of 34 moles, 0 wrong, 0 missed, 3 stars.** On the same data the old rules would have judged 8 of 35 taps on an echo. The radio was calmer than at 15:20 (95th percentile delivery 160 ms), so not all of the gain is the fix.

## Whole-wall rotation, first wall test (16:36, D-17)

Firmware: Pico at `690f2de` plus the UDP pcb fix (below); tiles 1 to 9 at `690f2de` (TELEMETRY at once on a turn). Website at `bf6affd`. 3x3, `frog_ripple_64x64.gif`, playing. Tile orientations upright: 1 1 2 0 1 2 1 3 2 (mount offsets differ). Logs `turn2_monitor.log` (website state every 0.1 s), `pico_turn2.log`.

First try (16:27, `turn1_monitor.log`): the tiles reported the turn, but the Pico did nothing: `COORDINATOR: cannot open port 4215`. lwIP's default `MEMP_NUM_UDP_PCB` 4 was used up by DHCP, DNS and the three existing pcbs. Fixed with `MEMP_NUM_UDP_PCB 6` in `lwipopts.h` and a compile-time check; CL_STATS now prints `tel_rx`, `wt_known` and `wall_turn`. After the fix `tel_rx` rose about 9 per second and `wt_known=0x1ff`. The Pico restarts for the flash left the tiles playing, and they refused uploads (BUSY) until the user power-cycled them: **a Pico restart during video leaves the wall stuck** (open bug).

Second try:

| Step | Time (s from the first tile's change) | Requirement |
|---|---|---|
| First tile reports the turn | 0 | |
| All 9 tiles report +1 | 0.3 | E3 window 1.5 s |
| Pico WALL_TURN 1, STOP, READY; website shows the turned map (7 4 1 / 8 5 2 / 9 6 3) | 1.8 | E5 correct map; E8 within 2 s: met |
| Re-cut, new session, 9 parallel uploads | 2.0 to 7.5 | |
| COORDINATING PLAYBACK again (Pico resumed at its saved frame) | 7.6 | E6 within 10 s: met |
| Turned back: WALL_TURN 3, map back to 1 2 3 / 4 5 6 / 7 8 9 | 1.7 after that turn | E3, E5 |

During the re-upload after the turn back, **the Pico froze** (website NO STATUS 16:36:33 to 16:36:59): `wd_boot=1 wd_step=0x0000000a wd_prev=0x80000002`, that is a freeze inside a UDP send in the beacon step, then a second watchdog reset while rejoining (step 10). The watchdog and the website recovered it (new session 206, all tiles ready), but the restart lost the pending resume, so the video stayed stopped until started again. The freeze is the same CYW43 hang as on the Asus, here under the burst of nine parallel uploads plus beacons.

## A Pico restart during video (16:52, tile fix `6b1e446`)

All 9 tiles reflashed with the fix (tile 1's display check printed nothing over serial, but its TELEMETRY was fresh with no faults). Before the test, the website's last upload had failed on 8 tiles as BUSY (taken while they still ran the old firmware) and was never retried, so the Pico waited in WAITING FOR TILE READINESS until the GIF was chosen again (open: retry failed uploads).

Video playing from 16:52:53; `picotool reboot -f` at 16:52:58 (`restart_stop_monitor.log`, `pico_restart_stop.log`). The website saw NO STATUS, then "Pico back: publishing again" 6 s later; **all 9 uploads succeeded, none BUSY** (3.0 to 4.5 s), READY with every tile ready at 16:53:10, 12 s after the restart, with no power cycle. Before the fix the same restart left the tiles playing and refusing uploads until power-cycled.

## Failed uploads retried (17:00, website `a18bce7`)

Tile 9 unplugged, the GIF chosen again at 16:57:12: 8 tiles uploaded, tile 9 failed ("no TELEMETRY yet, address unknown"), the Pico waited (WAITING FOR NODES). Replugging tile 9 knocked tile 8 off as well; the user reseated both (tiles 8 and 9 share a power run; keep it in mind for the demo). Tile 9 was heard again at 17:00:41; the website logged "retrying the upload to tile 9" 1.0 s later and the wall was READY with every tile ready 4.3 s after the tile rejoined, with nothing done on the dashboard (`retry_monitor.log`).

## Upload soak (17:01 to 17:05)

`.venv/bin/python -I docs/bench/24nov/tools/upload_soak.py 30 docs/bench/24nov/soak_w9.log`: the website re-uploads the whole 3x3 wall 30 times (9 tiles at once, video stopped). **0 freezes in 30**; READY 6 to 7 s after each start, slowest tile about 5.5 s, longest STATUS gap 0.3 s. Parallel uploads alone do not freeze the Pico, so pacing them is not justified by this. The 16:36 freeze came during the re-upload after a wall turn back (STOP x3, new session, 9 uploads); the NR-08 turn card will show whether turn re-uploads freeze more often.

## NR-08 part A: 10 whole-board turns (17:11 to 17:13, Pico `f9d313c`)

Before the run the website's map was found turned (7 4 1 / 8 5 2 / 9 6 3) with the wall upright, sometime after 16:55 (the Pico log had a gap there). Two Pico bugs could each cause it and both were fixed first (`f9d313c`, host tests C36 and C38): a wall turn moved only the online tiles' references (tiles 8 and 9 were offline during the retry test), and a new layout that happened to be a turn of the last one invented a pending turn. The website was restarted with the upright map.

Video playing; the user turned the board cw, cw, ccw, ccw, 180, 180, cw, ccw, 180, then back to upright, waiting for the video each time (`nr08_monitor.log`, `pico_nr08.log`):

| Turn | Pico WALL_TURN | Map after | Detected after the first tile change | Playing again after the first tile change |
|---|---|---|---|---|
| 1 cw | 1 | 7 4 1 / 8 5 2 / 9 6 3 | 1.7 s | 7.5 s |
| 2 cw | 1 | 9 8 7 / 6 5 4 / 3 2 1 | 1.7 s | 7.0 s |
| 3 ccw | 3 | 7 4 1 / 8 5 2 / 9 6 3 | 1.7 s | 7.5 s |
| 4 ccw | 3 | 1 2 3 / 4 5 6 / 7 8 9 | 1.7 s | 6.8 s |
| 5 180 | 2 | 9 8 7 / 6 5 4 / 3 2 1 | 1.5 s | 7.1 s |
| 6 180 | 2 | 1 2 3 / 4 5 6 / 7 8 9 | 1.7 s | 7.0 s |
| 7 cw | 1 | 7 4 1 / 8 5 2 / 9 6 3 | 1.8 s | 7.9 s |
| 8 ccw | 3 | 1 2 3 / 4 5 6 / 7 8 9 | 1.7 s | 6.7 s |
| 9 180 | 2 | 9 8 7 / 6 5 4 / 3 2 1 | 1.5 s | 7.9 s |
| 10 back | 2 | 1 2 3 / 4 5 6 / 7 8 9 | 1.6 s | 6.9 s |

**10 of 10 detected with the right turn**; detection at most 1.8 s (E8: 2 s), playing again at most 7.9 s, mean 7.2 s (E6: 10 s). **No Pico freeze** in these 10 turn re-uploads (`wd_boot=0`, no STATUS gap).

## NR-08 part B: 10 single-tile turns (17:15 to 17:16)

The user turned one tile a quarter and back, ten times: tiles 1, 6, 8, 3, 9, 1, 9, 2, 8, 6. The website saw each tile's orientation change and return 3 to 5 s later; **no WALL_TURN, no "wall turned" message, and the video played throughout** (COORDINATING PLAYBACK the whole time). **NR-08 passes: 10 of 10 wall turns, 0 of 10 single-tile turns read as a wall turn.**

## Camera reader validation (NR-07 by camera, synthetic videos)

`docs/bench/24nov/tools/sync_video.py` checked on synthetic videos of two tiles with a known skew (`synth_sync_video.py SKEW_MS 4 OUT.mp4 FPS`), boxes A `50,70,115,115`, B `190,70,115,115`, before any real video. The 3 ms row is from the first session (240 fps); the others were run at 239.7 fps (the camera phase drifting against the 60 Hz ticks), 4 s each, 199 or 200 paired steps.

| True skew | Measured median | Max step error | Reader verdict | Right? |
|---|---|---|---|---|
| 3.0 ms (240 fps) | 3.15 ms | max skew 3.34 ms | PASS | yes |
| 0 ms | 0.00 ms | 0.18 ms | PASS | yes |
| -7 ms (B ahead by more than a frame) | -6.86 ms | 0.48 ms | FAIL | yes |
| 12 ms | 12.02 ms | 0.58 ms | FAIL | yes |
| 20 ms (more than one 16.7 ms tick) | 19.86 ms | 0.46 ms | FAIL | yes, read as 20 ms, not 3.3 ms |

Every median is within 0.15 ms of the true skew, and no step is off by more than 0.58 ms (one frame is 4.17 ms). The counter's implied frame rate came out at 238.9 fps against 239.7: the counter steps on the 60 Hz tick grid, so the first and last steps of a 4 s clip can each shift by part of a 16.7 ms tick (up to about 0.4 %); a 10 s recording halves that. The reader is ready for a real video.

## NR-07 by camera: two recordings (18:11 and 18:12)

Wall: 3x3 upright, `sync_3x3.gif` in video mode (website session 135; READY, readiness 0x1FF, no faults, map 1..9, all 9 uploads first try). Recorded by a teammate on an iPhone 17 Pro Max in slow motion, hand-held, and sent as the phone's own files (HEVC 1920x1080, Apple tags). Tile A is the left tile in the picture, B the right one.

| Video | Tiles (A, B) | Stream | Frames, length |
|---|---|---|---|
| `IMG_3429.mov` | 4, 5 | 30 fps | 3532, 117.7 s |
| `IMG_3430.mov` | 7, 8 | 30 fps | 3638, 121.3 s |

The phone stored the slow motion in the frames: every frame is a 240 fps capture played at 30 fps, except for the normal-speed start and end. A first export of the same videos (848x464, re-encoded, no Apple tags) was dropped for the originals.

Method: each video cut into 10 s pieces (about 300 frames, 1.25 s of real time, about 63 counter steps); boxes per piece from the LED extent (pixels of 240 and up, every 10th frame) plus 8 px; `sync_video.py --width 960 --fps 240 --threshold 200`. **Threshold 200** instead of the default (half the brightest pixel, about 127): the cards behind the tiles reach about 156 and the LEDs 240 and up, so the default counts the card as LEDs. A piece is used only when its implied frame rate is within 2 % of 240.

Excluded pieces: 3429 at 0 s (189.3 fps, normal-speed start), 30 s (245.1), 110 s (183.8, normal-speed end); 3430 at 0 s (195.5, start), 40 s (246.0), 110 s (207.7, end), 120 s (146.1, only 38 frames). 245.1 and 246.0 are probably the 60 Hz tick rounding over a 1.25 s span (up to about 1.3 %), but they are outside the rule.

Usable pieces (skew B - A, ms; missed counter steps per tile):

| Video | Piece | Implied fps | Median | Max | Missed A / B | Pairs over 4 ms |
|---|---|---|---|---|---|---|
| 3429 | 10 s | 240.3 | -0.43 | 3.84 | 0 / 0 | 0 |
| 3429 | 20 s | 238.6 | -0.12 | 4.86 | 0 / 0 | 2 |
| 3429 | 40 s | 237.2 | -0.23 | 6.23 | 0 / 2 | 1 |
| 3429 | 50 s | 237.6 | -0.09 | 25.99 | 0 / 8 | 3 |
| 3429 | 60 s | 243.6 | +1.84 | 9.87 | 0 / 2 | 3 |
| 3429 | 70 s | 237.8 | 0.00 | 4.66 | 0 / 0 | 3 |
| 3429 | 80 s | 239.3 | -0.20 | 1.21 | 0 / 0 | 0 |
| 3429 | 90 s | 238.6 | -0.16 | 1.27 | 0 / 0 | 0 |
| 3429 | 100 s | 239.4 | -0.36 | 29.79 | 0 / 4 | 1 |
| 3430 | 10 s | 240.5 | +0.05 | 4.34 | 0 / 0 | 4 |
| 3430 | 20 s | 241.2 | +0.11 | 4.20 | 0 / 0 | 3 |
| 3430 | 30 s | 236.3 | +0.29 | 4.50 | 0 / 0 | 6 |
| 3430 | 50 s | 239.4 | -0.67 | 4.81 | 0 / 0 | 1 |
| 3430 | 60 s | 241.9 | -0.87 | 4.75 | 0 / 0 | 5 |
| 3430 | 70 s | 238.2 | -0.34 | 4.82 | 0 / 0 | 5 |
| 3430 | 80 s | 242.0 | -0.22 | 4.80 | 0 / 0 | 7 |
| 3430 | 90 s | 244.3 | -1.41 | 4.81 | 0 / 0 | 11 |
| 3430 | 100 s | 236.5 | -2.81 | 8.64 | 0 / 0 | 20 |

(3429 at 60 s: both tiles also jump 13 values at the GIF's loop point, since 500 frames is not a multiple of the 64-value counter; not counted as missed.)

Every pair over 4 ms was checked from both tiles' decoded values over frames f-3 to f+4; the 13 over 5 ms also by eye on crops of both tiles:

| Pairs | Measured | Classification |
|---|---|---|
| 3429: 40 s (value 1), 50 s (17, 25, 57), 60 s (25), 100 s (9) | -6.2, -26.0, -15.7, -8.9, -9.9, -29.8 ms | **Misread** (excluded): both tiles show the new value on the same frame, or B one frame later; B was blurred over several frames and its step was timed too early |
| 3430 at 100 s: values 19, 23, 27, 31, 35, 38, 39 | -5.0, -5.0, -5.5, -8.6, -8.2, -5.3, -7.6 ms | **Real**: B (tile 8) shows each new value one whole frame before A (tile 7); true skew 4.2 to 8.3 ms per step |
| The other 62 (both videos) | 4.00 to 4.86 ms | One-frame timing granularity: one tile has a part-changed frame, both reach the new value within one frame; under 5 ms |

Each step is timed to about one frame (4.17 ms), not the 0.15 ms of the synthetic check: the iPhone's exposure was much shorter than the frame period, so the reader's sub-frame interpolation does not apply. The medians (55 to 64 steps per piece) are the finer figure.

**NR-07: FAIL on tiles 7 and 8** (rule: every real step in the usable pieces under 5 ms; misreads shown by eye excluded; a one-tick slip of about 16.7 ms fails). Tiles 4 and 5: every real step under 5 ms, medians -0.4 to +1.8 ms. Tiles 7 and 8: no one-tick slip, but tile 8 drifts ahead of tile 7 at the end of the recording (median 0.0 to -0.9 ms over 10 to 80 s, then -1.4, -2.8, and -4.1 ms in the excluded end piece, about the last 2 s of 15 s real), with 7 real steps of 5.0 to 8.6 ms. Rolling shutter does not explain it: tile 8 is lower in the picture, so it is read later, which would show it later, not earlier. Next: look at the time sync between tiles 7 and 8 (offset and drift corrections around 18:12).
