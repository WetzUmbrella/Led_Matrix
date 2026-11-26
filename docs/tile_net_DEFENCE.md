# tile_net: design defence (T-B3-03, T-B3-05 comms side, T-B3-06)

Subsystem: tile networking (ESP32-S3 tile). Files: `panel_sync/main/tile_net.c/.h`, `panel_sync/main/telemetry_book.c/.h`; host tests `panel_sync/test/test_telemetry_book.c` (`make -C panel_sync/test`, "Telemetry: Pass = 5 of 5"). Coding standard: Barr-C:2018, `docs/coding_standard.md` (deviation D-2). Updated 25 Nov 2026 against `a6d4902`: sections 1 to 10 are the October design, still in the code with the values checked; section 11 is what the game added (9 to 25 Nov).

**Names.** Barr-C 6.1.i puts the module prefix on public functions: `tile_net_start()`, `tile_net_task()` (the plan's Network Task), `tile_net_timesync_queue()`, `tile_net_command_queue()`, `tile_net_node_id()`, `tile_net_has_ip()`, `tile_net_post_status()`. The plan's static names are kept (coding standard interpretation 7): `tn_on_beacon()`, `tn_send_heartbeat()`, `tn_telemetry_send()`, `tn_on_telemetry_ack()`, `tn_comms_sm()`. The plan's `q_timesync`, `q_command`, `q_telemetry` are `g_p_q_timesync`, `g_p_q_command`, `g_p_q_telemetry` (7.1.j). The plan's Upload Queue `q_upload` is created and read by `asset_store` (its only reader), posted to by `tn_on_upload()`. The pending-ACK ring is its own module, `telemetry_book`, so its rules run in the host tests.

Each entry: the design choice, the PostTri row it comes from (inventory ID from `commitment_inventory.md` or the plan row), and the question an examiner is likely to ask. Numbers not in PostTri are labelled (plan choice).

## 1. Only tile_net owns sockets; no shared variables across contexts

- Source: T-B3-03 "Only `tile_net` creates sockets"; SRM-4M; RAR-1.
- Choice: three contexts touch the module (the receive hook, the Network Task, the WiFi event handler) and talk only through FreeRTOS queues and the event group. Counters written in the receive context travel inside each log record; the Display Engine's Telemetry Queue drop count travels inside each status item. asset_store never sends: its replies come back through its Reply Queue for the Network Task to send.
- Examiner: "Which task increments `dropped`, which task prints it, and why is that not a data race?"

## 2. Beacon receive hook and the Time Sync Queue (OD-09)

- Source: T-B3-03, IFM-3a, VA-3.7 "drop-oldest with a drop counter ... `xQueueReceive(q, &old, 0)` then `xQueueSend()`".
- Choice: t_l is taken first in the lwIP callback (hook 1, default) or right after `recvfrom` (hook 2, build option `-DTN_RX_HOOK=2`); the (t_p, t_l) pair goes into the length-4 Time Sync Queue drop-oldest. The shipped build uses hook 1, the lwIP callback (`TN_RX_HOOK` default in `tile_net.h`); hook 2 stays as a build option.
- Examiner: "Why is a stale beacon worse than a dropped one for the minimum-delay filter, and how does drop-oldest serve that?"

## 3. Command Queue: reject and NACK

- Source: VA-3.6 "Command queue: reject and NACK when full"; layout_table.md NACK reason 2.
- Choice: length 3 (PROVISIONAL, OD-11): a START_AT, PAUSE, RESUME_AT burst of three copies each fits. Full: the command is not queued and a NACK reason 2 goes to the Pico with the command's seq.
- Examiner: "The Pico sends every command three times. What does a fourth arrival do to the queue and to the Pico?"

## 4. TELEMETRY every 1 s and clear-on-ACK (VC-4)

- Source: VC-4 (whole row); LDC-2; T-B3-06 row.
- Choice: the Network Task snapshots the fault word, sends TELEMETRY {seq, node_id, fault word, orientation, display state} as a subnet broadcast to UDP 4215 and records (seq, sent bits) in a 4-entry ring. A TELEMETRY_ACK on UDP 4214 that matches a ring entry calls `fault_word_clear_events(sent bits)`: only event bits are cleared, never condition bits; an unknown, expired or repeated ACK changes nothing. Bits not yet ACKed stay in the word and so ride in the next packet with no extra code. Host test T5 simulates the VC-4 receiver dropping every third packet: 100% of injected events reported, 0 off/on transitions of a held condition.
- Examiner: "An event is raised again between the send and the ACK of the packet that carried it. Is it lost? Point to the VC-4 sentence that allows your answer."

## 5. Where TELEMETRY_ACK is read

- Source: layout_table.md section 11 (ports 4214 and 4215); T-B3-06.
- Choice: port 4214 is a non-blocking socket read by the Network Task every pass (at most the 20 ms idle wait late), in both OD-09 builds, because the ACK has no timing role. Only MANIFEST and ASSET_CHUNK set the reply address for upload replies, so a TELEMETRY_ACK from the dashboard's telemetry socket cannot redirect a CHUNK_ACK (bug found and fixed in patch 13).
- Examiner: "The PC has two sockets talking to your port 4214. How does the tile know where to send a CHUNK_ACK?"

## 6. Upload routing and the session check (T-B3-05, T-X-08 case 3)

- Source: VA-3.6 "Upload queue: reject and NACK"; T-X-08 "(3) manifest with another session_id: rejected".
- Choice: MANIFEST and ASSET_CHUNK are copied into the length-4 Upload Queue; full: NACK reason 3 to the PC. An upload whose session_id differs from the session the beacons carry is dropped and counted (`upload_wrong_session`); the NACK reason list has no session code (section 10 item 8), so the PC simply sees no CHUNK_ACK.
- Examiner: "Why is a wrong-session manifest dropped silently rather than NACKed, and how would the operator know it happened?"

## 7. Comms statechart derived each pass (W6 statechart 3)

- Source: T-B3-05 states; W6D-2d CONNECTING timeout 10 s (proposed).
- Choice: `tn_comms_sm()` derives the state from what each owner publishes (IP bits and the timeout here, REGISTER here, the upload phase from asset_store, the playback state from Playback Control) and logs each change as `TN_COMMS <state>`. No second copy of the upload or playback state is kept, so the two can never disagree.
- Examiner: "Your statechart has no transition table. Where does the RECEIVING ASSET to VERIFYING ASSET transition happen, and who owns it?"

## 8. REGISTER carries the cached asset

- Source: T-B3-05 "REGISTER carries the cached session_id and CRC32 so a matching cached asset is reused after a reconnect".
- Choice: `asset_store_cached()` gives the session and CRC32 of the asset in RAM (0 and 0 when none), read through asset_store's published pointer.
- Examiner: "A tile reboots mid-session. What does it send, and what does the Pico do with it?"

## 9. One coordinator per tile (fix for the bench finding of 29 Oct 2026)

- Source: bench log 29 Oct 2026 (`docs/week10/skew_2tile/bench_log.md`): with two Picos on one SSID the tile mixed two clocks (about 17 beacons/s, `lost=` in the millions).
- Choice (30 Oct 2026, PROVISIONAL): in the receive context, the first beacon sender is locked; beacons and commands from any other address are dropped and counted (`foreign=` in `TN_STATS`); the lock moves only after the locked coordinator has been silent 2 s (twice the 1 s holdover), so a Pico reboot is followed. A new sender restarts the seq baseline, so the switch is not counted as router loss.
- Examiner: "Another team's Pico is on the lab router. What happens to your skew, and what happens when your own Pico reboots?"

## 10. Review round 1 fixes in tile_net (R1-1, R1-5, R1-6, R1-9, R1-10, R1-11)

- R1-1: REGISTER is sent `TN_REGISTER_COPIES` = 3 times, `TN_REGISTER_GAP_US` = 100 ms apart (PROVISIONAL, owner Cheston), like START_AT's three copies; heartbeats still start after the first copy. The Pico's `cl_on_register()` is idempotent apart from clearing that tile's SYNC_READY bit, which the next heartbeat restores. A beacon from the same Pico address whose session_id falls from non-zero to 0, or whose seq is more than 16 behind the previous one, sets the register request again, so a Pico reboot on the same IP is followed.
- R1-5 and R1-6: the coordinator lock moved unchanged into the pure module `coord_lock.c` (2 s switch rule, foreign count, seq baseline reset on a new sender) with a host test (`test_coord_lock.c`). One change: commands are refused while nothing is locked, where before any sender was accepted.
- R1-9: `tn_poll_pc()` reads at most `TN_PC_RX_PER_PASS` = 8 datagrams per pass, so a flood on port 4214 cannot delay the heartbeats.
- R1-10: the PC reply address is set only when `tn_on_upload()` reports that the item decoded and passed the session check, so a malformed or wrong-session datagram can no longer redirect CHUNK_ACKs. The second half of R1-10 (reject uploads while the session is 0) is not applied: the bench upload tests send straight to a tile while the Pico beacons session 0. PROVISIONAL for Cheston: decide whether to require a session once the dashboard flow is the only upload path.
- R1-11: `FAULT_BIT_QUEUE_OVERFLOW` is now raised when `tn_post_drop_oldest()` drops an item, when the Command Queue is full (the NACK path) and when `asset_store_post_upload()` fails.

## 11. What the game added (9 to 25 Nov 2026)

- **Game messages handled in the Network Task, not the receive hook** (bench 11 Nov 2026). With hook 1 the receive context is lwIP's tcpip thread. Sending the game ACK from there with `sendto()` waits on that same thread forever, and both tiles left the network 3 s into the first round. Now `tn_post_game()` copies each MOLE_ON, FEEDBACK, GAME_CONTROL, WALL_PICTURE or ACK (at most `TN_GAME_MAX_BYTES` = 24 B) into the log queue. The Network Task then hands it to `game_tile_on_pico()` and sends the ACK. The game state and the TAP outbox have one owner, as the NACK path already did.
- **Receive buffer sized by a compile-time check** (bench 12 Nov 2026). `TN_RX_BYTES` was RESUME_AT + 1 (14 B), which cut every 23 B WALL_PICTURE short, so no countdown or results showed. It is now WALL_PICTURE + 1, and a `_Static_assert` checks that every port 4210 message fits.
- **TAP outbox.** `tn_game_service()` takes each tap from the Orientation Task's queue (length 4), converts its time to the Pico clock through the time-sync snapshot and encodes a TAP. The outbox (`common/proto/ack_outbox.c`, 48 slots) resends it every `TN_TAP_RETRY_US` = 50 ms until the Pico's ACK, for at most `TN_TAP_LIFE_US` = 1 s. A tap with no clock sync yet is dropped and counted (`g_tap_unsynced`), because a local time would mis-score it. The tile's own ACKs are sent once: the Pico resends a message whose ACK is lost. See `game_tile_DEFENCE.md` section 8.
- **A new or restarted Pico resets the tile's game, clock and playback** (18 and 24 Nov 2026). A beacon from a new address, or `tn_pico_restarted()` on the same address (session fell to 0, or seq more than 16 behind), calls `tn_game_new_pico()`. That stops playback (its timeline went with the old Pico, and a playing tile refuses uploads), starts a new time-sync window (`timesync_pico_restart()`, the only re-seed since `630c390`), clears the game state and every seq baseline (`game_tile_new_pico()`), and publishes the cleared state. On 18 Nov only the message seq was reset, so a restarted Pico's ARM and pictures were dropped as old (host test G7).
- **Heartbeats offset by Node ID.** `TN_STAGGER_US` = 500 ms / 9, so nine tiles do not heartbeat in the same millisecond.

## Settled values (25 Nov 2026)

| Item | Value | Why |
|---|---|---|
| Time Sync Queue | 4, drop-oldest | a stale beacon is worse than a lost one for the minimum-delay filter |
| Command Queue | 3, reject and NACK reason 2 | one burst of three copies fits |
| Telemetry Queue | 2, drop-oldest | the Display Engine is its only producer |
| Upload Queue, Reply Queue | 4 (reject, NACK reason 3), 8 (drop-oldest) | in asset_store |
| TELEMETRY period, pending-ACK ring | 1 s, 4 entries | 4 s of unACKed packets before the oldest is forgotten; its bits are still in the word |
| Heartbeat | 500 ms, staggered by Node ID | the Pico times out at 2.5 s |
| REGISTER | 3 copies, 100 ms apart | like START_AT |
| Coordinator lock switch | 2 s of silence | twice the 1 s holdover |
| TAP resend, lifetime | 50 ms, 1 s | four sends fit inside the Pico's 200 ms MOLE_ON lead |
| CONNECTING timeout | 10 s, then keep retrying with the WIFI bit set | |
| Wrong-session upload | dropped without a NACK, counted | no NACK code for it; the PC sees no CHUNK_ACK |

No Fault Queue: faults reach the Network Task through the fault word.

## Questions to practise before the demo

Answer each in your own words, without reading the code.

1. Why can the tile not send the game ACK from the lwIP receive callback? What happened on 11 Nov when it did?
2. The Pico restarts on the same IP address. How does the tile notice, and what four things does it reset?
3. Explain, with the queue item in front of you, how the Telemetry Queue's drop count reaches the `TN_STATS` line without a variable shared between two tasks.
4. The PC's TELEMETRY_ACK for seq 41 arrives after TELEMETRY seq 42 has gone out. Walk through what the tile does.
