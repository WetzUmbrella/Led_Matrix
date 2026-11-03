# tile_net: design defence (T-B3-03, T-B3-05 comms side, T-B3-06)

Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston: T-B3-03 in Ben's session (29 Oct 2026); TELEMETRY (T-B3-06), the PC port, the comms statechart and the upload routing (T-B3-05) in Jia Jun's session (29 to 30 Oct 2026), for Cheston to review, change where they disagree, and defend. Files: `panel_sync/main/tile_net.c/.h`, `panel_sync/main/telemetry_book.c/.h`; host tests `panel_sync/test/test_telemetry_book.c` (`make -C panel_sync/test`, "Telemetry: Pass = 5 of 5"). Coding standard: Barr-C:2018, `docs/coding_standard.md` (deviation D-2).

**Names.** Barr-C 6.1.i puts the module prefix on public functions: `tile_net_start()`, `tile_net_task()` (the plan's Network Task), `tile_net_timesync_queue()`, `tile_net_command_queue()`, `tile_net_node_id()`, `tile_net_has_ip()`, `tile_net_post_status()`. The plan's static names are kept (coding standard interpretation 7): `tn_on_beacon()`, `tn_send_heartbeat()`, `tn_telemetry_send()`, `tn_on_telemetry_ack()`, `tn_comms_sm()`. The plan's `q_timesync`, `q_command`, `q_telemetry` are `g_p_q_timesync`, `g_p_q_command`, `g_p_q_telemetry` (7.1.j). The plan's Upload Queue `q_upload` is created and read by `asset_store` (its only reader), posted to by `tn_on_upload()`. The pending-ACK ring is its own module, `telemetry_book`, so its rules run in the host tests.

Each entry: the design choice, the PostTri row it comes from (inventory ID from `commitment_inventory.md` or the plan row), and the question an examiner is likely to ask. Numbers not in PostTri are labelled (plan choice).

## 1. Only tile_net owns sockets; no shared variables across contexts

- Source: T-B3-03 "Only `tile_net` creates sockets"; SRM-4M; RAR-1.
- Choice: three contexts touch the module (the receive hook, the Network Task, the WiFi event handler) and talk only through FreeRTOS queues and the event group. Counters written in the receive context travel inside each log record; the Display Engine's Telemetry Queue drop count travels inside each status item. asset_store never sends: its replies come back through its Reply Queue for the Network Task to send.
- Examiner: "Which task increments `dropped`, which task prints it, and why is that not a data race?"

## 2. Beacon receive hook and the Time Sync Queue (OD-09)

- Source: T-B3-03, IFM-3a, VA-3.7 "drop-oldest with a drop counter ... `xQueueReceive(q, &old, 0)` then `xQueueSend()`".
- Choice: t_l is taken first in the lwIP callback (hook 1, default) or right after `recvfrom` (hook 2, build option `-DTN_RX_HOOK=2`); the (t_p, t_l) pair goes into the length-4 Time Sync Queue drop-oldest. OD-09 is settled by the smaller p99 spread in bench step D4.
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

## PROVISIONAL list for Cheston to confirm

- OD-11: Time Sync Queue 4, Command Queue 3, Telemetry Queue 2 (Display Engine its only producer), pending-ACK ring 4, telemetry period 1 s; Upload Queue 4 and Reply Queue 8 (in asset_store).
- No Fault queue: faults reach the Network Task through the fault word (RAR-1); its length and policy stay open.
- After the CONNECTING timeout the tile keeps retrying with the WIFI bit set.
- Port 4214 polled by the Network Task in both OD-09 builds.
- Wrong-session uploads dropped without a NACK.
- VC-4 test build `TN_TEST_INJECT_FAULTS`: one CRC_FAIL every 5 s.

## Questions for Cheston before Tribunal 2

1. Explain, with the queue item in front of you, how the Telemetry Queue's drop count reaches the `TN_STATS` line without a variable shared between two tasks.
2. Walk through what happens on the tile, step by step, when the PC's TELEMETRY_ACK for seq 41 arrives after TELEMETRY seq 42 has already been sent.
3. Justify each queue length in the PROVISIONAL list from a message rate.
