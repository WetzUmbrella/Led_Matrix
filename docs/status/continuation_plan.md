# SE15 continuation plan (kept current during the session)

Last updated: 29 Oct 2026, after patch 05. Work continues from `docs/HANDOVER.md` (Ahsanul, end of Phase 1). Written in Ben's (Benjamin, B2) session, building on Ahsanul's working copy.

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

| 04 | T-B3-03 completion (Cheston), T-B3-01 (Cheston), HANDOVER steps 7 and 11 | `tile_net` rewritten: WiFi start, Node ID and `wifi_event` moved in from `panel_sync.c`; `esp_wifi_set_storage(WIFI_STORAGE_RAM)`; REGISTER on first beacon and every reconnect; HEARTBEAT 0.5 s staggered by Node ID; SYNC_READY with each heartbeat while ready; session copied from beacons; commands on 4210 into `q_command` (length 3), full = NACK reason 2; 10 s CONNECTING timeout sets the WIFI bit; no variable shared between contexts except through queues and the event group. `panel_sync.c` reduced to `app_main` (Barr-C, in scope). `display_engine` waits for `timesync_sync_ready()` before its first tick; its unused cross-task counter reader removed. `asset_store_probe_heap()` (T-B3-01) called once after the first IP. `docs/week13/iram_budget.md`. Bench checklist Part D (D1 to D7). `.gitignore` gains `**/build2/` (the HANDOVER hook-2 build folder was not ignored) | ESP-IDF v5.3 `panel_sync` build and `-DTN_RX_HOOK=2` build: complete, 0 warnings; DIRAM 112,739 B used, 229,021 B free; IRAM 16,383 of 16,384 B; barr_check 42 files 0; all host tests and pytest pass |

| 05 | T-B4-04 `playback_control` (Ahsanul) | W6 statechart 4, all 9 states; once per (session_id, seq), other session and older seq rejected; START_AT and RESUME_AT arm the timeline (saved frame at t_resume), PAUSE holds the frame, STOP from PAUSED; CLOCK HOLDOVER after 200 ms without beacons, pause and BEACON_LOSS after the 1 s holdover; timeline published by seqlock for the Display Engine; task at priority 15 on core 0 reading `tile_net`'s Command Queue. Added `timesync_newest_age_us()`, `tile_net_has_ip()`, `asset_store_active()` (reports no asset until T-B3-04/05, so a tile waits in WAITING FOR READINESS). D-1 file list extended to `playback_control` (Ahsanul to confirm) | playback host tests 6 of 6 (plain, asan); timesync 4 of 4 and OD-10 rule unchanged; both ESP-IDF builds 0 warnings; barr_check 46 files 0 |

### Found and fixed on the way

- The Pico CI job would fail on `ubuntu-latest`: its newlib has no `PRIu64`, which the old `coordinator_loop.c` used. The new code prints no 64-bit format.
- `tile_net` and `display_engine` counters are read across tasks without atomics (the `fault_word` review point). **Not fixed yet**, see step 5 below.

## Not verified here

- **`boardtest`**: not built here (its component comes from the Espressif registry, which this workspace cannot reach). This session did not change it.
- ESP-IDF in this workspace needs `export IDF_PYTHON_CHECK_CONSTRAINTS=no` before `. ~/esp/esp-idf/export.sh` (the constraints file host is blocked) and `idf-component-manager~=2.1` in its Python env.
- Nothing has run on hardware.

## PROVISIONAL decisions for owners to confirm

| Owner | Decision | Where |
|---|---|---|
| Benjamin | SESSION_CONFIG `frame_count` + 9 CRC32 (item 9); C to P commands reuse START_AT/PAUSE/RESUME_AT/STOP rows (item 11); ports and TELEMETRY broadcast (section 11); seq streams: Pico beacon, command and status counters, one counter per tile, REGISTER resets it (item 13); session_id 0 means "no session" | `protocol/layout_table.md` sections 10, 11 |
| Benjamin | Stall rule: skip whole missed beacon slots, no burst; Pico link loss mid-run goes to FAULT until reset; RESUME during PAUSED resynchronises first, pause-policy recovery returns to PAUSED; ring 32, send slots 4 | `ledwall_sync/coordinator_loop.c` header |
| Ahsanul | Rule 1.4.b: complied with, not interpreted away; 6.2.a counted strictly | `docs/coding_standard.md` |
| Cheston, Jia Jun | Their review round 1 fixes as written in patch 01 | `docs/fault_word_DEFENCE.md`, `docs/tile_tasks_DEFENCE.md` |

## Next steps, in order (resume here)

1. Done (patch 04): tile side of the new protocol.
2. Done (patch 04): start-up defect.
3. Done (patch 04): T-B3-01 heap probe.
4. Done (patch 04): IRAM budget doc.
5. Done except hardware (patch 04): Week 10 gate passes in software; bench steps D1 to D7 added.
6. **Week 13, plan section 7 order:** T-B4-04 done (patch 05). Next: T-B3-06 telemetry send/ACK (TELEMETRY broadcast on 4215, ACK on 4214); T-B3-04 `asset_store` RAM copy; T-B3-05 upload and validation; T-B5-02/03 frame path; T-B5-05 orientation; PC side T-B1-02 to T-B1-06 (`pc/` only, can run in parallel); T-B2-06 `pc/tools/sim_nodes.py`; T-X build options and procedures.
7. **DEFENCE docs:** `docs/coordinator_loop_DEFENCE.md` for Benjamin (choices, PostTri rows, examiner questions, 3 owner questions) once step 1 is in.
8. **Independent review:** two fresh reviewers (Tribunal fidelity; Barr-C and integration) over all patches; log in `docs/status/review_log.md`; loop to 0 BLOCKING.
9. **Hand back:** update `docs/HANDOVER.md` and `docs/status/overall.md`, refresh patches, zip the folder.

## Hardware runs owed (unchanged)

See `docs/week10/bench_checklist.md` and the plain-text guide `SE15_hardware_test_steps.txt` given to the team on 29 Oct.

## How to resume in a new Claude session

Give it this folder, `docs/HANDOVER.md`, this file and the binding inputs (`SE15_PostTri1.docx`, `Plan/`, `SE15 (1).txt`, `inf2004_SE15_W6.pdf`, `Week10_Kickoff/`). Tell it: continue from "Next steps" step 1 above; no commits and no pushes; one patch per task; keep this file current after each task.
