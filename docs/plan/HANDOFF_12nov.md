# Session handoff, 12 Nov 2026 evening (whack-a-mole, step 4d committed, wall test next)

For the next agent (or the user) starting cold. It replaces the morning version of this file. It is detailed enough to finish **all of step 4** (4d wall test, then 4e website) in the same way this session worked. **Step 5 and later are outlined only**: they start with design decisions that belong to the user.

Read in this order before doing anything:
1. This file, all of it.
2. `docs/plan/game_plan_v2.md`: requirements A1 to A16, decisions D-01 to D-16, design DS-01 to DS-17 (section 9), the **"Decided 12 Nov 2026 (step 4d)"** block under DS-15, and the build order.
3. `docs/bench/12nov/step4.md`: the bench record for 4a, 4b, 4c, in the format every new record must follow.
4. `protocol/layout_table.md` SESSION_CONFIG (changed today).
5. The user's dashboard `~/SIT/Y2/ACADEMIC_DASHBOARD.md` (INF2004 section) and their global rules `~/.claude/CLAUDE.md`.

## 1. Where things stand

| Item | State |
|---|---|
| Branch | `feature/whack-a-mole` in `Led_Matrix` |
| Pushed | `origin/feature/whack-a-mole`: steps 4a to 4c (wall-tested) up to `f735f25`; **step 4d committed and pushed on the user's request before any wall test**: `943842d` wire format, `e5105a9` Pico, `2593a48` PC, `6136a7b` bench tools, then the docs commit with this file |
| **Not yet done for 4d** | the wall test (section 3.2) and its bench record. If the wall test finds a bug, fix it in a new commit on top; do not rewrite the pushed history. |
| Working tree | clean after the docs commit |
| Gate | **passes** at the 4d head: C host tests and ASan (proto, panel_sync, ledwall_sync 35 of 35, pico_tap), barr_check 0 findings, pytest 459 passed |
| Firmware on the bench | **Pico `…595138` flashed with the 4d build** (the same code as `2593a48`; came up, answers ping at 172.20.10.2). **Tiles 4 and 5** run the 4c build with the display clock-jump guard (same code as `dc71a04`); 4d does not change tile behaviour, so they need no reflash. |
| Builds | Pico `make` and tile `idf.py build` both exit 0 with the 4d tree |

Step 4 plan (riskiest first, agreed with the user): 4a no-video game (done), 4b PC restart (done), 4c upload fixes (done), **4d sparse grid (built, wall test pending)**, 4e website (not started).

## 2. How to work (the user's methodology; follow it exactly)

