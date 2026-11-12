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
