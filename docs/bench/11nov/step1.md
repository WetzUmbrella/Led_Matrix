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

## Other findings

- **PC restart in the same session is ignored by the Pico:** a new dashboard process starts its command seq at 0 and its session at 0 until it sends SESSION_CONFIG, so the Pico drops its commands as stale (`stale=5`). Workaround in `game_run.py`; fix in step 4 (seed the seq, adopt the session).
- Upload: both tiles first time (fresh Pico, session 0: no race).

