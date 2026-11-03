# Review log

## Round 1: independent review of patches 01 to 17 (3 Nov 2026, Ahsanul)

Two fresh reviewers, neither of which wrote the code, over `git diff b4/handover b4/phase2`: reviewer A on Tribunal and PostTri fidelity, reviewer B on Barr-C:2018 and integration. Ahsanul read the code behind every BLOCKING item and the items marked "checked" below; the rest are as the reviewers reported. Nothing was fixed in this round. Nothing has run on a board.

Checks at `b4/phase2` before the review: `barr_check.py` 64 files, 0 findings; clang-format clean; pytest 223 passed; `panel_sync/test`, `common/proto/test` and `ledwall_sync/test` pass, with `asan`; each of the 13 task branches passes the same set on its own. Pico build 0 warnings (text 347,704 B, bss 50,500 B). Tile build 0 warnings, DIRAM 118,615 B used, IRAM 16,383 of 16,384 B.

### BLOCKING (fix before the Week 10 bench runs)

| # | Where | Finding | Suggested fix | Checked |
|---|---|---|---|---|
| R1-1 | `tile_net.c` `tn_handle_record()`, `tn_net_step()`; `coordinator_loop.c` `cl_on_tile_report()` | REGISTER is sent once, on a new IP or a new Pico address. If that one datagram is lost, or the Pico reboots on the same DHCP address, the Pico drops every HEARTBEAT and SYNC_READY from the tile as unregistered (`stale`), so the tile stays invisible until it reboots. | Repeat REGISTER until the Pico shows it knows the tile (for example the tile's bit in the beacon or STATUS readiness bitmap), and re-REGISTER when the beacon seq or session goes backwards | yes |
| R1-2 | `timesync.c` publication (never stepped, at most 100 us per tick) with `tile_net.c` `tn_on_beacon()` coordinator switch (both reviewers) | A coordinator switch or a Pico reboot changes the `t_p` epoch. The window mixes two epochs and the published offset then moves at about 6 ms/s, so it takes minutes to hours to converge, while `sync_ready` stays true because only the newest sample's age is tested. The tile plays at the wrong time while reporting ready. | On a lock change, or a `t_p` that goes backwards, flush the window and re-seed through the first-publication path; drop SYNC_READY until the new window is full. Add a bench step: skew after a switch | yes |
| R1-3 | `asset_store.c` RAM copy, `asset_store.h` `ASSET_STORE_RAM_NEEDED` | Placement compares the largest internal block with a fixed 90,368 B (600 frames), but manifests up to 1023 frames (98,208 B) and the 115,200 B 8-bit asset are accepted. `CONFIG_SPIRAM` is off, so the PSRAM branch always fails; the tile raises CRC_FAIL and never sends ASSET_READY. Risks the G3 8-bit run. | Compare against the manifest length plus the margin; if internal RAM cannot hold it, reject the MANIFEST with a NACK rather than fail after the upload. The B0 `HEAP largest_internal=` line gives the real headroom | yes |

### SHOULD FIX

| # | Where | Finding | Suggested fix | Checked |
|---|---|---|---|---|
| R1-4 | `tile_net.c` coordinator lock (patch 17) | First beacon heard wins, with no identity check. After a tile reboot, a WiFi outage or more than 2 s of lost beacons from our Pico, a tile can lock to another team's Pico and stay there; tiles can split between Picos. No flapping (a switch needs 2 s of silence). | Prefer the Pico the tile REGISTERed with, or pin the Pico address from SESSION_CONFIG. Until then "one Pico per SSID" still applies; say so in `tile_net_DEFENCE.md` | |
| R1-5 | `tile_net.c` command acceptance | Commands from any sender are accepted while no coordinator is locked, so a foreign Pico that speaks first can START_AT. | Accept commands only from the locked coordinator | |
| R1-6 | `tile_net.c` lock logic | No host test: the lock lives in a static FreeRTOS-linked function, unlike `telemetry_book` and `asset_rx`. | Extract a pure `coord_lock` with a table test (first lock, foreign, stale switch, return, seq wrap) | |
| R1-7 | `asset_store.c` `as_handle()` and the write wrappers | Time-of-check gap: flash permission and the unpublish/free happen before Playback can accept START_AT. The tile can end up ARMED or PLAYING with no asset while the Display Engine may still hold the freed block. Also an erase burst can start in READY just as START_AT arrives (PostTri: no flash writes in ARMED or PLAYING). | Playback refuses START_AT while an upload is past WAITING/READY, or both sides check under one lock | |
| R1-8 | `asset_store.c`, `asset_rx.c`, `pc/uploader.py` | After verify, a repeated last chunk is ignored with no CHUNK_ACK. If the final CHUNK_ACK is lost the uploader retries to `MAX_ROUNDS` and fails although the tile is READY. ASSET_READY is a single datagram. | ACK repeats of the just-completed session's chunks | |
| R1-9 | `tile_net.c` `tn_poll_pc()` | `while (b_more)` has no bound; a host streaming to 4214 starves heartbeats and the Pico declares the tile lost after 2.5 s. | Cap at about 8 datagrams per pass | yes |
| R1-10 | `tile_net.c` `tn_poll_pc()` | `g_pc_addr`/`g_pc_port` are overwritten by any MANIFEST or ASSET_CHUNK before validation and the session check; the session check is skipped while `g_session` is 0. Any LAN host can redirect CHUNK_ACKs. | Set the reply address only after the item is accepted; require a nonzero session | yes |
| R1-11 | fault word | `FAULT_BIT_QUEUE_OVERFLOW` and `FAULT_BIT_SYNC_STALE` are never set by production code, so the VC-4 queue-overflow report cannot fire. | Set them in `tn_post_drop_oldest()`, the upload NACK path and the timesync drop/stale paths | yes |
| R1-12 | `display_engine.c` reporting | A start-up or RMT FAULT is overwritten by IDLE; orientation is posted only on a display-state change, so a rotation while waiting for the tick is never reported. | Keep FAULT sticky; post when either value changes | |
| R1-13 | `timesync.c` `timesync_newest_age_us()` | A failed seqlock read returns `INT64_MAX`, which Playback reads as beacons absent (one bad read: HOLDOVER; two: pause with BEACON_LOSS). Same pattern in `de_prepare_frame()` with the timeline. | Retry or return the last good value | |
| R1-14 | `tile_net.c` start-up | `udp_new()` failure and Node ID 0 only print; Node ID 0 also wraps the heartbeat stagger to about 66 hours. A dead tile with no fault bit. | Set a fault bit and refuse to start | |
| R1-15 | `tile_net.c` BCN printing | A `printf` per beacon (10 Hz) in the Network Task: a stalled console blocks heartbeats and telemetry. | Rate-limit or make the BCN line a build option | |
| R1-16 | `continuation_plan.md` section 4 | The patch 17 switch time `TN_COORD_SWITCH_US` (2 s) is PROVISIONAL in `tile_net.h` and the DEFENCE doc but missing from section 4. The DEFENCE argument "twice the 1 s holdover" mixes PostTri's 1 s with Playback's 200 ms holdover entry. | Row added in this round; reword the DEFENCE argument | yes |
| R1-17 | T-B3-06, VA-3.5 | No Fault queue (faults go through the fault word, RAR-1). Sound, but an examiner reading VA-3.5 will look for it. | State it in `tile_net_DEFENCE.md` as a deviation and name who accepted it | |

### NIT

- `docs/week13/after/idf_size.txt` was built at patch 14; re-run before sizes go in a report (IRAM is 1 B from full).
- `docs/status/overall.md` sections 1, 2 and 6 are a Phase 0 snapshot (still name `led_strip`); add a one-line "superseded" note.
- Dead code: `tile_net_node_id()`, `playback_control_frame_at()`, `playback_control_rejected()`; PREPARING_FRAME and TRANSMITTING_PIXELS never reported.
- `asset_store.c` defines `AS_COUNTER_LOG_MS` twice; `% 2u` should use the array size.
- `display_engine.c`: the swap critical section protects data only `de_task` touches; remove it or say why. A comment says the asset uses the first 64 KB (it is up to 100 KB).
- `tile_net.c`: `msg` fields copied when a decode fails (uninitialised read, harmless today); `fcntl()` result unchecked; WiFi reconnect has no back-off; no stack high-water mark recorded for the 4096 B Network Task.
- The switch record travels through the drop-oldest log queue; the next beacon repeats it.
- `tile_stress.c`: with both STRESS flags the TS functions compile unused.

### Confirmed sound by the reviewers

PostTri task table, priorities and cores; tcpip pinned to core 0; GPTIMER and RMT ISRs IRAM-safe; coordinator and tile timings; queue policies (Time Sync and Telemetry drop-oldest, Command and Upload reject with NACK); per-tick order swap, start RMT, prepare n + 1; brightness cap 8; Orientation 26-sample debounce; the playback timeline seqlock; `fault_word` ordering; `asset_rx` bounds; `common/proto` exact-length decoders; no new IRAM function. No invented measurement found: every unmeasured cell carries `[NEEDS MEASUREMENT]` or "Nothing has run yet".

### Verdict

Not ready to freeze. R1-1 to R1-3 fail under ordinary bench events (a lost datagram, a Pico restart, the 8-bit asset). Patch 17 does what the bench finding asked but is not enough on its own (R1-2, R1-4). Fix R1-1 to R1-3, R1-6 and R1-9 to R1-11, then run round 2 over the fixes only.

## Round 0: self-review of patches 09 to 15 (30 Oct 2026, Jia Jun's session)

**Not the independent review of continuation plan step 9.** The handoff prompt asks for two fresh reviewers (Tribunal fidelity; Barr-C and integration). This round is the implementing session checking its own work; it does not replace them. Tags as in the prompt: BLOCKING, SHOULD FIX, NIT.

### Found and fixed during the work (no longer open)

| Where | Finding | Fix |
|---|---|---|
| `tile_net.c` `tn_poll_pc()` (patch 11) | Upload replies went to the last sender on UDP 4214; a TELEMETRY_ACK from the dashboard's telemetry socket could redirect a CHUNK_ACK | only MANIFEST and ASSET_CHUNK set the reply address (patch 13) |
| `playback_control.c` `playback_control_state()` | read as a plain variable by `asset_store` and `tile_net` in other tasks, against coding standard interpretation 1 | atomic copy written with release, read with acquire (patch 15) |
| `tile_net.c` `tn_on_upload()` | T-X-08 case 3 (another session's manifest) was accepted | uploads for a session other than the beacons' are dropped and counted (patch 14) |
| `pc/` | no way to build or upload the OD-15 8-bit asset T-X-03 needs | `asset_builder.py --format 8bit`, `uploader.py --format 1` (patch 14) |
| `pc/tools/gen_week13_docs.py` | bit counts doubled the header | uses `body_bits()` alone; pytest added (patch 14) |
| `pc/tools/evidence_index.py` | procedure templates counted as evidence, so a readiness row turned Green with no run | documents that say "Nothing has run yet", and the index itself, are not evidence; pytest added (patch 14) |
| `tile_net.c` `tn_on_beacon()` (bench log) | beacons and commands accepted from any sender; two Picos on one SSID were mixed | coordinator lock: first sender kept, others dropped and counted (`foreign=`), switch after 2 s of silence (patch 17, PROVISIONAL; the bench must confirm with two Picos) |
| `.github/workflows/build.yml` | the new pytest needs Pillow, which CI did not install | `pillow==11.0.0` added (patch 14) |
| `panel_sync/test/test_telemetry_book.c` | T5 used a condition bit as an event, so it could not fail | event bits only; the end check now requires every event cleared (patch 09) |

### Open

| Tag | Where | Finding | Suggested fix |
|---|---|---|---|
| SHOULD FIX | `orientation.h` | QMI8658 register values come from the datasheet map and have never run | check `ORIENT imu ready` and the axes on a board before T-B5-06 |
| SHOULD FIX | `display_frame.h`, `orient_filter.h` | LED numbering, rotation direction and quadrant mapping are assumptions | settle in the T-B5-06 trials |
| SHOULD FIX | `asset_store.c` | the BUSY burst of T-B3-04 (1) relies on the Network Task draining the 8-slot Reply Queue at least every 20 ms; at the default 100 chunks/s that is about 2 replies per pass, but a faster `--rate` could drop BUSY replies (counted in `reply_drops`) | keep the T-B3-04 run at the default rate or raise the Reply Queue length |
| NIT | `pc/tools/skew_analyse.py`, `pc/tests/test_skew_analyse.py` | `pycodestyle` reports long lines and `l` names (existing files, not changed here) | owner's choice; CI does not run pycodestyle |
| NIT | `asset_store.c` | flash counters print only when no upload item arrives for 60 s | enough for the 10 min PLAYING check, where nothing arrives |

### Checks at the end of the round

`python3 tools/barr_check.py`: 64 files, 0 findings. `clang-format --dry-run --Werror`: exit 0. `make -C panel_sync/test`: timesync 4 of 4, fault word 5 of 5, playback 6 of 6, telemetry 5 of 5, asset 5 of 5, display 6 of 6. `make -C common/proto/test`: 7 of 7. `make -C ledwall_sync/test`: 14 of 14. `pytest -q tools/tests pc/tests`: 223 passed. ESP-IDF v5.3: the shipped build, `-DTN_RX_HOOK=2`, and the `FAULT_STRESS`, `TS_STRESS`, `DE_DUMP_FRAMES`, `DE_PROFILE`, `DE_TEST_SLOW_PREP`, `DE_TEST_FLASH_ERASE` and `TN_TEST_INJECT_FAULTS` builds all complete with 0 warnings; IRAM 16,383 of 16,384 B. Pico SDK 2.3.1 build complete. Nothing has run on a board since patch 09.
