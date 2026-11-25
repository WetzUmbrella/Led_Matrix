# coordinator_loop: design defence (T-B2-02 to T-B2-05)

Owner: Benjamin (Buddy 2). Implemented by Ahsanul (B4) for Benjamin; T-B2-03 to T-B2-05 written on 29 Oct 2026 in Benjamin's session, for him to review, change where he disagrees, and defend. Files: `ledwall_sync/coordinator_loop.c/.h`, `ledwall_sync/main.c`; host tests `ledwall_sync/test/` (`make -C ledwall_sync/test`, "Coordinator: Pass = 43 of 43" on 25 Nov, with the game rules, the outbox, the join backoff and the wall turn). Coding standard: Barr-C:2018, `docs/coding_standard.md` (deviation D-2 only). Updated 25 Nov 2026 against `a6d4902`: sections 2 and 6 are corrected to the current code, and section 11 covers what the game and the bench added (9 to 25 Nov).

**Names.** Barr-C 6.1.i puts the module prefix on public functions, so the only public functions are `coordinator_loop_run()` and `coordinator_loop_lateness_bin()`. The plan's names are kept for the static functions (coding standard interpretation 7): `cl_rx_cb()`, `cl_dispatch_one()`, `cl_scan_heartbeats()`, `cl_connect_router()`, `cl_session_apply()`, `cl_on_asset_ready()`, `cl_send_status()`, `cl_policy_on_loss()`, `cl_triple_send()`; the plan's `cl_registry[9]` is `g_registry[CL_MAX_NODES]` (7.1.j), `cl_state` is `g_state`, `cl_lateness_hist[]` is `g_lateness_hist[]`. The plan's separate `cl_cmd_start_at()`, `cl_cmd_resume_at()`, `cl_cmd_pause()`, `cl_cmd_stop()` exist with those names; `cl_on_sync_ready()` is handled inside `cl_on_tile_report()` with HEARTBEAT and NACK, because all three refresh the same registry row.

Each entry: the design choice, the PostTri row it comes from (inventory ID and wording from `Plan/commitment_inventory.md` or the plan row), and the question an examiner is likely to ask. Numbers not in PostTri are labelled (plan choice).

## 1. Poll mode and where callbacks run (OD-01)

- Source: LDC-3, "Poll mode, pending Pico SDK citation; `critical_section` as fallback"; PostTri change log: background mode runs callbacks in interrupt context and the RP2040 has no atomic read-modify-write.
- Choice: `pico_cyw43_arch_lwip_poll`; `cyw43_arch_poll()` is the first thing in every loop pass. The SDK citation is in the header of `coordinator_loop.h` (SDK 2.3.1 `cyw43_arch.h` line 56 and 98 to 99, `async_context_poll.h` line 31). No SDK sentence states where poll-mode callbacks run, so `cl_rx_cb()` records `__get_current_exception()` and the bench run (checklist D2) must read `exc_max=0`.
- Examiner: "Prove your receive callback is not running in an interrupt. What number in your log shows it?"

## 2. One bounded routine per loop pass (OD-08 ruling)

- Source: VA-2.5 and R5, "one bounded callback per loop pass"; Round 0 ruling SC-1; T-B2-03 "One buffered message is processed per loop pass, bounded at 1 ms, and it waits when `time_us_64()` + 1 ms would pass `next_beacon_us`".
- Choice: after polling, each pass runs only the first due routine, in this order: beacon; a due copy of a timed command; one datagram from the send queue (added 19 Nov, section 11); heartbeat scan; one due resend from the acked outbox; STATUS; the game routine; one buffered message; one log line. The outbox, the game routine and a buffered message run only when 1 ms is left before the next beacon and the next command copy, and a log line only when 5 ms is left (plan choice). Host test C5 checks the 1 ms guard.
- Examiner: "Nine REGISTERs arrive in the same millisecond as a beacon is due. Walk through the next five loop passes."

## 3. Absolute beacon schedule and what a stall does

- Source: VA-2.2, "Beacon every 100 ms on an absolute schedule (next += 100 ms), not sleep after work"; VA-2.9, timestamp "immediately before `sendto()`".
- Choice: `next_beacon_us += 100000`. The beacon is fully encoded and copied into the pbuf first; then `t_p = time_us_64()` is read, written into the payload (`proto_beacon_set_t_p()`, a 64-step bit write because t_p starts at bit 29) and `udp_sendto()` follows. After a stall longer than one period, every slot that has fully passed is skipped and counted (`skipped` in `CL_STATS`); the current slot is sent at once, late, and the grid continues (plan choice). seq counts only sent beacons, so tiles do not mistake a skipped slot for router loss. Host tests C1 and C2.
- Examiner: "The loop stalls for 350 ms. How many beacons go out when it recovers, at what times, and why not three in a row to catch up?"

## 4. Lateness histogram and the send log

