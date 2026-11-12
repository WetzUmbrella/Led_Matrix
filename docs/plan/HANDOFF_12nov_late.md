# Session handoff, 12 Nov 2026 late (whack-a-mole: 9-tile wall on Matrix, step 4 done except 4e)

Written by the orchestrating agent for itself (or any agent) starting cold. **This file supersedes `HANDOFF_12nov.md` (evening) and `HANDOFF_12nov_night.md` (other laptop) for "where things stand".**

- The method (sections 2 and 2a of `HANDOFF_12nov.md`) still governs; everything learned since is folded into section 3 below.
- The user (Cheston) said they will do **step 4e (the website)** themselves another time. If asked to help, follow section 6.
- **Role:** this agent is the main orchestrator. Its test method overrides any other agent's handoff. A second agent sometimes works from another laptop (Windows) and pushes to the same branch. At every session start: `git fetch`, read anything new it pushed, verify its claims against code and logs, and fold its useful additions into the method.

Read before acting, in this order:
1. This file, all of it.
2. `HANDOFF_12nov.md` sections 2 and 2a (the method, in detail).
3. `docs/bench/12nov/step4.md`, the last three sections ("Step 4d bench", "Matrix bench", "9-tile bench", and "Pico receive buffer and time sync on Matrix").
4. `docs/plan/game_plan_v2.md`:
   - DS-15, with the 12 Nov step 4d decisions;
   - "Time sync on the demo network";
   - DS-16 (website) and DS-14 (parallel uploads);
   - requirements A1 to A16.

## 1. Where things stand

| Item | State |
|---|---|
| Branch | `feature/whack-a-mole` in `~/SIT/Y2/inf2004/project/Led_Matrix`. Head on origin: the commit with this file, after `fab5601`. Working tree clean. |
| Step 4a (game needs no video) | Done, wall-tested (round 6) |
| Step 4b (PC restart keeps commands) | Done, wall-tested (round 7) |
| Step 4c (upload fixes, results blank on tap test and mode switch, faster windows, display clock-jump guard) | Done, wall-tested (rounds 8b to 10c) |
| Step 4d (sparse grid up to 9x9, SESSION_CONFIG with a cell per Node ID) | Done, wall-tested on 2 tiles (V1, rounds 11, 12, by the other agent) and on 9 tiles (V2b, round 15b) |
| Pico receive-buffer fix (step 4d bug: 48 B buffer, 53 B message) | Done, host test C31, wall-tested (V2b) |
| Time sync: re-seed only after 3 off beacons in a row (decision A) | Done, host-tested, on all 9 tiles, round 15b clean (0 re-seeds, 0 missed ticks on tile 5) |
| Time sync under sustained congestion | **Accepted by the user** as a known limit (section 5). The "hold the reference" fix is kept as a patch, not merged. |
| Bench script fault check (decision 2) | Done: `game_round.py` refuses to arm only on condition faults; event faults are printed as a warning. Host-tested; **not yet seen on the wall**. |
| **Step 4e website** | **Not started.** The user will do it (section 6). |
| Gate | Passes at head: C host tests and ASan (proto, panel_sync, ledwall_sync 36 of 36, pico_tap), barr_check 0 findings, pytest 460 passed |

### Firmware on the bench (all built with Matrix credentials)

| Board | Code | How verified |
|---|---|---|
| Pico `E66430A64B595138` | `ec7f0cf` (receive buffer) on top of 4d | V2b, round 15b |
| Tiles 1 to 9 | `6d70e30` (time sync A) on top of 4c and 4d | Each flashed one at a time, `DE ticks` checked after |

`panel_sync/build` and `ledwall_sync/build` were rebuilt from the committed head at the end of the session (both exit 0), so they match the branch and the boards.

### Commits of this session (newest last)

