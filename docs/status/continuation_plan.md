# SE15 continuation plan

Written 29 Oct 2026 at a planned safe stop of Ben's (Benjamin, B2) session. This session continued Ahsanul's Phase 1 work (`docs/HANDOVER.md`). Read this file first (updated 29 Oct 2026 after patch 09, T-B3-06; next is step 4); read `docs/HANDOVER.md` second for setup, rules and the open decisions table.

## 1. Where everything is

The handoff folder holds:

| Item | What it is |
|---|---|
| `Led_Matrix/` | The repo. Branch `b4/handover` (= `b4/phase1` + handover files) plus **every change from this session as uncommitted edits**. Nothing was committed or pushed. |
| `patches/01` to `07` | One patch per task, in order, made with `git diff --binary`. They apply in number order on top of `b4/handover`. |
| `inputs/` | The binding documents this session worked from: `SE15_PostTri1.docx` (and a text copy), `SE15_Implementation_Plan.md`, `commitment_inventory.md`, `critic_log.md`, `rubric_matrix.md`, `decision_briefs.md`, `w10_review_log.md` (the Week10_Kickoff review log), `SE15 (1).txt`, `inf2004_SE15_W6.pdf`. They are outside the repo on purpose (the repo is public). |
| `SE15_hardware_test_steps.txt` | Plain-text bench guide for the team. |
| `CONTINUATION_PLAN.md` | A copy of this file. |

`wifi_secrets.h` files are not in the folder (they hold the real router password). Copy `wifi_secrets.example.h` to `wifi_secrets.h` in `panel_sync/main/` and `ledwall_sync/` and fill them in.

## 2. Rules followed (keep following them)

- **No commits, no pushes** from Ben's session. Ahsanul reviews and commits. To turn the patches into branches: from `b4/handover`, `git apply patches/NN-*.patch`, commit on its own `b<n>/<task>` branch, repeat in order. Or keep the working tree as is and commit file groups by task using the table in section 3.
- Barr-C:2018 for all team C (`docs/coding_standard.md`); every new C file is in `tools/barr_scope.txt`; `python3 tools/barr_check.py` must report 0.
- PostTri constants frozen; no invented measurements (`[NEEDS MEASUREMENT]` plus a bench step); British English; no AI attribution in commits (`docs/HANDOVER.md` section 3).

## 3. Done in this session

