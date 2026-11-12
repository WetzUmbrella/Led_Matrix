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