| Commit | Content |
|---|---|
| `943842d`, `e5105a9`, `2593a48`, `6136a7b`, `c04b529` | Step 4d: wire, Pico, PC, bench tools, docs |
| `d2a7911`, `32ff7cc` | Other agent: 4d wall test; Matrix and 9 tiles; night handoff (merge `776b51a` also brought Ahsanul's 6 Nov `scripts/` runner and gate commits) |
| `ec7f0cf` | Pico receive buffer from the protocol, compile-time size checks, over-long datagrams dropped, C31 |
| `6d70e30` | Time sync 3-in-a-row re-seed rule, `TS dropped` log line, `timesync_dropped_count()` |
| `023eb17` | Bench record V2b, rounds 15 and 15b, congestion simulation, handoff method updates |
| `010425b` | Decision 2: conditions and events in `pc/telemetry.py`, `game_round.py` arms on events with a warning |
| `fab5601` | Time-sync limit accepted (bench record, game plan), `tools/timesync_hold_reference.patch` |
| (this commit) | This handoff and `docs/bench/12nov/tools/flash_tiles.sh` |

**Commit identity on the other laptop:** commits there are authored as `WetzUmbrella <ahsanulpro@gmail.com>` (Ahsanul's identity) for the user's work. The user was told; they may set `git config user.name/user.email` there.

## 2. Decisions made this session (all with the user, all recorded in `game_plan_v2.md`)

| Topic | Decision | Rejected |
|---|---|---|
| 4d wire | A slot per Node ID: placed (1 bit), row (4), col (4); `crc32[n-1]` by Node ID; 53 B | A list of (node, row, col) entries |
| 4d video | Cut over the whole grid; empty cells not shown | Fit to the placed tiles' box; the whole GIF on every tile |
| 4d pictures | Big when the placed tiles fill at least half the grid, otherwise small on each tile. User: round 12's big picture with a missing middle "reads well enough for a child". | Small whenever any cell is empty |
| Results blanking (4c) | The wall goes blank on the tap test and on a mode switch; the results keep looping otherwise | A results timeout |
| Time sync A | Re-seed only after 3 samples in a row more than 0.5 s off; single late beacons dropped | A 5 s threshold; late and early treated differently |
| Time sync congestion | Accept that tiles follow sustained congestion, **as long as the error keeps shrinking as they pull back** (host check: never rises again; within 5 ms 40 s after a 900 ms ramp) | Hold the reference (built, dropped: a 600 ms plateau still re-seeded onto congestion; fixing that needs decision A changed to "re-seed when the Pico time stamp jumps back") |
| Decision 2 | The bench script refuses to arm only on condition faults (WIFI, BEACON_LOSS, IMU, ASSET_FORMAT, DISPLAY); events are a warning | Keep the strict check |

Small choices I made and told the user:
- the 5 ms jitter floor for the hold (it went with the dropped fix);
- the 9-entry bench map still accepted for small grids (`ab.normalise_map`);
- `cl_centre_node()` ties go to the first cell in row-major order.

## 3. Method additions from this session (on top of `HANDOFF_12nov.md` 2 and 2a)

**Wire and buffers.**
- When a message changes size, grep every fixed receive buffer on every board and test through the receive path. `coordinator_loop_test_rx()` goes through `cl_enqueue()`.
- The Pico now has `_Static_assert`s for every accepted type. The tile's port 4210 buffer has had a compile-time check since step 3.

**Timing problems the wall cannot repeat.**
- Simulate on the host with `TIMESYNC_HOST_TEST`, against each code variant: `docs/bench/12nov/tools/timesync_congestion_sim.c`.
- A quoted include picks up the header next to the `.c` file, so copy both files for each variant. One comparison this session was invalid until I did.
- Before blaming new code, compare with the committed code: `git show HEAD:path > variant/...`.

**Reading the logs (Pico `CL_EVENT` values).**
- `GAME_TAP value` is arrival minus tap time in us (modulo 2^32). 15 to 50 ms is normal; hundreds of ms means that tile's clock is off. Per-tile means by awk are in the session's round 15b analysis.
- `GAME_FEEDBACK value`: 1 hit, 0 miss.
- Tile `DE ticks ... missed= jumps=` is printed every 10 s. `TS reseed` and `TS dropped` lines show time-sync behaviour.
- `BCN,seq,t_p,t_l`: compute `t_l - t_p` per beacon. A jump of more than 0.5 s means a late beacon.

**Faults.**
- Event bits stay set until the PC ACKs a TELEMETRY. After any reflash, acknowledge for a few seconds before reading faults:
  ```
  .venv/bin/python -I -c "import sys,time; sys.path.insert(0,'pc'); import telemetry; r=telemetry.TelemetryReceiver(); e=time.time()+5
  while time.time()<e: r.poll(); time.sleep(0.1)"
  ```
  Then read faults the same way.
- A passive listen (bind 4215, `telemetry.parse()`, no ACK) shows each tile's address, faults and session without changing anything. It is the quickest "is every tile on the network" check.

**USB and power.**
- **Flash tiles one at a time on a laptop port.** A hub with 3 tiles plus the Ethernet adapter dropped everything twice. The second time no port saw it until tiles were plugged in directly.
- Use `docs/bench/12nov/tools/flash_tiles.sh --dry-run`, then without `--dry-run`. It maps USB serials to tile numbers, skips the Pico, flashes, and prints each tile's `DE ticks`. Pass tile numbers to skip.
- Kernel evidence: `journalctl -k --since '-5 min' | grep -iE 'usb|cdc_acm|r8152'`.

**Network.**
- The wired adapter's name follows its USB port: `enp0s13f0u3u2` on one port, `enp0s13f0u2u2` on another.
- The firewall allows the boards' UDP **per interface name**. Both names are allowed now; a third port needs `sudo ufw allow in on <name> from 192.168.1.0/24 proto udp`.
- NetworkManager makes a new profile per name. Set `ipv4.never-default yes` (and ipv6) on it, or the cable can take the default route. "Wired connection 1" and "2" both have it now.
- Symptom of the firewall: `rx_packets` on the adapter rises (about 90 in 5 s), but no telemetry reaches a socket.

**Boards that are silent after power-up.**
- Reset them with `docs/bench/9nov/tools/grab.py` (resets and logs the boot).
- Tiles 1 to 3 were stuck from an earlier power-up and joined Matrix normally after a reset.
- Read the boot for `connected with`, `sta ip`, `node N`. Never print the WiFi name or password in replies; it is fine in the terminal, but blank it in anything committed.

**Talking to the user.**
- The user sometimes names the wrong tile number; confirm which physical tile first.
- They also stop commands they did not expect. When a command is rejected, stop and ask; do not retry the same thing.
- Before a hardware step that needs their hands (plugging, power-cycling), say exactly what to do and why.

**The bench script's "go" rule** still applies to rounds. Flashing does not need a "go" (the user said "you can simply flash").

## 4. Bench setup (current)

**Network:**
- The laptop's WiFi is on the user's hotspot (172.20.10.x), or another network the laptop joins automatically; that is only for the agent.
- The wall is on the **Matrix router** (2.4 GHz, WPA2, Wireless router mode, no internet). The laptop reaches it by a **USB Ethernet adapter in a LAN port** (laptop 192.168.1.228).
- The two networks are not bridged (never-default on the wired profiles).
- Both `wifi_secrets.h` files name Matrix. The tiles' copy came from `~/.config/se15/panel_wifi_secrets_matrix.h`; the Pico's was typed in by the user. Never print them.

**Boards** (USB serial = MAC for tiles; addresses from the router's DHCP, stable so far):

| Board | USB serial | Address |
|---|---|---|
| Pico | `E66430A64B595138` (MAC `2c:cf:67:d7:eb:c3`) | 192.168.1.154 |
| Tile 1 | `9C:13:9E:F1:30:70` | .86 |
| Tile 2 | `B8:F8:62:D8:09:F8` | .144 |
| Tile 3 | `B8:F8:62:D8:05:EC` | .97 |
| Tile 4 | `9C:13:9E:F1:2E:B0` | .6 |
| Tile 5 | `9C:13:9E:F1:2F:70` | .14 |
| Tile 6 | `9C:13:9E:F1:2F:E4` | .130 |
| Tile 7 | `9C:13:9E:F1:2C:70` | .51 |
| Tile 8 | `9C:13:9E:F1:2B:D0` | .75 |
| Tile 9 | `B8:F8:62:D8:0E:D8` | .219 |

The wall is 3x3 in reading order: map `1,2,3,4,5,6,7,8,9`, layout `3x3`. The start sprite goes on tile 5 (the centre). Usually the Pico and tile 5 are on USB for logs.

**Next free session id: 34.** Recent sessions: 29 V2, 30 round 14, 31 V2b, 32 round 15 (refused), 33 round 15b.

**Commands:**
- Video: `.venv/bin/python -u docs/bench/12nov/video_check.py 192.168.1.154 <session> docs/bench/12nov/numbered_cells_3x3.gif 3x3 1,2,3,4,5,6,7,8,9 15`
- Game: `.venv/bin/python -u docs/bench/11nov/game_round.py 192.168.1.154 <session> - 1,2,3,4,5,6,7,8,9 --layout 3x3 --no-video --level easy --threshold 300 --tap-test 20`
- Tap test only: `docs/bench/12nov/tap_only.py <pico> <session> <seconds>`. It needs a session the Pico already holds; after a Pico restart use `game_round.py`.
- Serial logs: `docs/bench/12nov/serial_log.py <seconds> <port>=<file> ...`. Name logs by board, wait for the previous logger PID to exit.
- Flash the Pico: `cd ledwall_sync/build && make -j8`, check the exit code, then `picotool load -x -f ledwall_sync/build/ledwall_sync.uf2`. Check ping and `CL_STATS` after; once it came up silent and needed a replug.
- Flash tiles: `. ~/esp/esp-idf/export.sh; cd panel_sync; idf.py build`, check the exit code, then `bash docs/bench/12nov/tools/flash_tiles.sh` with tiles plugged in one at a time.
- Gate: from `Led_Matrix`, `PATH=$(pwd)/.venv/bin:$PATH bash ../game_handoff_bundle/gate.sh $(pwd)`.

**Health check before a round:**
1. The Pico pings.
2. `CL_STATS` shows `online=0x1ff sync=0x1ff`.
3. All 9 tiles are heard with `faults=none`, after an ACK pass.
4. The tile on USB prints `DE ticks` every 10 s with `missed` flat.

## 5. Known limits and open items

- **Time sync follows sustained congestion** (accepted). Tiles can be hundreds of ms off for tens of seconds after a congested period on the router. Effects:
  - tap grouping (an echo with a wrong time stamp counts as a separate wrong tap);
  - reaction times read high;
  - video sync on those tiles.

  The error shrinks at the 100 us per tick slew, about 6 ms/s. If the demo room is busy and it shows, revisit with `tools/timesync_hold_reference.patch` and the findings in `step4.md`. That needs decision A changed so a re-seed happens when the Pico time stamp jumps back by more than 1 s, and late beacons never re-seed; present it to the user as a decision.
- **Echoes on the 3x3 wall:** wrong taps from neighbours. DS-03 asked for a re-check of "highest peak wins" with more tiles. Round 15b's wrong taps were the user's own, so the clean re-check is still to do, with clocks in sync and no deliberate wrong taps.
- **Open from 4c**, candidates for 4e:
  - a longer ACK wait for the first window after a MANIFEST;
  - re-send the tap test and round state to a tile that re-registers;
  - a display that stops ticking should raise DISPLAY.
- **Uploads go one tile after another:** about 22 s for 9 tiles (2.1 to 3.2 s each). DS-14 wants parallel uploads (expected 6 to 10 s); this is in 4e.
- **Not measured:** GPIO4 pulses (DS-13).
- **The Pico occasionally does not start after `picotool load -x -f`.** A replug fixes it; cause unknown.
- **A Pico restart costs each tile a burst of missed ticks** while time sync re-seeds. With the guard the display does not freeze; the event fault is set until acknowledged.

## 6. Step 4e, the website (the user will do it; how to help if asked)

**Requirements:** A1 to A13 and A16 in `game_plan_v2.md`, layout decided in DS-16:
- **One page, three tabs** (Setup, Video, Game) under an always-visible status bar: Pico state, tiles online, mode, problems.
- **Setup:**
  - a grid editor up to 9x9: click a cell, then tap its tile (`/api/place`); click a placed tile to switch it on or off (`/api/slot`);
  - tile health (online, orientation, cell);
  - the tap test, which should show each tap's peak (A11);
  - the sensitivity slider (`/api/game/threshold`).
- **Video:**
  - a GIF library uploaded from the browser;
  - a preview of exactly how the GIF is cut for the current grid, empty cells greyed;
  - upload with progress per tile, in parallel (DS-14);
  - Play, Pause, Stop (A13).
- **Game:**
  - level; Arm, Start now, Abort;
  - the round state;
  - a live score, missed, wrong and time left (A7, within 500 ms);
  - results: hits, missed, wrong, accuracy, average reaction, stars (A9), and best per level for the session (A10, "new best").
- **Scoreboard:** a `/score` page in large type (A16).
- **Behaviour:** buttons enabled only when allowed; the page polls `/api/state` 4 times a second; no WebSockets.

**What exists:**
- `pc/web.py` (241 lines, standard-library `http.server`, one lock around the shell). Routes:
  - `GET /api/state`;
  - `POST /api/grid`, `place`, `slot`, `mode`;
  - `POST /api/game/start`, `abort`, `arm`, `now`, `tap_test`, `threshold`.
- `pc/web/index.html` (211 lines, plain JS, no CDN). It is the old single page; it already draws `rows * cols` cells from `node_map` and `parked`.
- `DashboardShell` website methods: `snapshot`, `set_grid`, `set_slot`, `arm_place`, `set_mode`, `start_game`, `arm_game`, `start_now`, `set_tap_test`, `set_level`, `set_threshold`, `abort_game`, `publish`.
- `pc/tests/test_web.py` (20 tests, with `FakeShell`).
- **The model since 4d:** `node_map` is the row-major list of `rows * cols` cells (0 = empty), and `parked[slot]` remembers a switched-off tile's number. `snapshot()` returns `rows`, `cols`, `node_map`, `parked`, Pico state, readiness, online, round counts and messages.

**Not there yet** (each needs an API route and a test):
- a GIF library (no upload route);
- a preview;
- video Play, Pause, Stop routes (the shell has `do_start` and the others, plus the link's `start`, `pause`, `resume`, `stop`);
- parallel uploads (`run_uploads` is sequential);
- per-tile progress;
- tap peaks: `PicoLink.poll_status()` keeps only the board number of a TAP, not its peak;
- best-per-level tracking;
- `/score`.

**My open recommendations**, all for the user to decide one at a time:

| Question | Recommendation | Why |
|---|---|---|
| GIF library | `pc/gifs/` (add to `.gitignore`), uploaded by `POST /api/gifs` with the raw bytes and a name | It survives a PC restart |
| Preview | Rendered on the PC (`GET /api/preview.png`) from `asset_builder`'s own cut, with Pillow | It is exactly what the tiles get |
| Parallel uploads | One `Uploader` per tile in threads, progress in `/api/state`; measure on the wall | DS-14 expects 6 to 10 s for 9 tiles |
| Best score per level | In memory | "Best today" was the requirement |
| `/score` | A separate static page reading `/api/state` | |

**How to build it** (the method):
1. Walk the user through the shape first, then decide one question per message.
2. API and tests first, then the page.
3. Check the page in a real browser if a browser tool is available. Be pixel-picky: the user's rule is to fix anything that looks off, even if unrelated.
4. Gate before each commit.

**Wall test (A13):** the whole demo from the website alone:
1. Power-on.
2. Grid editor with tap-to-place on the 9 tiles.
3. A tile off and on.
4. Video upload with progress, then play, pause, stop.
5. Game arm, abort, a full round, results, `/score`.
6. Run the regression rounds from section 4 alongside.

The website binds 0.0.0.0:8080. For a phone or TV on Matrix to open it, the laptop needs a TCP rule on the wired interface, for example `sudo ufw allow in on enp0s13f0u2u2 from 192.168.1.0/24 proto tcp to any port 8080`.

## 7. After step 4 (outline; each starts with a design walk-through with the user)

- **Step 5, whole-wall rotation** (DS-04 to DS-06):
  - the tile's spin check, TURN_START and ROTATE, rotation of the cells on the Pico;
  - a video re-cut and re-upload after a turn, and an instant redraw in game mode;
  - for a quarter turn the cells map as (r, c) to (c, rows-1-r), and rows and cols swap;
  - ask the user how a sparse grid should turn.
- **Echo re-check on the 3x3** with clocks in sync (DS-03).
- **GPIO4 pulse measurements** (DS-13).
- **Report and defence docs.** Cheston must explain his modules, including today's `asset_rx` held upload. Benjamin's table changed (4d), and his coordinator receive buffer (C31). Ahsanul's time sync changed (decision A, and the accepted congestion limit).
- **Tribunal 2:** Mon 23 Nov (Week 13), with the demo in the team's lab that week.

**Messages the user still has to send** (drafts):
- **Benjamin:** the SESSION_CONFIG change, draft in `HANDOFF_12nov.md` section 3.3. Add one line: "The Pico's receive buffer was 48 B and cut the 53 B layout message; it is now sized from the protocol with compile-time checks (C31)."
- **Ahsanul:** "Time sync now re-seeds only after 3 beacons in a row more than 0.5 s off (single late beacons on the Matrix router caused re-seed storms and missed display ticks). Under sustained congestion the estimate still follows the delay and recovers at the slew rate; we accepted that for now. Details in step4.md, last section."

## 8. Personal context (only if available; ignore if you cannot see these files)

- The user's planning dashboard is `~/SIT/Y2/ACADEMIC_DASHBOARD.md`, and the agent memory is in `~/.claude/projects/-home-coloc-SIT-Y2-inf2004-project/memory/`. Both were updated this session.
- Deadlines named on 12 Nov:
  - UDE2222 A2 deck: Fri 13 Nov 23:59 (still open on the dashboard on 12 Nov);
  - INF2001 M2 presentation: Wed 18 Nov;
  - INF2004 lecture quiz and practical: Week 12;
  - Tribunal 2: Mon 23 Nov.
- Remind the user once per session of anything closer than the bench work.