| Patch | Task (owner) | What changed |
|---|---|---|
| 01 | HANDOVER steps 2 to 5 (Cheston, Jia Jun, Ahsanul) | `fault_word` review fixes (what release/acquire orders, counters must be atomic, how owner-only clearing is checked, question 1 wording, stale TELEMETRY note, holdover label). `tile_tasks` review fixes (core 1 also runs ipc1/IDLE1; IRAM-safe ISR still fires but the re-arm waits for flash; `vTaskList()` line is 47 B per the ESP-IDF v5.3 source, buffer raised to 64 B per line). Doc NITs (VC-1 citation, `render.c` references, bench checklist A1 to A4 done, `sizeof.txt`, status names). **Rule 1.4.b decided: comply** (230 operands parenthesised); `barr_check.py` now checks 1.4.b, 1.4.a mixing and 6.2.a (with tests); two over-long test functions split, output identical. |
| 02 | T-B2-01 additions (Benjamin) | SESSION_CONFIG gains `frame_count` and `crc32[9]` (46 B); `protocol/layout_table.md` section 10 items 9 to 13 and new section 11 (ports 4210 to 4215, TELEMETRY broadcast); Python twin, vectors and the C vector tool handle array fields. |
| 03 | T-B2-02 to T-B2-05 (Benjamin) | `ledwall_sync/coordinator_loop.c/.h` rewritten (one routine per pass, ring buffer, registry of 9, 2.5 s heartbeat scan, 10 s CONNECTING then FAULT, statechart 2, readiness and sync bitmaps, stale rejection, STATUS 250 ms, START_AT/RESUME_AT/PAUSE/STOP 3 copies, pause and continue policies, 5 s recovery, rejoin RESUME_AT, stall skip without burst, OD-01 exception check, callback/dispatch histograms, `CL_TEST_SUPPRESS_MS` for T-B4-05); `main.c` reduced; prototype HELLO, `layout_for()`, `pack_wall()`, Pico access point and `dhcpserver/` deleted; USB printf timeout 1 ms; host tests `ledwall_sync/test` and a CI step. |
| 04 | T-B3-03 completion, T-B3-01 (Cheston); HANDOVER steps 7 and 11 | `tile_net` rewritten: WiFi start, Node ID and `wifi_event` moved in; WiFi storage in RAM; REGISTER on first beacon and reconnect; HEARTBEAT 0.5 s staggered by Node ID; SYNC_READY with each heartbeat; session copied from beacons; Command Queue length 3 with NACK reason 2; 10 s CONNECTING timeout sets the WIFI bit; no cross-context shared variables. `panel_sync.c` reduced to `app_main` (Barr-C). `display_engine` waits for SYNC_READY before its first tick. `asset_store_probe_heap()`. `docs/week13/iram_budget.md`. Bench checklist Part D (D1 to D7). `.gitignore` gains `**/build2/`. |
| 05 | T-B4-04 (Ahsanul) | `playback_control.c/.h`: statechart 4 (9 states), once per (session_id, seq), saved frame at t_resume, STOP from PAUSED, CLOCK HOLDOVER after 200 ms and pause with BEACON_LOSS after 1 s, timeline seqlock for the Display Engine, task at priority 15 on core 0. Added `timesync_newest_age_us()`, `tile_net_has_ip()`, `asset_store_active()` (no asset until T-B3-04/05). D-1 file list extended to `playback_control`. |
| 06 | Benjamin's defence doc | `docs/coordinator_loop_DEFENCE.md`: 10 design choices with PostTri rows and examiner questions, PROVISIONAL list, 3 questions for Benjamin. |
| 07 | T-B2-06 (Benjamin) | `pc/tools/sim_nodes.py` (nine simulated tiles: REGISTER burst, then 0.5 s heartbeats, staggered or burst) and its pytest. |
| 08 | Ben's stop | This continuation plan. |
| 09 | T-B3-06 telemetry (Cheston), 29 Oct 2026, Jia Jun's session | `tile_net`: TELEMETRY {seq, fault word, orientation, display state} every 1 s as a subnet broadcast to UDP 4215 (`tn_telemetry_send()`), TELEMETRY_ACK on UDP 4214 read by the Network Task each pass (`tn_poll_ack()`, `tn_on_telemetry_ack()` calling `fault_word_clear_events()`), Telemetry Queue length 2 drop-oldest with the producer's drop count carried in each item (`tile_net_post_status()`), `TN_STATS` gains `tel_sent`, `tel_acked`, `ack_unknown`, `tel_q_dropped`; test build `TN_TEST_INJECT_FAULTS` for VC-4. New `telemetry_book.c/.h` (pending-ACK ring of 4, host-testable). `display_engine` reports INITIALISING, IDLE, WAITING FOR NEXT TICK or FAULT (orientation 0 until T-B5-05). Host tests T1 to T5 (T5: VC-4 drop-every-third simulation with the real `fault_word`). `pc/tools/telemetry_ack.py` (VC-4 receiver, `--drop-every 3`) and its pytest. Bench checklist Part E (E1 to E4). `.gitignore` gains `**/build_vc4/`. |

### Results at the stop (all run 29 Oct 2026 in this workspace)

| Check | Result |
|---|---|
| `python3 tools/barr_check.py` | 46 files, 0 findings |
| `clang-format --dry-run --Werror` (18.1.8) on the scope | exit 0 |
| `make -C panel_sync/test` and `asan` | timesync 4 of 4, fault word 5 of 5, playback 6 of 6; after patch 09 also telemetry 5 of 5 (`make`, macOS; `asan` not run there, it needs `setarch`) |
| `make -C common/proto/test` and `asan` | 7 of 7 |
| `make -C ledwall_sync/test` and `asan` | coordinator 14 of 14 |
| `pytest -q tools/tests pc/tests` | 170 passed; 177 after patch 09 |
| ESP-IDF v5.3 `panel_sync` and `-DTN_RX_HOOK=2` | complete, 0 warnings; DIRAM 112,739 B used (229,021 B free); IRAM 16,383 of 16,384 B. After patch 09 (macOS, ESP-IDF v5.3): both complete, 0 warnings, DIRAM 112,899 B, IRAM unchanged at 16,383 B; `TN_TEST_INJECT_FAULTS` build also complete |
| Pico SDK 2.3.1 `ledwall_sync` | complete, 0 warnings; text 347,704 B, bss 50,500 B (built at patch 03; patches 04 to 07 do not touch it) |
| `boardtest` | **not built** (needs the Espressif component registry, unreachable here); unchanged |
| Hardware | **nothing has run on a board** |