- Source: VA-2.6 (NFR16, lateness under 5 ms, proposed); T-B2-02 acceptance "100% of beacons have lateness under 5 ms; the Pico send log has 0 missing seq numbers".
- Choice: 1 ms bins 0 to 9 ms plus one bin for 10 ms or more (plan choice), printed every 10 s with `CL_STATS` (`sent`, `first_seq`, `last_seq`, `send_fail`). No per-beacon print: a USB host that stops reading could block `printf` (SDK default 500 ms); the build sets `PICO_STDIO_USB_STDOUT_TIMEOUT_US=1000` as well.
- Examiner: "Your log has one line every 10 s. How does it prove that no seq number is missing?"

## 5. Registry of 9 Node IDs and the heartbeat timeout

- Source: VC-11b (9 bits), VA-2.7 "Heartbeats every 0.5 s with a 2.5 s timeout (worst case 2.6 s with the 100 ms scan)" (proposed), VA-2.3 scan every 100 ms, W6B-3 "no other tile changes slot".
- Choice: `g_registry[9]` indexed by Node ID - 1; every valid message refreshes `last_heard` with the callback's receive time (not the dispatch time); the scan runs on its own absolute 100 ms schedule and marks a tile offline when it has been silent for more than 2.5 s. Slots come only from SESSION_CONFIG, so a loss changes nothing for other tiles. Host tests C3 and C4 (10 of 10 trials between 2.50 and 2.59 s).
- Examiner: "Why is the worst case 2.6 s and not 2.5 s, and what breaks if you used the dispatch time instead of the receive time?"

## 6. CONNECTING timeout and FAULT

- Source: W6D-2d, "10 s CONNECTING timeout (proposed, to be measured)"; Tribunal 1 feedback "the system statechart lacks a CONNECTING timeout".
- Choice: each join attempt is bounded at 10 s (`cyw43_arch_wifi_connect_timeout_ms(..., 10000)`, which waits for `CYW43_LINK_UP`, an IP address, so DHCP is inside the 10 s). A failed join leaves the coordinator in FAULT, and `cl_rejoin_routine()` retries on the `join_backoff_ms()` schedule: 1 s first, growing to at most 10 s, forever (`join_backoff.c`, host-tested). The October design stayed in FAULT until reset; that was changed so the wall recovers from a router reboot by itself. A lost router also stops a running round (`game_rules_stop()`).
- Known bug (25 Nov, pause policy): a tile away more than 5 s also ends in FAULT, whose only routine is the re-join, so the Pico re-joins a router that is still up. The join outlasts the 8 s watchdog (`wd_step=0x0a`, JOIN) and the Pico restarts. The demo runs `--policy continue`. Fix: a tile-loss FAULT must not re-join a router whose link is up.
- Examiner: "The router reboots during a show. What does the wall do now, and what did the October design do?"

## 7. Session, readiness and stale rejection (T-B2-04)

- Source: W6D-7b "one readiness bitmap per session, the only authority"; IFM-2a "every message carries session_id and seq; stale or mismatched messages rejected"; W6D-7a ASSET_READY {session_id, CRC32}.
- Choice: SESSION_CONFIG (now with frame_count and one CRC32 per map position, `layout_table.md` section 10 item 9) is accepted only while stopped. ASSET_READY counts only when its CRC32 equals the manifest CRC32 for that Node ID; a REGISTER whose cached session and CRC32 match also counts (rejoin without re-upload). Every tile message must carry the active session_id (tiles copy it from the beacons) and a seq newer than the last accepted from that Node ID; REGISTER restarts that baseline. Host tests C6 (100 injected, 0 accepted), C7, C8, C9.
- Examiner: "A tile reboots in the middle of a session. Its seq starts again at 0. Why are its messages not all rejected as stale?"

## 8. STATUS every 250 ms

- Source: VA-2.4, W6D-5a "status every 250 ms (proposed)", inside NFR9's 2 s.
- Choice: absolute 250 ms schedule to the address and port of the last valid datagram from the PC. Host test C10.
- Examiner: "The dashboard must show a lost tile within 2 s. Add up the worst case from the tile's last heartbeat to the dashboard."

## 9. Timed commands: 3 copies, one seq, 600 ms lead (T-B2-05)

- Source: VA-4.4 "three sends at 0, 100 and 200 ms with a 600 ms lead (DTIM 3 x 102.4 ms = 307 ms; 200 + 307 = 507 ms)"; SRM-8Md critical commands sent three times; W6D-2e STOP from PAUSED.
- Choice: copy 1 fixes t_start (or t_resume) at its own send time plus 600 ms; copies 2 and 3 resend the same bytes 100 and 200 ms after copy 1. Commands share one seq stream, separate from beacons (so beacon gaps still mean loss). Four send slots (plan choice). Host tests C11 and C12.
- Examiner: "Why 600 ms and not 500 ms today, and what has to be measured before you may drop it?"

## 10. Failure policies and rejoin

- Source: FR18 as rewritten (VA-1.7, W6B-3); W6D-2c 5 s recovery timeout (proposed); OD-14 option (a) "a RESUME_AT-style message to that tile with the current origin and a boundary at least 600 ms ahead".
- Choice: pause policy: PAUSE to the wall, RECOVERING, FAULT 5 s after the loss if the tile stays away; when every required tile is back and SYNC_READY, the wall returns to PAUSED for the user to resume. Continue policy: the others keep playing; a tile that comes back with a valid asset and SYNC_READY gets a unicast RESUME_AT for the first tick deadline at least 600 ms ahead, with that tick's frame. Host tests C13 and C14.
- Examiner: "Under the continue policy a tile rejoins. Show that it lands on the same frame as its neighbours, using the deadline formula."

