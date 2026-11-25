# Session handoff, 24 Nov 2026 (old router, freeze verdict, echo fix, CI green, requirements check)

Written by the orchestrating agent for itself (or any agent) starting cold. **This file supersedes `HANDOFF_19nov.md` for "where things stand".** Bench setup and method: `HANDOFF_18nov.md` sections 2 and 4, with the changes in section 2 below.

Read before acting, in this order:
1. This file.
2. `docs/final/requirements_check_24nov.md`: what is met, what is not, and the NR-03 decision.
3. `docs/bench/24nov/freeze_old_router.md`: the day's bench record (freeze runs, real rounds, both fixes).

## 1. Where things stand (updated 24 Nov, 17:20)

| Item | State |
|---|---|
| Branch | `feature/whack-a-mole`, pushed (head after `ac53533`). CI green from `263d5c4`. |
| Pico freezes | Old router for the demo. One freeze in the evening, during a re-upload after a wall turn back; 30 plain re-uploads and 10 turn re-uploads afterwards had none. The watchdog recovers it. |
| Echo grouping | Fixed (`d7399f4`, collect wait 150 ms); NR-03 restated to under 300 ms (user's decision). |
| **Whole-wall rotation (D-17)** | **Built and passing on the wall, video mode only.** The Pico listens to the tiles' TELEMETRY (port 4215), decides a wall turn (`wall_turn.c`), stops and saves the frame, reports STATUS `wall_turn`; the website re-cuts and re-uploads; the Pico resumes at the saved frame. NR-08: 10 of 10 board turns (detection at most 1.8 s, playing again at most 7.9 s), 0 of 10 single-tile turns. E7 (rotation in a round) deferred. |
| Robustness fixes today | Website resends the mode once a restarted Pico is READY (`314f51e`); tiles stop playing when the Pico restarts (`6b1e446`); failed uploads retried once the tile rejoins (`a18bce7`); Pico UDP pcbs raised to 6 (`8988105`); two wall-turn fixes (`f9d313c`). |
| Gate | All host suites plain and ASan (coordinator 43 of 43, playback 7 of 7), barr_check 0 findings on 104 files, pytest 511, both firmwares build. |

### Boards on the bench

| Board | Firmware | Notes |
|---|---|---|
| Pico `…E66430A64B595138` (MAC `2c:cf:67:d7:eb:c3`) | `f9d313c` | 192.168.1.154, on the laptop's USB for logs |
| Tiles 1 to 9 | `6b1e446` | Power bank; tiles 8 and 9 share a power run, and 3 and 6 dropped together once |

## 2. Bench setup changes

- **Router: the old Matrix router** (BSSID `88:d7:f6:be:85:70`), SSID changed to `MATRIX` so the firmware needs no change, fixed channel 11. Move away from busy WiFi: in a crowded spot pings were 40 to 190 ms and uploads 27 to 39 s per tile; in a quieter spot 15 to 110 ms and 4 to 20 s.
- Laptop wired adapter is now `enp0s13f0u3u2` (.228).
- Website start unchanged: `.venv/bin/python pc/web.py --pico 192.168.1.154 --layout 3x3 --gif numbered_cells_3x3.gif`. A restarted website writes a new tap log (`pc/logs/taps_<start>.csv`).
- Tiles 3 and 6 dropped off together at 15:13 and came back only on a replug: check their power feed before the demo.

## 3. Next, in order

1. **Measure video sync skew (NR-07)**: by slow-motion camera, see section 5. Wiring `pico_tap` to the tiles' GPIO4 is impossible (no header or pad on the tiles, soldering not allowed). Never measured; it is the brief's core claim.
2. **Week 13 change-requirement evidence**: game rows and the new STATUS `wall_turn` in `docs/week13/data_representation.md`, re-measure the "after" sizes on head, fill the `[NEEDS MEASUREMENT]` cells, run or explicitly drop the five hardware tests (`requirements_check_24nov.md` section 4.2).
3. **Defence docs** (NR-14): each buddy's game part, now including `wall_turn` (Benjamin), the TELEMETRY-on-turn and restart stop (Cheston, Ahsanul for playback), the website turn and retry (Jolyn).
4. Smaller: the Pico's `stale` counter after restarts (not checked); 10 rounds in a row for NR-06; time A6, A7, A8; whether turns while paused behave (built, not wall-tested).

## 4. Method notes from today

- **After any `tap_burst.py` run, restart the Pico** before a real round: the fake TAPs push the Pico's per-tile TAP seq past the tiles' own, and real taps from tiles 1 to 8 are dropped as stale until a restart. In the same session, give the tool a FIRST_SEQ past the last run's (6th argument).
- The Pico's USB log keeps nothing from before the port is opened: start the capture before a reboot to see boot events. Use plain `cat`; a `stty` left in D state held the port once (kill -9 it).
- `docs/bench/24nov/tools/echo_sim.py PICO_LOG TAPS_CSV` replays the Pico's grouping on a round's real arrival times; it matched the Pico's judgement counts within 2.
- Barr output: read all of it, not `tail -1`; several findings hid behind the last line today.
- `docs/bench/24nov/tools/upload_soak.py RUNS LOG` re-uploads the whole wall from the website and counts freezes.
- The Pico prints `tel_rx`, `wt_known`, `wall_turn` in CL_STATS: `tel_rx` must rise about 9 per second, `wt_known=0x1ff` for a 9-tile wall.
- After any layout change by hand, check the website's map against the tiles' orientations before a turn test.
- Host tests inject datagrams without opening sockets, so a port that fails to open (lwIP pcb limit) passes them; read the Pico's boot lines after adding a port.
- A restarted Pico passes SYNCHRONISING, where it refuses a mode change but ACKs it.

## 5. Where the session stopped (24 Nov, about 17:40): NR-07 by camera, in progress

**Read this section first if you take over now.** Everything above is done and pushed. This part is half done.

### 5.1 Why a camera

NR-07 (video sync skew under 5 ms) has never been measured. The planned method (`tools/pico_tap` Pico recording each tile's GPIO4 tick pulse, `pc/tools/pico_tap_capture.py` + `pc/tools/skew_analyse.py`, see `tools/pico_tap/README.md` and `docs/week10/skew_2tile/RIG.md`) is **impossible: the Waveshare tiles have no header or pad for GPIO4 and soldering is not allowed** (user, 24 Nov). The `pico_tap` firmware is built (`tools/pico_tap/build/pico_tap.uf2`) but not needed now.

Replacement (agreed with the user): a **slow-motion phone video** of two tiles playing a counter GIF. The phone is a teammate's **iPhone 17 Pro Max** (true 240 fps slow motion at 1080p). The file comes to the laptop by **LocalSend**. The teammate holds the phone (asked to brace it).

### 5.2 Tools (in `docs/bench/24nov/tools/`, committed)

| File | What it does |
|---|---|
| `sync_gif.py ROWS COLS OUT.gif` | Writes the counter GIF: every 8x8 cell shows the same 6-bit Gray-code counter, one step every 20 ms (500 frames, 10 s, 50 steps/s; exactly one 2x2 block changes per step). Layout of the 4x4 block grid: row 0 = anchor, bit0, bit1, anchor; row 1 = bit2 to bit5; row 2 = dark; row 3 = all anchors. Anchors give each video frame the tile outline (hand-held drift) and the lit level; row 2 the dark level. Made: `pc/gifs/sync_3x3.gif` (gitignored folder: regenerate with `.venv/bin/python -I docs/bench/24nov/tools/sync_gif.py 3 3 pc/gifs/sync_3x3.gif`). Checked: the website pipeline (`asset_builder.load_gif`, `resample_60fps`) keeps the pattern pixel-exact and steps on 5 of every 6 ticks. |
| `sync_video.py VIDEO --preview p.png` | Writes the brightest pixels of the first second at 480 px wide, to pick each tile's box (look at the PNG). |
| `sync_video.py VIDEO --box-a X,Y,W,H --box-b X,Y,W,H [--fps F]` | Reads both tiles in every frame (outline from lit pixels inside the box, 4x4 block levels normalised between the dark row and the anchors), times every counter step of each tile to a fraction of a frame (straddling frame: change at f + 1 - b when a block brightens to level b, f + b when it darkens; exposure taken as the whole frame period), pairs A and B steps by counter value, prints median, mean, p95, max of B - A and the NR-07 verdict. Also prints the frame rate implied by the counter (50 steps/s), as a check on the slow-motion file's timing. |
| `synth_sync_video.py SKEW_MS SECONDS OUT.mp4 [FPS]` | Renders a synthetic video of two tiles with a known skew (8 sub-samples per frame for the exposure, noise, slow drift), to test the reader. Slow: about 90 s for 4 s of video. |

### 5.3 Reader validation so far

- Synthetic 3.0 ms at 240 fps: first version gave 4.17 ms (whole-frame rounding: a slightly lit straddling frame read as clean "old value" and the search started after it). **Fixed** (search starts at the last old-value frame). Now **3.15 ms median, max 3.34 ms** (error about 0.15 ms). Boxes used: A `50,70,115,115`, B `190,70,115,115`.
- **Not yet run** (interrupted): synthetic 0 ms, -7 ms (B ahead by more than a frame) and 12 ms (must FAIL) at 239.7 fps (camera phase drifting against the 60 Hz ticks, like a real phone). Command:
  ```
  S=<scratch dir>; T=docs/bench/24nov/tools
  for k in 0 -7 12; do .venv/bin/python -I $T/synth_sync_video.py $k 4 $S/synth_$k.mp4 239.7; done
  for k in 0 -7 12; do .venv/bin/python -I $T/sync_video.py $S/synth_$k.mp4 --box-a 50,70,115,115 --box-b 190,70,115,115 --fps 239.7 | tail -5; done
  ```
  Expect about 0, -7 and 12 ms (12 ms: FAIL). Fix the reader if not, before any real video.
- Known limits to state in the report: the camera measures light, which includes the LEDs' 2 ms WS2812 update (the same for both tiles); tiles must be on the **same row** of the picture (the iPhone sensor reads row by row, so a vertical offset between tiles would add a rolling-shutter delay); a whole-frame slip (16.7 ms) shows as a skew of about 16.7 ms (pairing is by counter value within 0.6 s).

### 5.4 Next steps, in order

1. Run the three synthetic checks above.
2. Website: upload `sync_3x3.gif` (website GIF library, or POST `/api/gif {"name": "sync_3x3.gif"}` after copying it to `pc/gifs`), wait for READY, play (`/api/video/play`). Video mode, no turns during the recording.
3. Recording (user's teammate): two tiles **side by side in the same row** (for example 4 and 5), filling most of the picture, phone braced, a fairly dark room, **240 fps slow motion, 10 s or more**. Send the **original** file by LocalSend (not a re-edited export). Ask for several recordings if possible: tiles 4-5, 7-8, and a far pair on one row (1-3).
4. `ffprobe` the file (check `r_frame_rate`, `avg_frame_rate`, `nb_frames`; iPhone slow motion may carry a time remap), `--preview`, look at the PNG, pick boxes, run `sync_video.py`. Use `--fps 240` if the stream says otherwise; the "implied frame rate" line must come out near 240.
5. **NR-03 by camera (tap to flash, target under 300 ms, user decision 24 Nov):** in game mode, film one tile in slow motion while tapping moles; count frames from the finger's contact to the green burst (by eye or a small script). Write both results into `docs/bench/24nov/freeze_old_router.md` (or a new `docs/bench/25nov/` record) and into `docs/final/requirements_check_24nov.md` (NR-07, NR-03 rows).

### 5.5 State of the bench when the session stopped

- Website running: `.venv/bin/python pc/web.py --pico 192.168.1.154 --layout 3x3 --gif frog_ripple_64x64.gif` (log `docs/bench/24nov/web5.log`), map upright 1..9, all 9 tiles ready, video was playing after NR-08.
- Background captures may still be writing `docs/bench/24nov/pico_nr08.log` and `nr08_monitor.log` (they stop by themselves about 17:50). **Do not commit their later growth** (`git checkout --` them), as done for the other logs today.
- Tile 5 and tile 3 were also seen plugged into the laptop's USB earlier; check the board before tests.
- Untracked and not to commit: `docs/bench/24nov/web2.log` to `web5.log`, `docs/bench/19nov/web_end.log` change.

### 5.6 The full test method used today (follow it)

- **One hardware step per user request** ("flash the tiles connected now": `bash docs/bench/12nov/tools/flash_tiles.sh <done tile numbers>` each time, report flash exit and the `DE ticks` line; a tile whose serial line is empty is judged by website TELEMETRY instead). No automatic watchers on hardware. Start rounds and turn tests only when the user says "go".
- **Before a wall test:** website READY, `readiness` 0x1FF, faults none, Pico CL_STATS (`tel_rx` rising about 9/s, `wt_known=0x1ff`, `wall_turn=0`, `wd_boot`), and the website's `node_map` agrees with the tiles' orientations (upright: 1 1 2 0 1 2 1 3 2).
- **During a wall test:** Pico serial capture (`nohup bash -c "... cat /dev/ttyACM0 >> LOG"` loop that reopens after a reboot) plus a website state monitor (Python polling `/api/state` every 0.1 s, logging only changes) into `docs/bench/24nov/*_monitor.log`. Analyse from those logs (timelines, WALL_TURN events, STATUS gaps), not from memory.
- **Every fix:** host test first that fails on the old code (mutation check: revert or break the fix, confirm the test fails, restore), then the gate: `clang-format` 18.1.8 from `.venv/bin/clang-format` on `tools/barr_scope.txt` (the system clang-format 23 differs), `tools/barr_check.py` (read every line), `make` and `make asan` in `panel_sync/test`, `common/proto/test`, `ledwall_sync/test`, `pytest tools/tests pc/tests`, both firmware builds (`idf.py build` in `panel_sync` after `. ~/esp/esp-idf/export.sh`; `cmake --build ledwall_sync/build`). Add new C files to `tools/barr_scope.txt` (missed once today). Then commit, flash, wall test, record in the bench file, commit, push.
- **Records:** every result with times, numbers and log file names goes into `docs/bench/24nov/freeze_old_router.md`; requirement verdicts into `docs/final/requirements_check_24nov.md`.
