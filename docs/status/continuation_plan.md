# SE15 continuation plan (kept current during the session)

Last updated: 29 Oct 2026, after patch 03. Work continues from `docs/HANDOVER.md` (Ahsanul, end of Phase 1). Written in Ben's (Benjamin, B2) session, building on Ahsanul's working copy.

## Rules this session follows

- **Nothing is committed, nothing is pushed.** Every change is an uncommitted edit in this folder, on top of branch `b4/handover` (= `b4/phase1` + handover files). Ahsanul reviews and commits.
- One patch per task in `../patches/NN-name.patch` (outside the repo), made with `git diff --binary` against the previous checkpoint. To split into branches: start from `b4/phase1`, `git apply` the patches in number order, commit each on its own `b<n>/<task>` branch.
- Barr-C, PostTri constants, no invented measurements, British English: as in `docs/HANDOVER.md` section 3.

## Done in this session

| Patch | Task | What changed | Checks run |
|---|---|---|---|
| 01 | HANDOVER steps 2 to 5 (review round 1) | `fault_word` comment and DEFENCE fixes (Cheston's 6 items); `tile_tasks` DEFENCE fixes and `vTaskList()` line size corrected (47 B, buffer now 64 B per line, read from the ESP-IDF v5.3 source); doc NITs (VC-1 citation, `render.c` references, bench checklist A1 to A4, `sizeof.txt`, status name); rule 1.4.b decided as **comply** (every non-trivial `&&`/`\|\|` operand parenthesised, 230 places); `barr_check.py` now checks 1.4.b, 1.4.a mixing and 6.2.a (100 lines, strict count); two long test functions split | barr_check 0; clang-format 0; host tests unchanged output (diffed) |
| 02 | T-B2-01 additions (Benjamin) | SESSION_CONFIG gains `frame_count` and `crc32[9]` (46 B; OD-07 item 9); `layout_table.md` section 10 items 9 to 13 and new section 11 (ports 4210 to 4215, TELEMETRY broadcast); Python twin, vectors, C vector tool generalised for array fields | proto 7 of 7 (plain, asan); pytest 165 passed; C/Python vectors 0 mismatches |
| 03 | T-B2-02 to T-B2-05 (Benjamin) | `coordinator_loop.c/.h` rewritten: one routine per pass, ring buffer, registry[9], heartbeat scan 2.5 s, 10 s CONNECTING then FAULT, statechart 2 (12 states), readiness and sync bitmaps, stale session/seq rejection, STATUS 250 ms, START_AT/RESUME_AT/PAUSE/STOP triple sends, pause and continue policies, 5 s recovery, rejoin RESUME_AT (OD-14 a), stall handling (skip slots, no burst), OD-01 exception check, callback/dispatch histograms, `CL_TEST_SUPPRESS_MS` (T-B4-05); `main.c` reduced (plan 0.1); prototype HELLO, `layout_for()`, `pack_wall()`, Pico access point and `dhcpserver/` deleted; USB printf timeout 1 ms; host tests `ledwall_sync/test` (14 of 14, plain and asan); CI job step added | coordinator 14 of 14; Pico SDK 2.3.1 build 0 warnings (text 347,704 B, bss 50,500 B); barr_check 38 files 0 |

### Found and fixed on the way

- The Pico CI job would fail on `ubuntu-latest`: its newlib has no `PRIu64`, which the old `coordinator_loop.c` used. The new code prints no 64-bit format.
- `tile_net` and `display_engine` counters are read across tasks without atomics (the `fault_word` review point). **Not fixed yet**, see step 5 below.

## Not verified here

- **ESP-IDF builds of `panel_sync` and `boardtest`** after patch 01 (two comment lines and one buffer size in `tile_tasks.c`). ESP-IDF v5.3 is now installed in this workspace (`. ~/esp/esp-idf/export.sh`); run the build before relying on it.
- Nothing has run on hardware.

## PROVISIONAL decisions for owners to confirm

| Owner | Decision | Where |
|---|---|---|
| Benjamin | SESSION_CONFIG `frame_count` + 9 CRC32 (item 9); C to P commands reuse START_AT/PAUSE/RESUME_AT/STOP rows (item 11); ports and TELEMETRY broadcast (section 11); seq streams: Pico beacon, command and status counters, one counter per tile, REGISTER resets it (item 13); session_id 0 means "no session" | `protocol/layout_table.md` sections 10, 11 |
| Benjamin | Stall rule: skip whole missed beacon slots, no burst; Pico link loss mid-run goes to FAULT until reset; RESUME during PAUSED resynchronises first, pause-policy recovery returns to PAUSED; ring 32, send slots 4 | `ledwall_sync/coordinator_loop.c` header |
| Ahsanul | Rule 1.4.b: complied with, not interpreted away; 6.2.a counted strictly | `docs/coding_standard.md` |
| Cheston, Jia Jun | Their review round 1 fixes as written in patch 01 | `docs/fault_word_DEFENCE.md`, `docs/tile_tasks_DEFENCE.md` |

## Next steps, in order (resume here)

1. **Tile side of the new protocol (T-B3-03 completion, Cheston):** move WiFi start and `wifi_event` into `tile_net` (Node ID from NVS, `esp_wifi_set_storage(WIFI_STORAGE_RAM)`, 10 s CONNECTING then FAULT + WIFI bit); learn the Pico address from beacon source; REGISTER on join and reconnect; HEARTBEAT every 0.5 s offset (Node ID - 1) x 500 / 9 ms; copy session_id from beacons into outgoing messages; resend ASSET_READY and SYNC_READY with each heartbeat while true (no ACK row exists); dispatch commands on 4210 into `q_command` (length 3, full = NACK reason 2 to the Pico on 4211); counters into the log record (no cross-task reads).
2. **Start-up defect (HANDOVER step 11):** `display_engine` waits for `timesync_sync_ready()` before its first tick.
3. **T-B3-01 heap probe:** `asset_store.c` with `asset_store_probe_heap()` called from `tile_net` after `IP_EVENT_STA_GOT_IP`.
4. **IRAM budget doc (HANDOVER step 7):** `docs/week13/iram_budget.md` from HANDOVER section 4.1.
5. **Week 10 gate:** ESP-IDF builds (`panel_sync`, `-DTN_RX_HOOK=2`, `boardtest`), Pico build, every host test, pytest, barr_check; add bench steps for T-B2-02 (10 min lateness, `exc_max` 0), T-B3-03 (spread log both hooks), T-B3-01, T-B3-02 to `docs/week10/bench_checklist.md`.
6. **Week 13, plan section 7 order:** T-B4-04 `playback_control` (needs statechart 4, W6 PDF p.22 to 23); T-B3-06 telemetry send/ACK (TELEMETRY broadcast on 4215, ACK on 4214); T-B3-04 `asset_store` RAM copy; T-B3-05 upload and validation; T-B5-02/03 frame path; T-B5-05 orientation; PC side T-B1-02 to T-B1-06 (`pc/` only, can run in parallel); T-B2-06 `pc/tools/sim_nodes.py`; T-X build options and procedures.
7. **DEFENCE docs:** `docs/coordinator_loop_DEFENCE.md` for Benjamin (choices, PostTri rows, examiner questions, 3 owner questions) once step 1 is in.
8. **Independent review:** two fresh reviewers (Tribunal fidelity; Barr-C and integration) over all patches; log in `docs/status/review_log.md`; loop to 0 BLOCKING.
9. **Hand back:** update `docs/HANDOVER.md` and `docs/status/overall.md`, refresh patches, zip the folder.

## Hardware runs owed (unchanged)

See `docs/week10/bench_checklist.md` and the plain-text guide `SE15_hardware_test_steps.txt` given to the team on 29 Oct.

## How to resume in a new Claude session

Give it this folder, `docs/HANDOVER.md`, this file and the binding inputs (`SE15_PostTri1.docx`, `Plan/`, `SE15 (1).txt`, `inf2004_SE15_W6.pdf`, `Week10_Kickoff/`). Tell it: continue from "Next steps" step 1 above; no commits and no pushes; one patch per task; keep this file current after each task.