## 11. What the game and the bench added (9 to 25 Nov 2026)

- **Send queue, one datagram per pass** (`CL_TXQ_LEN` = 32, bench 19 Nov 2026). A hard tap is felt by the whole board, so up to 9 TAPs arrive together, each needing an ACK. Sending dozens of datagrams back to back froze the Pico inside the WiFi driver at the end of every tap test. Now every send except the BEACON and the timed command copies goes into this queue, and the loop sends one per pass, right after polling the chip.
- **8 s hardware watchdog with breadcrumbs** (19 Nov). The loop feeds the RP2040 watchdog every pass, and the join feeds it every 100 ms while it waits. The current step goes into watchdog scratch register 0, which survives the reset, so the boot after a freeze prints where it hung (`CL_STATS wd_step`, `wd_boot=1`). Recovery from a freeze to READY took about 20 s on the bench (limit 30 s).
- **Acked outbox for game messages** (`common/proto/ack_outbox.c`). MOLE_ON, FEEDBACK, GAME_CONTROL and WALL_PICTURE are unicast with a seq and resent every 50 ms (`CL_ACK_RETRY_US`) until the tile ACKs, within a lifetime: FEEDBACK 300 ms, GAME_CONTROL 1 s, a MOLE_ON until its show time (a late mole is worse than none). The seq is one counter for every Pico command, so a tile can order them (see `game_tile_DEFENCE.md` section 3).
- **Round states on the Pico** (DS-08): GAME IDLE, ARMED, COUNTDOWN, ROUND, RESULTS in `g_round`. ARM shows a start sprite on the active tile nearest the wall's centre. A tap on it at least 400 ms after it shows (`CL_START_MIN_US`, filters a leftover tap), or Start now on the website, begins a 3 s countdown. ARM lapses after 60 s. The countdown and results go out as WALL_PICTUREs 500 ms ahead (`CL_PIC_LEAD_US`), so every tile changes in the same frame. The rules themselves are pure C in `game_rules.c` (`game_rules_DEFENCE.md`).
- **Game readiness** (A14): in game mode a tile is ready when online and SYNC_READY, with no asset. The mode changes only while stopped (`cl_stopped()`); otherwise the command is refused and logged (`GAME_REFUSED`).
- **STATUS carries the round**: round state, level, score (8 bits, -99 to 99), targets shown, missed, wrong, time left, average reaction time and the wall turn, every 250 ms. That is how the website, the Pico and the wall show the same score (NR-11, met 24 Nov).
- **Sparse grid** (DS-15): SESSION_CONFIG gives each Node ID a cell in a grid of 1 to 9 rows and columns, so 9 tiles can form any shape.
- **Whole-wall rotation in video mode** (`wall_turn.c`, D-17, 24 Nov). Each tile's orientation in TELEMETRY is compared with its own first report. A change opens a 1.5 s window, and when it closes, if every active tile turned by the same k quarter turns, the wall turned and the map rotates (a cell at row r, column c moves to row c, column rows - 1 - r). Otherwise only those tiles turned. NR-08: 10 of 10 board turns, 0 of 10 single-tile turns misread. Rotation during a round (E7) is deferred.

## Settled values (25 Nov 2026)

| Item | Value |
|---|---|
| Ports | 4210 to 4215 (`layout_table.md` section 11); TELEMETRY broadcast on 4215 |
| Beacon, scan, STATUS | 100 ms, 100 ms, 250 ms, absolute schedules |
| Heartbeat timeout | 2.5 s (2.6 s worst case with the scan) |
| Timed commands | 3 copies 100 ms apart, 600 ms lead |
| Recovery timeout (pause policy) | 5 s, then FAULT (see the bug in section 6) |
| Receive ring, send slots, send queue | 32, 4, 32 |
| Guards | 1 ms for dispatch, outbox and game; 5 ms for a log line; stats every 10 s |
| Join | 10 s per attempt, backoff 1 s to 10 s, forever |
| Watchdog | 8 s |
| Outbox | resend 50 ms; FEEDBACK 300 ms, GAME_CONTROL 1 s, MOLE_ON until its show time |
| Round | arm timeout 60 s, countdown 3 x 1 s, round 30 s, picture lead 500 ms, start tap at least 400 ms after the sprite |

OD-01 (poll mode, callbacks not in an interrupt) is settled: `exc_max=0` on the bench.

## Questions to practise before the demo

Answer each in your own words, without reading the code.

1. Why does every send now go through a queue, one per pass? What happened on 19 Nov without it?
2. A tile's heartbeats stop at time T. List every routine and timer involved until the website shows it missing, with the worst-case delay of each.
3. The Pico freezes during a round. Walk through what happens until the website is back to READY, and where the breadcrumb tells you it hung.
4. Explain the pause-policy bug and the one-line rule that would fix it.
5. Why is poll mode safe for the readiness bitmap without a critical section?