### Found and fixed on the way

- The Pico CI job would fail on `ubuntu-latest`: its newlib has no `PRIu64`, which the old `coordinator_loop.c` used. The new code uses no 64-bit format macro.
- The HANDOVER's hook-2 build folder `build2/` was not git-ignored.
- `tile_net` and `display_engine` read counters across tasks without atomics; both now keep counters inside one context.
- IRAM is 1 byte from full (16,383 of 16,384). Any new IRAM function must come with a removal; see `docs/week13/iram_budget.md`.

### Found on the bench, 29 Oct 2026 (Jia Jun, one tile and the Pico, not fixed)

- **Two coordinators on one SSID are mixed.** With a second Pico beaconing on the same router, a tile received about 17 beacons/s from two clocks (b4/phase1 build). `tile_net` still takes beacons from any sender (`tn_handle_record()` re-learns the Pico address whenever the sender changes, so it would also re-REGISTER on every flip). Bench rule until fixed: one Pico per SSID. Fix belongs to T-B2-04 / T-B3-03 (accept beacons from one coordinator or session only).
- **Tile ESP-4 (MAC 9c:13:9e:f1:2e:b0) boots into download mode on every software reset** from macOS (`rst:0x15 ... boot:0x0 (DOWNLOAD(USB/UART0))`); it runs only after a USB replug. Steps that say "press RESET" need a replug on that tile.

## 4. Decisions waiting for owners (PROVISIONAL)

| Owner | Decision | Where |
|---|---|---|
| Benjamin | SESSION_CONFIG `frame_count` + 9 CRC32; C to P commands reuse START_AT/PAUSE/RESUME_AT/STOP; ports 4210 to 4215 and TELEMETRY broadcast; seq streams; session_id 0 = no session | `protocol/layout_table.md` sections 10, 11 |
| Benjamin | Stall rule; link loss = FAULT until reset; Resume resynchronises first; pause-policy recovery returns to PAUSED; ring 32, 4 send slots; OD-01 only after `exc_max=0` on the bench | `docs/coordinator_loop_DEFENCE.md` |
| Cheston | After the CONNECTING timeout the tile keeps retrying with the WIFI bit set; heartbeat stagger and SYNC_READY with each heartbeat; Command Queue 3 | `panel_sync/main/tile_net.h` |
| Cheston (patch 09) | OD-11: telemetry period 1 s, Telemetry Queue 2 (Display Engine is its single producer), pending-ACK ring 4; no Fault queue (faults go through the fault word, RAR-1; length and policy still open); port 4214 polled by the Network Task in both OD-09 builds; VC-4 injection every 5 s of `FAULT_BIT_CRC_FAIL` | `panel_sync/main/tile_net.h`, `telemetry_book.h` |
| Benjamin (patch 09) | TELEMETRY uses the tile's single header seq stream (shared with REGISTER, HEARTBEAT, SYNC_READY, NACK) | `panel_sync/main/tile_net.h` |
| Jia Jun (patch 09) | Display Engine reports WAITING FOR NEXT TICK while the Week 10 loop ticks; orientation 0 until T-B5-05 | `display_engine.c` |
| Ahsanul | 1.4.b complied with; 6.2.a counted strictly; D-1 extended to `playback_control`; holdover entry at 200 ms; FAULT on invalid frame_count; START_AT/RESUME_AT accepted in READY and all playing states | `docs/coding_standard.md`, `playback_control.h` |
| Jia Jun | `vTaskList()` buffer 64 B per line; first tick waits for SYNC_READY; the Display Engine must read `playback_control_read_timeline()` in T-B5-02/03 | `tile_tasks.c`, `display_engine.c` |

## 5. Next steps, in order