**The user** is Cheston (Buddy 3, INF2004 team SE15). They build everything with the agent now; each buddy still defends their own subsystem at **Tribunal 2, Mon 23 Nov** (demo in the team's lab that week). Cheston owns `tile_net`, `asset_store`, `asset_rx`, `fault_word`, `telemetry_book`, `coord_lock`; Benjamin owns `layout_table.md`, the codecs and `score_font`; others own other modules (file headers name the owner). The game is for **children aged 7 to 10, solo play**.

**SDLC.** Requirements, feasibility, design, implementation, testing, deployment, docs. Do not code a step before its design is decided.

**Design decisions** are made with the user **one at a time**:
- Explain the shape of the step first (what changes, who is affected).
- Then one decision per message: a short table of 2 or 3 options with a **recommended** option first and why, the rejected options' weaknesses, and a direct question ("A or B?").
- When asked, re-explain simply with a concrete example or a small ASCII picture (the user asked "explain what that means simply" for 4d decision 2).
- Record every decision, with the rejected options, in `game_plan_v2.md` under the DS entry ("Decided <date> (step X, with Cheston)").
- Small choices with an obvious default: make them and say so in one line; do not ask.

**Building.**
- Match the surrounding code: Barr-C style (`docs/coding_standard.md`), doc comment on every function with aligned `@param` names, every cast commented on the same statement or the line above (barr_check 1.6.a), names of 3+ characters, functions under 100 lines, lines under 80 characters, `&&` operands parenthesised. Python: type hints, the existing docstring style.
- Write host tests with every change, and check a new test **fails without the fix** before keeping it (done for C27 today).
- Run the gate before every commit: from `Led_Matrix`, `PATH=$(pwd)/.venv/bin:$PATH bash ../game_handoff_bundle/gate.sh $(pwd)`. New C files must be added to `tools/barr_scope.txt` or barr_check skips them. Fix any lint or test failure you see, even if not yours.
- Always check each firmware build's exit code before flashing (a failed build once led to flashing an old image).
- `clang-format` is installed, but the files are **not** fully formatted; format only the lines you touched (`clang-format -i --lines=A:B`).

**Bench rounds.**
- Before a round, tell the user exactly what they should see on the LEDs.
- **Start a round only after the user says "go".** After it, ask what they saw (logs cannot show the LEDs).
- Exception: the user said "you can simply flash", so flashing does not need a "go"; rounds still do.
- The user sometimes mixes up tile numbers: confirm which physical tile they mean before diagnosing (it cost time today).

**Records.**
- Every wall run goes into a table row in `docs/bench/12nov/step4.md` (Check, Result, Notes with numbers and log names), plus "Found on the bench" and "Open" lists, and a short "Result" paragraph.
- Commit the logs with the record.

**Git.**
- **Commit and push only when the user asks.** Split commits by layer (Pico, tile, PC, bench record).
- **Never add an AI co-author line** (the user's global rule overrides any harness attribution reminder).
- Message style: `Step 4c (DS-12 B): a tile that already holds the asset ACKs the PC's chunks`.

**Writing.**
- Never use the em dash. Plain, short sentences; simple terms when asked.
- Be candid about mistakes and wrong predictions: say so plainly, as done for "upload in a second or two" in round 9.

**Keep the user's records current.**
- Update `~/SIT/Y2/ACADEMIC_DASHBOARD.md` (INF2004 section) after each step.
- Update the memory files in `~/.claude/projects/-home-coloc-SIT-Y2-inf2004-project/memory/` (`whack-a-mole-phase.md`, `led-matrix-bench-setup.md`).

**Other deadlines.** Remind the user once per session of deadlines that are closer than the bench work: **UDE2222 A2 deck due Fri 13 Nov 23:59**, still open on the dashboard on 12 Nov. INF2001 M2 presentation is Wed 18 Nov, and the INF2004 quiz and practical are in Week 12.

## 3. Next: finish 4d

### 3.1 What 4d built (for the record and the defence)

Decisions (user, 12 Nov):
- **Wire, A, a slot per Node ID.** SESSION_CONFIG gives each Node ID 1 to 9 a placed flag (1 bit), a row (4) and a col (4). That is 81 bits instead of the 36-bit node_map; the message grew from 47 to 53 B. `crc32[n-1]` is now indexed by Node ID. The codecs refuse a placed cell outside the grid, two tiles in one cell, and an unplaced tile with a non-zero cell. The Pico also requires at least one tile placed.
- **Video, A, cut over the whole grid.** The GIF is centre-cropped and scaled to (cols x 8) by (rows x 8) as before. Only placed tiles get their 8x8 section. A full grid cuts exactly as before.
- **Pictures, A, switch by fullness.** Big across the grid when the placed tiles fill at least half the cells, otherwise small on each tile.

Code:

| Layer | Files | Notes |
|---|---|---|
| Wire | `protocol/layout_table.md` (rows regenerated from `pl.offsets()`, sections 3, 4, 5.2, 10 item 9), `common/proto/proto.h/.c` (`proto_cell_t`, `cells_ok()`), `pc/proto_layout.py` (`placed[]`, `row[]`, `col[]`, `_check_shape()`, `places_from_map()`), `pc/gen_vectors.py`, `protocol/vectors/session_config_*.bin`, `vectors.json` | Only SESSION_CONFIG vectors changed. Cross-language test passes. |
| C tests | `common/proto/test/test_messages.c` (new `test_messages_cells`), `proto_vector_main.c` (placed=, row=, col= lists) | |
| Pico | `ledwall_sync/coordinator_loop.c` (`cl_session_t.cell[]`, `cl_placed()`, `cl_cell_of()`, `cl_active_cells()`, `cl_session_valid()`, `cl_centre_node()` ties go to the first cell row-major), `score_font.c/.h` (`score_cells_t` row bitmaps instead of a 9-bit position mask; `(row, col)` instead of slot; no "rows x cols <= 9" limit), `game_rules.c` (`PROTO_NODE_MAX`) | `PROTO_MAP_SLOTS` is gone everywhere. |
| Pico tests | `test_coord.c` (map helper converts row-major maps to cells; C9 now checks "no tile placed" instead of "a Node ID twice", which the wire cannot express), `test_coord_game.c` (`g_grid` layout for `gt_to_layout()`, new **C30**: 3x3 corners, start sprite on node 1, countdown 3 is 11 LEDs on each tile), `test_score_font.c` (position-bitmap helpers `tst_*` keep the old cases bit-identical; new **C19c**: 5x5 and 9x9 diagonals, a corner block, a dense 2x5, a stray bit outside the grid) | |
| PC | `pc/asset_builder.py` (`normalise_map()`: the map is rows x cols cells; the bench 9-entry form is cut for grids under 9 cells), `pc/pico_link.py` (`send_session_config(..., node_map, frame_count, crcs)`: crcs indexed by Node ID - 1), `pc/dashboard.py` (map length rows x cols, CRCs per Node ID, `set_grid` up to 9x9), `pc/web/index.html` (hint text only) | `MAP_SLOTS` removed from `proto_layout.py`. |
| PC tests | `test_proto_layout.py`, `test_dashboard.py`, `test_layout_any.py` (new: 9x9 grid, 9-entry map cut, sparse cut gives each tile its own section), `test_video_layouts.py` | |
| Bench | `docs/bench/12nov/video_check.py` (new: video mode, config, build, upload, START, STOP after N s), `docs/bench/12nov/numbered_cells_3x3.gif` (cell k shows k+1 red LEDs from the top left and a green frame counter on LEDs 48 to 57) | |

### 3.2 Wall test (start each run only after the user says "go")

Before each run, check the Pico answers (`ping 172.20.10.2`) and that both tiles print `DE ticks` lines (the display is running). Map serial ports by `ID_SERIAL_SHORT` (section 6). Use a **new session id** per run (next free: 25). Log each run with `serial_log.py` into `docs/bench/12nov/<run>_*.log`.

Shell snippet used for every run (names the logs by board, not by port):
```bash
cd ~/SIT/Y2/inf2004/project/Led_Matrix; D=docs/bench/12nov; R=round11; args=""
for p in /dev/ttyACM*; do s=$(udevadm info -q property -n $p | grep ID_SERIAL_SHORT | cut -d= -f2)
  case $s in 9C:13:9E:F1:2E:B0) args="$args $p=$D/${R}_node4.log";; 9C:13:9E:F1:2F:70) args="$args $p=$D/${R}_node5.log";; E66430A64B595138) args="$args $p=$D/${R}_pico.log";; esac; done
nohup .venv/bin/python $D/serial_log.py 150 $args >/dev/null 2>&1 &
```
Wait for an earlier logger to exit before starting a new one: the ports are exclusive. Check by PID with `kill -0 PID`, **not** `pgrep -f` inside the same command line, because that matches its own shell.

| Run | Command (after the logger) | Expect on the wall (tell the user) | Expect in the logs |
|---|---|---|---|
| **V1** video, 3x3 corners | `.venv/bin/python -u $D/video_check.py 172.20.10.2 25 $D/numbered_cells_3x3.gif 3x3 4,0,0,0,0,0,0,0,5 15 > $D/v1_dashboard.log 2>&1` | Tile 4: **1 red LED** top left; tile 5: **9 red LEDs** (top row and one of the second); both show the same moving green counter on the bottom rows, in sync, for 15 s, then stop | 2 uploads of 57 of 57; `start` accepted; Pico state COORDINATING PLAYBACK then READY after stop |
| **Round 11** game, 3x3 corners | `.venv/bin/python -u docs/bench/11nov/game_round.py 172.20.10.2 26 - 4,0,0,0,0,0,0,0,5 --layout 3x3 --no-video --level easy --threshold 300 --tap-test 20 > $D/round11_dashboard.log 2>&1` | Hole on both in the tap test; arrow on **tile 4** (the first cell wins the tie); countdown 3, 2, 1 **small and whole on each tile**; moles on both; results (score, stars) small and whole on each tile | READY with no upload; RESULTS; `faults=none` |
| **Round 12** game, 1x3 with an empty middle | same, session 27, map `4,0,5`, `--layout 1x3` | Countdown and results drawn **big across 3 cells**, so each tile shows only its outer third of the picture (2 of 3 cells is "at least half"). This is decision 3 working, not a bug; ask the user whether it reads well enough for a child | as above |
| Regression (optional) | round on `1x2 4,5,...` with video, session 28 | as in rounds 8b to 10c | upload about 2.5 s per tile |

`game_round.py` with `--no-video` sets `sh.model.layout` and `node_map` directly. `send_config()` normalises the map (`ab.normalise_map`), so the short and the 9-entry forms both work. `--layout` defaults to 1x2.

If round 12 reads badly, that reopens decision 3 (the rejected option B was "small whenever any cell is empty"). Do not change it without the user.

Record V1, rounds 11 and 12 as a "Step 4d bench" section in `step4.md`.

### 3.3 After the wall test

1. The code is already committed and pushed. Ask the user before committing the bench record and logs ("Step 4d passes on the wall (V1, rounds 11 and 12)") and any fix, then push when asked.
2. The user sends Benjamin the note (draft below); it is his table and score_font to defend. The code is already on the branch, so this is due now.
3. Update the dashboard and memory.

> Hi Benjamin, heads-up on your layout table: for the sparse grid (DS-15) SESSION_CONFIG changed. The 9-position node_map is replaced by a cell per board number (placed 1 bit, row 4, col 4, for boards 1 to 9), and crc32[] is now indexed by board number. Grids can be up to 9 × 9 with 9 tiles anywhere; the codecs refuse a cell outside the grid or two tiles in one cell. The message is 53 B, up from 47. The table, both codecs and the vectors are updated on feature/whack-a-mole, and your score_font now takes cells instead of slots. Tiles are unaffected; the PC and the Pico must be updated together. Happy to walk you through it before Tribunal 2.

(The × in the draft is the multiplication sign the user saw; it is not an em dash.)

### 3.4 Commits of 4d

| Commit | Content |
|---|---|
| `943842d` | Wire: `layout_table.md`, `proto.h/.c`, C protocol tests and vector tool, `proto_layout.py`, `gen_vectors.py`, vectors, `test_proto_layout.py` |
| `e5105a9` | Pico: `coordinator_loop.c`, `game_rules.c`, `score_font.c/.h`, Pico tests (C9, C19c, C30) |
| `2593a48` | PC: `asset_builder.py`, `pico_link.py`, `dashboard.py`, `web/index.html` hint, PC tests |
| `6136a7b` | Bench: `video_check.py`, `numbered_cells_3x3.gif` |
| docs commit | `game_plan_v2.md` (the three 4d decisions) and this handoff |

## 4. Then: 4e website (DS-16, A1 to A16)

Already decided (DS-16, 11 Nov):
- **Layout:** one page with tabs Setup, Video and Game, under an always-visible status bar.
- **Setup tab:** grid editor (click a cell, tap its tile; click a tile to switch it on or off), tile health, tap test, sensitivity slider.
- **Video tab:** GIF library uploaded from the browser, a preview of the cut with empty cells greyed, upload with per-tile progress, Play, Pause, Stop.
- **Game tab:** level; Arm, Start now, Abort; round state; live score, missed, wrong, time left; results (hits, missed, wrong, accuracy, reaction, stars, best today).
- **Behaviour:** buttons enabled only when allowed; the page polls 4 times a second; a `/score` page for a TV.

Also decided (DS-14, D-16): **parallel uploads** to all tiles, and each tile gets only its own section.

What exists:
- **`pc/web.py`:** standard-library HTTP server with a JSON API: `/api/state`, `grid`, `place`, `slot`, `mode`, `game/start`, `abort`, `arm`, `now`, `tap_test`, `threshold`.
- **`pc/web/index.html`:** the old single page, which already iterates `rows * cols` cells.
- **`DashboardShell`:** has the website methods.
- **Tests:** `pc/tests/test_web.py` uses a `FakeShell`.

How to run 4e:
1. **Walk the user through the shape** (what the page looks like, what is new in the API), then decide one at a time. My recommendations, all open:
   - **GIF library storage:** a folder `pc/gifs/` (gitignored) with uploads via `POST /api/gifs` as raw bytes plus a name. Recommend this over keeping them in memory, so the library survives a PC restart.
   - **Preview:** the PC renders the cut grid as a PNG (`GET /api/preview.png`) with Pillow, already a dependency. Recommend this over drawing in the browser, so the preview is exactly the asset_builder cut.
   - **Parallel upload:** one `Uploader` socket per tile in threads, with progress per tile in `/api/state`. Measure on the wall (DS-14 expects 6 to 10 s for 9 tiles).
   - **Video controls (A13):** the website alone must run the whole demo, so START, PAUSE, RESUME and STOP get API routes.
   - **Best score per level (A10):** in memory for the session, or a small JSON file. Recommend in memory, since "best today" was the requirement.
   - **`/score` page (A16):** a separate static page reading `/api/state`.
2. **Fold in the open items from 4c** (each small; ask whether to include):
   - A longer first ACK wait after a MANIFEST (round 10c: about 1 s before A2 counts the tile deaf).
   - Re-send the tap test or round state to a tile that re-registers (round 8).
   - A display that stops ticking should raise the DISPLAY fault (the freeze was invisible to every check).
3. **Build:** API and tests first (`test_web.py`), then the page. Use a real browser for the end-to-end check if a browser tool is available, and be pixel-picky about the UI (the user's rule: fix anything that looks off).
4. **Wall test:** the whole demo from the website only (A13): grid editor with tap-to-place on 2 tiles, tile on and off, video play, pause and stop, game arm and abort, results, `/score`.

## 5. After step 4 (outline only; each starts with a design walk-through)

- **Step 5, whole-wall rotation** (DS-04 to DS-06, decided 9 to 11 Nov; read them first):
  - **What it is:** the tile's spin check, TURN_START and ROTATE messages, rotation of every tile's (row, col) on the Pico, a video re-cut and re-upload after a turn, and an instant redraw in game mode.
  - **Interaction with 4d:** rotation now rotates **cells within the grid**. A non-square grid swaps rows and cols, and positions map as (r, c) to (c, rows-1-r) for a quarter turn. Confirm with the user how a sparse grid turns.
  - **Taps around turns:** the spin-to-score sprite is a stretch goal (D-15).
- **Then:** 9 tiles on the Matrix router (DS-17, section 6), GPIO4 pulse measurements with the pico_tap Pico (DS-13, NR-03), the report and the defence docs (`docs/*_DEFENCE.md`; Cheston must be able to explain his modules, including today's `asset_rx` held upload and the display clock-jump guard), Tribunal 2 on Mon 23 Nov.

## 6. Bench setup

**Boards:**

| Board | USB serial (`ID_SERIAL_SHORT`) | Address | Notes |
|---|---|---|---|
| Coordinator Pico | `E66430A64B595138` | 172.20.10.2 | |
| Tile node 4 | `9C:13:9E:F1:2E:B0` | 172.20.10.4 | on sponge |
| Tile node 5 | `9C:13:9E:F1:2F:70` | 172.20.10.5 | on sponge |
| Spare Pico `…3B6838` | | | its WiFi freezes; keep it for pico_tap |

**`/dev/ttyACM*` numbers change on every replug**, so always map ports by serial: `udevadm info -q property -n /dev/ttyACMx | grep ID_SERIAL_SHORT`.

**Network:** the user's phone hotspot (172.20.10.0/28, 2.4 GHz); the laptop must be on it (it was 172.20.10.3). Never print or commit `wifi_secrets.h`.

**Flash the Pico:** `cd ledwall_sync/build && make -j8`, check the exit code, then `picotool load -x -f ledwall_sync/build/ledwall_sync.uf2`. Once on 12 Nov the Pico came up silent after this (no serial, no ping). A replug fixed it, cause unknown. Check ping after every flash.

**Flash the tiles:** `. ~/esp/esp-idf/export.sh; cd panel_sync; idf.py build`, check the exit code, then `idf.py -p /dev/ttyACMx flash`, one port at a time, with the serial loggers stopped.

**Restarts:** a Pico restart under running tiles used to freeze the tile LEDs for minutes. Fixed by the display clock-jump guard (`jumps=` in `DE ticks`); confirmed by a replug on 12 Nov.

**Health checks before a round:**
- Pico: `ping`, and `CL_STATS` lines on its serial.
- Tiles: a `DE ticks` line every 10 s.
- The bench script refuses to arm on tile faults, but **a frozen display shows no fault**, so check `DE ticks` yourself.

**Tools:**
- `docs/bench/12nov/serial_log.py` holds DTR high, which the Pico's USB serial needs, and does not reset the boards. The tiles' USB serial drops lines under load, so a missing line is not evidence.
- `docs/bench/11nov/game_round.py`: one process for layout or upload, tap test, arm and follow to RESULTS. Options `--level`, `--threshold`, `--tap-test`, `--start-now`, `--legacy`, `--no-video`, `--layout`.
- `docs/bench/12nov/tap_only.py`: tap test only (pico, session, seconds).
- `docs/bench/12nov/video_check.py`: video playback.
- `pc/.last_seq` (4b) and `pc/.last_session` are gitignored state files.

**Process hygiene:** kill by PID. Never `pkill -f` or `pgrep -f` with a pattern that appears in the same shell command.

## 7. Known limits and open items

- Soft taps near 300 mg can go unseen; soft-tap echoes can beat the real tap on the next tile (step 2). Capacitive touch is the fallback to raise with the professor.
- Moving or picking up a tile sets off taps (rounds 6, 7: wrong taps at round start).
- The open items from 4c and the 4e candidates are in section 4, step 2.
- GPIO4 event pulses are not measured yet (DS-13).
- The Pico ACKs a stale PC command that it ignores; the PC cannot tell (rare since 4b).
- 4d is pushed but not yet wall-tested (section 3.2).
