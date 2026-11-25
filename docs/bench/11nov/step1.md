# Step 1 bench, 11 Nov 2026: first end-to-end run of the v1 game

Branch `feature/whack-a-mole` @ `c5cf2af` (5 Nov display fixes, 30 ms tap group). Pico `…3B6838` (172.20.10.2), tiles 4 and 5 on sponge, phone hotspot. Built and flashed from the repo; Pico `online=0x018 sync=0x018`.

| Check | Result | Notes |
|---|---|---|
| Upload, numbered 1x2 GIF, session 20 | PASS | Both tiles 57 of 57 chunks first time, 5.2 s each (fresh Pico: session 0, so no session race) |
| Colour order (D-13) | PASS | Node number red at the top left, frame counter green at the bottom (9 Nov: the other way round) |
| Rotation at 4 angles (card F4) | PASS | One tile held at 0, 90, 180, 270 degrees: the red node number stayed at the top left each time (5 Nov remap plus mount offset, tests D4 to D6 updated) |
| Game rounds 1 and 2 | FAIL: both tiles left the network about 3 s into the round | **Deadlock in `tile_net`**: game datagrams were handled in the lwIP callback (TN_RX_HOOK 1, lwIP's tcpip thread), and the ACK's `sendto()` waits on that same thread for ever. Display kept running; no beacons, no ping. The TAP outbox was also touched from two tasks. Fix: game datagrams posted as TN_REC_GAME records and handled in the Network Task, like NACK sending. |
| Game round 3 | PASS for the deadlock (tiles stayed online 30 s); score 0 | Operator missed the round |
| Game round 4 | PASS end to end: moles shown, taps reach the Pico, score changes; FAIL on scoring | Final -7 (v1 rules: +1 hit, -1 wrong). Hits mostly paired with a -1 within about 1 s. Tile taps accepted: node 4 17, node 5 34 (some 193 to 494 ms after the previous: likely one slap counted twice, the 150 ms re-arm against slaps ringing up to 600 ms, F-05). The Pico logs no tap or group detail yet, so attribution needs the DS-13 event log. |
| Node 4 never green (operator, round 4) | Bug found: **stale TAPs after a tile restart** | The Pico kept each tile's last TAP seq across the tile's restart (reset only at Pico boot). Node 4 had sent many TAPs during the rotation check, then was reflashed and counted from 0: its TAPs were ACKed but dropped as stale (`stale` 62 to 77 during round 4). Fix: REGISTER resets that tile's TAP baseline (layout_table.md section 10 item 13); host test C23. Mirror fix on the tile: a new or restarted Pico resets the tile's game command baseline. Round 5 showed node 4's TAPs ACKed. |
| Event log (DS-13) | Added | Pico: CL_EVENT GAME_MOLE (node, up_ms, at show time), GAME_TAP (node, arrival minus tap time in us), GAME_FEEDBACK (node, 1 hit or 0 miss). Tiles: TN_TAP (seq, peak, Pico-clock time), TN_GAME (type, seq). |
| Rounds 5 and 6 (new Pico build) | **OPEN: Pico froze** about 0.2 s after the round started | After the first GAME_MOLE line: no STATUS, no beacons, no ping, no CL_STATS; picotool could still reboot it over USB (main loop stuck, chip alive). Round 4 ran 30 s on the previous Pico build. Either this board (`…3B6838`, five hangs since 9 Nov, its WiFi driver the common factor) or the new Pico build. A/B test pending: the previous build (59cac5b) is on the Pico now. |
| Round 7 (previous Pico build) | Not run: the Pico dropped the PC's commands as stale (`stale` 3, then 6) | The PC-restart bug below, despite the seq workaround. `game_round.py` now does the upload and the round in one process, so the seq never restarts. |
| Round 8 | Not run (stopped before the round) | `round8_*` logs are idle wall logs on the previous Pico build; no game messages. |
| **Round 9 (second Pico `…E6613008E344BF33`, pushed build e0c4a8e)** | **PASS: full round, no freeze** | Score 32. Pico event log: 38 moles (19 per tile, never the same tile twice in a row), 32 TAPs reached the Pico (16 per tile), 32 hits, 0 misses, 6 moles timed out; no echo or double count reached the Pico (tiles on sponge, the v1 detector). Show to verdict on hits: median 390 ms (322 to 689; the operator's reaction). TAP arrival minus tap time: median 30 ms (15 to 68), mostly the tile's 20 ms housekeeping pass before the TAP is sent, plus WiFi. **The rounds 5 and 6 freeze was the `…3B6838` board, not the code** (same build, no freeze here). Estimated tap to flash about 70 to 90 ms: inside NR-03 but tight; send the TAP at once in step 2, and measure with the GPIO4 pulses (DS-13). |

## Other findings

- **PC restart in the same session is ignored by the Pico:** a new dashboard process starts its command seq at 0 and its session at 0 until it sends SESSION_CONFIG, so the Pico drops its commands as stale (`stale=5`). Workaround in `game_run.py`; fix in step 4 (seed the seq, adopt the session).
- Upload: both tiles first time (fresh Pico, session 0: no race).


## Result and next

**Step 1 done 11 Nov:** the v1 game runs end to end on the wall with the 5 Nov display fixes; two bugs found and fixed on the way (the lwIP deadlock, stale TAPs after a restart). Pico `…3B6838` is retired from coordinator duty (freezes); it becomes the pico_tap recorder (DS-13).

Next: a wrong-tap check (tap the empty tile: expect a red flash and -1), then step 2 (software tap detector, TAP sent at once).