1. **Review and commit what exists (Ahsanul).** Apply or commit patches 01 to 07 as in section 2. Done when every `b*/` branch builds in CI (the new `ledwall_sync` host-test step included).
2. **Hardware, Week 10 (team).** Follow `SE15_hardware_test_steps.txt` and `docs/week10/bench_checklist.md` Parts B0, D1 to D7, C1, A5 to A7, then Part B. Flash builds from patch 04 or later (they register and heartbeat; earlier builds do not). Run `python3 pc/tools/sim_nodes.py --pico <ip>` for the T-B2-06 load test. Done when the evidence files named in the checklist exist.
3. **DONE in patch 09 (bench Part E not run). T-B3-06 telemetry (Cheston).** `tile_net`: `tn_telemetry_send()` (TELEMETRY broadcast to port 4215 every telemetry period; OD-11 still open, propose 1 s), `tn_on_telemetry_ack()` on port 4214 calling `fault_word_clear_events()`, Telemetry Queue drop-oldest. Done when a host test of the send/ACK bookkeeping passes and the VC-4 drop-every-third test procedure is written.
4. **T-B3-04 and T-B3-05 asset path (Cheston).** `asset_store`: RAM copy by the OD-04 rule, pointer published once, `asset_store_flash_allowed()`, chunk receive on port 4214, verification, ASSET_READY, `asset_store_active()` returning the real session and frame_count, REGISTER's cached values. This unblocks Playback Control on a tile. Done when the host-testable parts pass and both tile builds compile.
5. **T-B5-02 and T-B5-03 frame path (Jia Jun).** `display_engine` reads `playback_control_read_timeline()` and the RAM asset; front/back buffers, swap, RMT on the tick, unpack into the back buffer; delete `DE_W10_TEST_PATTERN`, `frames.c/.h`, `gif_frames.h`. Measure IRAM again (`docs/week13/iram_budget.md` rule 4).
6. **PC side (Jolyn, can run in parallel, `pc/` only).** T-B1-02 `asset_builder.py` resample (settle OD-06 first), T-B1-03 pack and manifest, T-B1-04 uploader (ports 4214/4215), T-B1-05 dashboard and `pico_link.py` (ports 4212/4213, SESSION_CONFIG with frame_count and CRCs, commands as section 10 item 11), T-B1-06 numbered pattern.
7. **T-B5-05 orientation, T-X build options and procedures** (T-X-01 to T-X-09), then T-X-06 and T-B1-07 evidence documents.
8. **DEFENCE docs still missing:** `tile_net`, `playback_control`, `asset_store` (style of `docs/coordinator_loop_DEFENCE.md`).
9. **Independent review before Tribunal 2:** two fresh reviewers (Tribunal fidelity; Barr-C and integration) over everything; log in `docs/status/review_log.md` (that file does not exist yet; the Week10_Kickoff log is `inputs/w10_review_log.md`).
10. **Update `docs/status/overall.md` and `docs/HANDOVER.md`** with the new task states.

## 6. How to resume in a new Claude session

1. Put this whole folder where the session can read it (or zip it and attach it).
2. Give it `Led_Matrix/docs/prompts/SE15_Complete_All_Prompt.md` with these changes: the binding inputs are in `inputs/` instead of the Drive path; **do not commit** (or commit as yourself if you are the one committing, never as `WetzUmbrella` unless you are Ahsanul); keep one patch per task in `patches/`; update `docs/status/continuation_plan.md` after each task.
3. Then add: "Current state: read `CONTINUATION_PLAN.md`; continue from section 5, step 3."

To make the next patch: `cd Led_Matrix && git add -A && git diff --cached --binary > ../patches/08-name.patch` covers everything since `b4/handover`; to get only the new task, diff against a stash or keep a copy of the tree before starting (this session used a checkpoint script that staged after each patch and diffed staged against working tree).

### Tool setup in a fresh Linux workspace (what worked here)

- Python tools: `python3 -m venv ~/se15-venv && ~/se15-venv/bin/pip install clang-format==18.1.8 pytest==8.3.3 matplotlib`.
- Pico: `apt-get install gcc-arm-none-eabi libnewlib-arm-none-eabi libstdc++-arm-none-eabi-newlib`; Pico SDK `git clone -b 2.3.1 --depth 1 https://github.com/raspberrypi/pico-sdk.git` and `git submodule update --init --depth 1`; `export PICO_SDK_PATH=...`.
- ESP-IDF v5.3: `git clone -b v5.3 --depth 1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git`, `./install.sh esp32s3`. If `dl.espressif.com` is blocked: `python3 tools/idf_tools.py install-python-env --no-constraints`, then in the IDF Python env `pip install "idf-component-manager~=2.1"`, and always `export IDF_PYTHON_CHECK_CONSTRAINTS=no` before `. ~/esp/esp-idf/export.sh`.
- Checks: the command block in `docs/HANDOVER.md` section 2, plus `make -C ledwall_sync/test` and `make -C ledwall_sync/test asan`.
