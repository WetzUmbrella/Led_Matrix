# SE15 overall status

Sections 0 to 8 are the Phase 0 snapshot of 29 Oct 2026 (read only, from `b4/wire-w10`). Section 9 records what Phase 1 changed; `docs/HANDOVER.md` on `b4/handover` is the current picture.

Written 29 Oct 2026 by Ahsanul (Buddy 4) from `b4/wire-w10` at `95aaf07`, the integration head that contains every other `b4/*` branch. `master` and `origin/master` are at `3950a10`; nothing from `b4/*` is merged or pushed.

Sources: `SE15_PostTri1.docx` (PostTri, read through `Plan/commitment_inventory.md`, which was built from it after its last change), `SE15 (1).txt` (Week 6 feedback), `Plan/SE15_Implementation_Plan.md` (plan), `Plan/critic_log.md` (rulings), `Week10_Kickoff/` (decision briefs, bench checklist, review log), the repo.

Labels: **Done** = code and acceptance both met; **Done in code** = code meets the row, the acceptance needs hardware and has not run; **Partial** = part of the row is coded; **Not started**. Nothing in this file is a measurement unless a log file is named.

## 0. Branches

| Branch | Ahead of `master` | Merged to `master` | In `b4/wire-w10` |
|---|---|---|---|
| `b4/barr-foundation` | 6 | no | yes |
| `b4/timesync-barr` | 11 | no | yes |
| `b4/od10-sync-ready` | 13 | no | yes |
| `b4/dep-common-proto` | 9 | no | yes |
| `b4/dep-coordinator-loop` | 10 | no | yes |
| `b4/dep-tile-tasks` | 7 | no | yes |
| `b4/dep-tile-net` | 21 | no | yes |
| `b4/dep-display-engine` | 24 | no | yes |
| `b4/wire-w10` | 28 | no | (head) |
| `origin/b4/cr1-baseline`, `origin/b4/timesync`, `origin/b4/skew-tools`, `origin/render-module`, `origin/router-mode` | 0 | yes | yes |

Tags: `cr1-before` (annotated, object `8353405`, on `45e038c`, pushed), `v0-esp-now-single-node`.

The `barr_check` fix is in the history twice (`38b839a` and its cherry-pick `38b0572`), with an identical change; the merge has no conflict.

Git checks: all 28 commits on `master..b4/wire-w10` are authored and committed by `WetzUmbrella <ahsanulpro@gmail.com>` with no trailer. The local commit-msg hook rejects attribution trailers; it was tested with a throwaway commit (exit 1, HEAD unchanged).

## 1. Plan tasks

`barr` = file is in `tools/barr_scope.txt` and passes `barr_check` (0 findings) and `clang-format --dry-run --Werror` (18.1.8). "n/a" = no C file yet.

### 1.1 Week 10 path

| Task | Owner | Status | Evidence | Unmet dependencies or gaps | barr |
|---|---|---|---|---|---|
| T-B4-01 tag and before sizes | Ahsanul | Partial | Tag checks 3 of 3: `git rev-list -n 1 cr1-before` = `45e038ca...`; first `proto_put_bits` commit `10159e8` descends from the tag; tag grep returns 0 lines. Host `sizeof` 50 B / 9 B (`docs/week13/before/sizeof.txt`); derived rows `docs/week13/before_after.md:40-42,80-95`. | Blank measured cells `before_after.md:16-17,22-28`. S-1 (`idf.py size` at the tag), S-2 (Xtensa `sizeof`) and S-3 (Pico size at the tag) need no hardware and have not been run. B-1 to B-3 need tiles, Pico, router. | n/a (docs) |
| T-B2-01 `common/proto` | Benjamin | Partial | `common/proto/proto.c/.h`: `proto_put_bits()`, `proto_get_bits()`, common header, BEACON encode/decode, `proto_beacon_set_t_p()`; `common/proto/test/` 3 of 3. | No `protocol/layout_table.md`; no `proto_put_u16_le/u32_le/u64_le` or `proto_get_*_le`; 17 of 18 message rows missing; boundary set covers BEACON fields only (no frame index, slew, fault word, bitmap). Files named `proto.c/.h`, not the plan's `proto_layout.c/.h` (recorded PROVISIONAL in `proto.h:19`; no DEFENCE doc yet). OD-05, OD-07 open. | yes |
| T-B1-01 `pc/proto_layout.py`, vectors | Jolyn | Not started | No `pc/proto_layout.py`, `pc/gen_vectors.py`, `protocol/vectors/`. | T-B2-01 table. | n/a |
| T-B3-01 heap probe | Cheston | Not started | Only the tag-build patch `docs/week13/before/heap_probe.patch`. No `asset_store_probe_heap()`. | Hardware for acceptance. | n/a |
| T-B3-02 flash check | Cheston | Partial (software half) | Built `sdkconfig` (this session, fresh) has `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`; `partitions.csv:4-5`: factory 0x10000 + 0x180000 ends 0x190000, frames 0x200000 ends 0x390000, inside 0x400000. | `esptool.py flash_id` on 5 tiles; `docs/week10/flash_check.log` does not exist. | n/a |
| T-B5-04 `tile_tasks.h`, sdkconfig | Jia Jun | Done in code | `panel_sync/main/tile_tasks.h` six rows; built `sdkconfig` has all six options and PM unset. Display (`display_engine.c:86`), Network (`tile_net.c:95`), Time Sync (`panel_sync.c:126`) use `xTaskCreatePinnedToCore` with the table. | Boot log and `vTaskList()` (hardware). CPU frequency 160 MHz read from the built `sdkconfig` (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=160`, wrap 26.8 s) but not yet written into the boot log or the header. Stacks 4096 B unmeasured. | yes |
| T-B5-01 `display_engine` tick | Jia Jun | Done in code | `display_engine.c`: gptimer created in `de_task` on core 1 (`:306`), one-shot per tick from `timesync_deadline_local()`, test pin GPIO4, missed counter (`:64`), `DE_W10_TEST_PATTERN`, 1 ms / 4 ms marker every 64 ticks (`:44-46`). | 30 min logic analyser acceptance. `render.c/.h` are no longer built but still in the tree (section 7, item 2). Test pin GPIO4 PROVISIONAL. | yes |
| T-B3-03 `tile_net` | Cheston | Partial | `tile_net.c`: BEACON decode, t_l at the OD-09 hook (`TN_RX_HOOK` 1 or 2, both build), drop-oldest Time Sync Queue length 4 with drop counter, seq-gap counter, spread log. Packed and `recvfrom` greps over `panel_sync/main` return 0 lines. | No `q_command` (length 3) and no NACK; no heartbeat or Node ID stagger; `wifi_event` still in `panel_sync.c:51-61` (plan moves it to `tile_net`). 10 min acceptance (hardware). | yes |
| T-B4-02 `timesync` | Ahsanul | Done in code | `timesync.c/.h`, host tests 4 of 4 (plain and asan); `lock` and `get_render_state` gone from built code; OD-10 rule coded (`67266de`). | On-target behaviour only through T-B4-03. | yes |
| T-B2-02 `coordinator_loop` | Benjamin | Partial | `coordinator_loop.c`: poll library, `cyw43_arch_poll()` each pass, `next_beacon_us += 100000`, t_p read immediately before `udp_sendto`, lateness histogram; `FPS`/`frame_no` removed. | Acceptance grep fails: `ledwall_sync/main.c:42-46` still has a packed `hello_t`. OD-01 citation not in the header or `layout_table.md`. 10 min bench run. | yes (`main.c` out of scope) |
| T-B4-03 two-tile skew rig | Ahsanul | Partial (build ready) | Tile and Pico rig builds compile; `pc/tools/skew_analyse.py` with marker mode, pytest green; `docs/week10/skew_2tile/RIG.md`. | No capture. Bench Part B written in the Drive copy of `bench_checklist.md` only (section 7, item 4). | n/a |

### 1.2 Week 13 and Final

| Task | Owner | Status | Evidence or nearest code | Unmet dependencies |
|---|---|---|---|---|
| T-B5-02 no tile scaling | Jia Jun | Not started | `render.c:19,43` `BRIGHT`/`dim()` still in the tree, not built | T-B1-01, T-B3-04, OD-15 |
| T-B5-03 tick order, buffers | Jia Jun | Not started | `display_engine` has no front/back buffers | T-B5-02, T-B3-06, OD-16 |
| T-B3-04 `asset_store` | Cheston | Not started | `frames.c` (not built) is the old mmap path | T-B3-01, T-B3-02 acceptance; OD-04 |
| T-B2-03 bounded loop, registry | Benjamin | Not started | `main.c:40-153` prototype registry, `MAX_NODES 8`, `layout_for()`, `pack_wall()`, unbounded `hello_recv`, endless `router_connect()` | OD-08 ruled; OD-18 |
| T-B4-04 `playback_control` | Ahsanul | Not started | `timesync_frame_index()`, `timesync_sync_ready()` exist | T-B2-05, T-B3-03 Command Queue, T-B3-06 |
| T-B1-02 `asset_builder` resample | Jolyn | Not started | `panel_sync/tools/make_frames.py` (30 fps, mean delay) | OD-06 |
| T-B1-03 pack and manifest | Jolyn | Not started | none | T-B1-01, T-B1-02, OD-02, OD-05 |
| T-B2-04 session, readiness | Benjamin | Not started | none | T-B2-03, OD-14 |
| T-B2-05 START_AT and commands | Benjamin | Not started | none | T-B2-04 |
| T-B3-05 upload, validation | Cheston | Not started | none | T-B3-04, T-B1-03 |
| T-B3-06 `fault_word` | Cheston | Not started | none | T-B1-05 (for the ACK end), OD-11, OD-12 |
| T-B1-04 uploader | Jolyn | Not started | `upload_frames.sh` (USB) | T-B1-03, T-B3-05 |
| T-B1-05 dashboard | Jolyn | Not started | none | T-B2-04, OD-02 |
| T-B1-06 numbered pattern | Jolyn | Not started | none | T-B1-03 |
| T-B5-05 orientation | Jia Jun | Not started | `boardtest.c:41-47` IMU probe only | T-B3-06 |
| T-B5-06 A9 trials | Jia Jun | Not started | none | T-B5-05 |
| T-B5-07 display timing | Jia Jun | Not started | none | T-B5-03 |
| T-B5-08 power and thermal | Jia Jun | Not started | none | T-B5-02, OD-22 |
| T-B2-06 nine-sender load | Benjamin | Not started | none | T-B2-03 |
| T-B2-07 router 30 min | Benjamin | Not started | none | T-B2-03, T-B3-03 |
| T-B4-05 holdover sizing | Ahsanul | Not started | `TIMESYNC_HOLDOVER_US` 1 s (`timesync.h:71`) | T-B4-04, T-B4-03, `CL_TEST_SUPPRESS_MS` |
| T-B4-06 recovery timing | Ahsanul | Not started | none | T-B2-04, T-B3-05, T-B4-04 |
| T-B4-07 two-way fallback | Ahsanul | Not triggered | runs only on a skew result above 5 ms | T-B4-03, OD-20 |
| T-X-01 packed round trip | Jolyn | Not started | none | T-B1-04, T-B3-05, T-B5-02 |
| T-X-02 fault-word stress | Cheston | Not started | none | T-B3-06 |
| T-X-03 tick timing before/after | Jia Jun | Not started | none | T-B5-03, T-B1-03, OD-15 |
| T-X-04 re-upload while playing | Benjamin | Not started | none | T-B1-04, T-B2-03, T-B4-03, T-B3-05 |
| T-X-05 torn-read stress | Ahsanul | Not started | host forced-interleaving test in `panel_sync/test/test_timesync.c` only; no `TS_STRESS` build | T-B5-01 merged in `b4/wire-w10` |
| T-X-06 Week 13 evidence pack | Benjamin | Not started | `docs/week13/before_after.md` (before column partly filled) | T-X-01, T-X-03, T-X-04, T-X-08 |
| T-X-07 three-tile skew | Ahsanul | Not started | none | T-B4-03, T-B4-04, OD-21 |
| T-X-08 upload fault cases | Cheston | Not started | none | T-B3-05, T-B1-04, T-B2-04, T-B1-05 |
| T-X-09 five-tile soak | Cheston | Not started | none | T-X-07, T-B2-07, T-X-01 |
| T-B1-07 evidence index | Jolyn | Not started | none | T-X-06, OD-23, OD-24 |

Totals over 44 tasks: 0 Done, 3 Done in code (T-B5-04, T-B5-01, T-B4-02), 6 Partial (T-B4-01, T-B2-01, T-B3-02, T-B3-03, T-B2-02, T-B4-03), 34 Not started, 1 Not triggered (T-B4-07).

## 2. PostTri commitments

The inventory (183 in-scope items) audited `45e038c`. The table lists every item whose state has changed on `b4/wire-w10`. Every other in-scope item keeps its inventory status and is listed by ID after the table.

| ID | Task | State now | Contradiction left |
|---|---|---|---|
| VA-2.2, W6D-5a (beacon part) | T-B2-02 | Done in code: `next_beacon_us += 100000` | none |
| VA-2.9 | T-B2-02 | Done in code: t_p read immediately before `udp_sendto` | none |
| VA-2.6 | T-B2-02 | Partial: lateness histogram exists, unmeasured | none |
| VA-2.7 | T-B3-03, T-B2-03 | Missing: the rig tile sends no heartbeat; the Pico 5 s expiry was removed with `expire_nodes()` | none |
| VA-3.7 | T-B3-03, T-B3-06 | Partial: Time Sync Queue drop-oldest with counter; Telemetry Queue missing | none |
| VA-4.1 | T-B4-02 | Done in code: 16-beacon minimum-delay filter, host test 2 | none |
| VA-4.3, W6D-2b | T-B4-04 | Missing: 1 s holdover is only a SYNC_READY input; no pause on beacon loss | no longer Contradicts (`render.c` not built) |
| VA-4.5, W6D-1b | T-B5-01, T-B4-02 | Done in code | none |
| VA-4.6, VA-4.7 | T-B4-02 | Done in code: slew at most 100 us per tick, drift held with offset | none |
| VA-5.1, W6D-4b | T-B5-04 | Done in code (built `sdkconfig`) | none |
| VA-5.2 | T-B5-01, T-B5-04 | Done in code: gptimer from the core-1 task, IRAM-safe | none |
| VA-5.3, W6D-3a, SRM-1M, IFM-5b | T-B5-03 | Missing (prototype not built, replacement not written) | none |
| W6D-1a, W6B-10 | T-B4-02, T-B4-04 | Partial: `timesync_frame_index()` integer formula exists; no playback uses it | none |
| W6D-1e | T-B5-01, T-B2-02, T-B1-02 | Partial: tile `DE_FPS 60`, Pico 30 fps gone | `make_frames.py:16` still 30 fps |
| W6D-2d | T-B2-03, T-B3-05 | Contradicts | Pico `router_connect()` retries forever (`main.c:142-153`); tile `wifi_event` retries forever (`panel_sync.c:51-58`) |
| W6D-2f, IFM-2a | T-B2-01, T-B2-05 | Partial: BEACON carries session_id (0, PROVISIONAL) and seq | no stale rejection on the tile |
| W6D-4a | T-B5-04 | Partial: Display 20 core 1, Time Sync 16 core 0, Network 12 core 0 created from the table | none |
| W6D-5b, SRM-7M (callbacks) | T-B2-03 | Contradicts | `hello_recv` (`main.c:102-138`) still updates the registry and prints inside the callback |
| W6D-6a, VC-5, RAR-4 | T-B3-04, T-B3-05 | Missing: no asset path at all in the rig build | no longer Contradicts (`frames.c` not built) |
| SRM-2Ma | T-B5-03 | Done in code for the rig: `led_strip_new_rmt_device()` called in `de_task` on core 1 (`display_engine.c:351`) | none |
| SRM-3M | T-B3-04, T-B5-04 | Partial: tick and RMT ISRs IRAM-safe; no RAM asset; no `esp_wifi_set_storage` | none |
| SRM-4M | T-B3-03, T-B5-04 | Done in code: Display core 1, tcpip core 0, both hooks build | OD-09 measurement open |
| IFM-2b | T-B2-02, T-B2-05 | Partial: BEACON {session_id, seq, t_p}; commands missing | none |
| IFM-3a | T-B3-03 | Done in code: only `tile_net` opens sockets (`tile_net.c:248,312`) | none |
| IFM-4a, IFM-4b, VB-4c, LDC-5, VC-1, RAR-3 | T-B4-02 | Done in code: seqlock over offset, drift, reference, 3 retries then SYNC_STALE; `display_engine` reads through it | core-1 reader not stress-tested (T-X-05) |
| IFM-5a | T-B5-01 to T-B5-03 | Partial: deadline from `timesync`; no asset, orientation, timing log or display fault | none |
| VB-2a, LDC-4, CB-2 | T-B2-01, T-B2-02 | Partial: BEACON by shifts and masks | packed `hello_t` at `ledwall_sync/main.c:42` |
| VB-2c, LDC-3 | T-B2-02 | Done in code, PROVISIONAL (OD-01 citation gap) | none |
| VB-4a, LDC-6 | T-B2-01, T-B2-02 | Partial: t_p 64-bit on the wire; data representation table missing | none |
| VC-2 | T-B4-02, T-B2-01 | Partial: `int8_t` slew coded and host-tested; no table row | none |
| CB-4 | T-B4-02, T-B2-01 | Partial: seqlock and 64-bit time coded | 10-bit frame_count not enforced on the tile |
| VC-6, CB-1 | T-B5-02 | Missing in the build | `render.c:19,43` still in the tree, so the T-B5-02 grep fails |
| VC-8a | T-B5-04 | Partial: PM unset; 160 MHz in built `sdkconfig`, not logged | none |
| VC-11b | T-B2-03 | Contradicts | `MAX_NODES 8` at `ledwall_sync/main.c:40` |
| W6B-1 | T-B2-03, T-B2-04 | Contradicts | `layout_for()` and `pack_wall()` still compiled (`main.c:54-79`) |
| W6B-2 | T-B2-03, T-B3-05 | Regressed from Done to Missing in the rig build: the tile sends no HELLO and no REGISTER, so the Pico never learns its Node ID | Not needed by the rig; needed again by T-B2-03 |
| W6B-3 | T-B2-04 | Missing: re-split on loss removed with `expire_nodes()`; no policy yet | none |
| W10-1 | T-B4-01 | Done | none |
| W10-4 | T-B4-03 | Partial: rig build ready, not run | none |

Unchanged since the inventory (status as in `commitment_inventory.md`): VA-1.1 to VA-1.7, VA-2.1 (Done), VA-2.3, VA-2.4, VA-2.5, VA-2.8, VA-2.10, VA-3.1 to VA-3.6, VA-4.2, VA-4.4, VA-5.4 to VA-5.6, W6D-1c, W6D-1d, W6D-2a, W6D-2c, W6D-2e, W6D-2g, W6D-3b, W6D-3c, W6D-4c, W6D-5c, W6D-6b to W6D-6d, W6D-7a to W6D-7d, SRM-1E, SRM-2Mb, SRM-2Mc, SRM-2E, SRM-3E, SRM-4E, SRM-6M, SRM-6E, SRM-7E, SRM-8Ma to SRM-8E, SRM-9M, SRM-9E, IFM-1a to IFM-1c, IFM-2c, IFM-3b, IFM-3c, IFM-4c, IFM-5c, IFM-6a to IFM-6c, IFM-7, A1 to A9, VB-1, VB-2b (Done), VB-3a, VB-3b, VB-4b, VB-5a to VB-5c, LDC-1, LDC-2, LDC-7 (Done), LDC-8, RAR-1, RAR-2, R1 to R6, STP-1 to STP-5, CB-0, CB-3, CB-5, W10-2, W10-3, W10-5, W10-6, VC-3, VC-4, VC-7, W6B-4 to W6B-9, W6B-11 (Done). Out of scope: LDC-9, VC-8 to VC-12 (except VC-8a, VC-11a, VC-11b), OUT-1 to OUT-10.

## 3. Week 6 feedback (`SE15 (1).txt`)

| Bullet | What closes it | State |
|---|---|---|
| Power: cap brightness, check USB-C pack and heating over 30 min | T-B5-08, OD-22; cap on the PC (T-B1-03, T-B1-05, OD-02) | Open |
| NFR2: no free-running 60 Hz timer; tick at origin + n/60 s in corrected Pico time, re-armed per frame; target 5 ms | T-B4-02, T-B5-01 (coded); T-B4-03 (measurement) | Coded; skew unmeasured |
| Statechart 5: transmit the prepared buffer on the tick, then prepare; one back-buffer owner; swap in a critical section | T-B5-03 | Open |
| Clock sync: period, transport, min-delay filter, drift, tolerances; early timestamp; power save off; measure jitter (A6) | T-B4-02 (filter 16, drift, slew 100 us, coded), T-B2-02 (100 ms absolute, coded), T-B3-03 (OD-09 hook switch, spread log), power save off (`panel_sync.c:109`, Pico `main.c:152`) | Coded; spread and skew unmeasured |
| Pico loop design: event loop, timers, periods, servicing mode, worst-case handling | T-B2-02 (coded), T-B2-03 (bound, scan), T-B2-06 (load), OD-01 | Partial |
| Numeric priorities, WCET, cores; Telemetry and Fault queues; queue lengths and policies | T-B5-04 (coded), OD-11 (Time Sync 4 coded; Command 3 not coded; rest open), T-B3-06, OD-23 | Partial |
| Flash stalls: load the asset into RAM at ASSET_READY | T-B3-01, T-B3-04, OD-04 | Open |
| Untestable wording (NFR5, NFR8, A9, "repeated"); FR18 vs pause policy | T-B5-07, T-B4-06, T-B5-06, T-B2-04 (policy), T-B1-05 | Open |
| Upright mounting: accelerometer only | T-B5-05, README statement | Open |
| One readiness authority and session-ID agreement | T-B2-04, T-B1-05 | Open |
| System statechart: CONNECTING timeout, STOP from PAUSED, recovery timeout, holdover, START_AT lead; freeze the protocol | T-B2-01 (table), T-B2-03, T-B2-04, T-B2-05, T-B3-05, T-B4-04 | Open; W6D-2d still Contradicts |
| Key risk 1 skew; closing line "build a two-tile skew rig this week" | T-B4-03 | Build ready, not run |
| Key risk 2 nine tiles plus PC and Pico on one router | T-B2-07 (5 tiles), T-B2-06 (simulated nine) | Open |
| Key risk 3 Pico timeliness under load | T-B2-06 | Open |
| Key risk 4 display jitter with WiFi and flash | T-B5-07 | Open |
| Key risk 5 rejoin within 2 s | T-B4-06, OD-14 | Open |

## 4. Open decisions

| OD | Owner | State | Blocks |
|---|---|---|---|
| OD-01 poll mode | Benjamin | PROVISIONAL: poll coded (`ledwall_sync/CMakeLists.txt:10`); citation gap per the brief; `__get_current_exception()` log not coded | T-B2-02 acceptance |
| OD-02 brightness enforcement | Jolyn, Jia Jun | Open (no brief) | T-B1-03, T-B1-05 |
| OD-03 flash size | Cheston | Open; built `sdkconfig` says 4 MB, `esptool` not run | T-B3-05 |
| OD-04 SRAM or PSRAM | Cheston | Open | T-B3-04 |
| OD-05 nibble order | Jolyn, Jia Jun | Open; brief recommends (a), vector `21 43 65` | T-B2-01 freeze, T-B1-03, T-B5-02 |
| OD-06 GIF delay rule | Jolyn | Open (no brief) | T-B1-02 |
| OD-07 field widths | Benjamin | PROVISIONAL: type 5, session_id 8, seq 16 bits coded (`proto.h:29-36`) | T-B2-01 freeze |
| OD-08 loop bound | ruled Round 0 (one routine per pass, 1 ms) | Settled by ruling | none |
| OD-09 RX hook | Cheston, Ahsanul | PROVISIONAL switch `TN_RX_HOOK`, default (a); both variants build | T-B3-03 acceptance |
| OD-10 SYNC_READY | Ahsanul | PROVISIONAL (owner Ahsanul), coded (`67266de`): window full, drift available, newest sample within the 1 s holdover; settling data from T-B3-03 and T-B4-03 pending | T-B4-04 |
| OD-11 queue lengths | Cheston | PROVISIONAL Time Sync 4 (coded); Command 3 (brief only, not coded); Upload, Telemetry, Fault, telemetry period open | T-B3-03, T-B3-06 |
| OD-12 fault bits, `EVENT_MASK` | Cheston | Open | T-B3-06 |
| OD-13 re-upload rate | Jolyn, Benjamin | Open | T-X-04 |
| OD-14 rejoin message | Benjamin, Ahsanul | Open | T-B2-04 |
| OD-15 8-bit baseline flag | ruled Round 0; flag detail Ahsanul with Jia Jun | Ruling settled; detail open | T-B5-02, T-X-03 |
| OD-16 RMT DMA or ISR | Jia Jun | Open | T-B5-03 |
| OD-17 lecturer: pixels as application data, UDP for MQTT | Ahsanul asks | Open | T-X-06 |
| OD-18 heartbeat 2.5 s or 3 s | Benjamin | Open (after T-B2-07) | T-B2-03 uses 2.5 s primary |
| OD-19 holdover beyond 1 s | Ahsanul | Open (after T-B4-05) | none now |
| OD-20 two-way design | Ahsanul, Benjamin | Not triggered | T-B4-07 |
| OD-21 partial layouts | Jolyn, Benjamin | Round 0 ruling; 2-tile form open | T-X-07, T-X-09 |
| OD-22 thermal limits | Jia Jun | Open | T-B5-08 |
| OD-23 WCET scope | Jia Jun | Open | T-B1-07 |
| OD-24 Tribunal 2 date | Ahsanul asks | Open: Mon 23 Nov 2026 given by Ahsanul, to be confirmed on xSITe; a Gmail draft exists without a To address (W10 review log I-6) | T-B1-07 |

## 5. Bench evidence

Nothing has run on hardware. Every artefact below is missing unless marked.

| Needed by | Artefact | Exists |
|---|---|---|
| T-B4-01 | `docs/week13/before/sizeof.txt` (host) | yes |
| T-B4-01 | `idf_size.txt`, `sizeof_xtensa.txt`, `pico_size.txt` (no hardware needed) | no |
| T-B4-01 | `heap_at_tag.log`, `onair_before.pcapng`, `onair_before_beacon.txt`, `onair_before_hello.log` | no |
| T-B3-01 | `docs/week10/heap_after_wifi.log` | no |
| T-B3-02 | `docs/week10/flash_check.log` | no |
| T-B5-04 | boot log with `vTaskList()` | no |
| T-B5-01 | 30 min test-pin capture, screenshot, counter log | no |
| T-B3-03 | `docs/week10/beacon_spread_10min.csv` (both OD-09 hooks) | no |
| T-B2-02 | lateness histogram CSV and plot, Pico seq log, one beacon capture | no |
| T-B4-03 | `docs/week10/skew_2tile/` capture, `skew_pairs.csv`, `skew_plot.png`, `skew_report.txt`, `bench_log.md`, `wiring.jpg`, video | no |
| Week 13 tasks | every artefact in their plan rows | no |

## 6. Test and build results (this session, fresh worktree of `95aaf07`)

| Check | Result |
|---|---|
| `clang-format --dry-run --Werror` (18.1.8, CI pin) on the 21 scope files | exit 0 |
| `python3 tools/barr_check.py` | "21 file(s), 0 findings", exit 0 |
| `python3 -m pytest -q tools/tests pc/tests` (pytest 8.3.3) | 76 passed, 1 skipped (`test_skew_analyse.py:243`, matplotlib not installed) |
| `make -C panel_sync/test`, and `asan` | Pass = 4 of 4, both |
| `make -C common/proto/test`, and `asan` | Pass = 3 of 3, both |
| `idf.py build` `panel_sync` (v5.3, esp32s3, fresh `sdkconfig`) | complete, 0 warnings; image 805,560 B; DIRAM 111,771 B used of 341,760 B; IRAM 16,383 of 16,384 B |
| `idf.py -DTN_RX_HOOK=2 build` `panel_sync` | complete, 0 warnings |
| `idf.py build` `boardtest` | complete |
| Pico `ledwall_sync` (SDK 2.3.1, `pico_w`) | complete; text 341,860 B, bss 46,928 B |

The local machine has clang-format 10.0 (cannot parse `.clang-format`) and no pytest; the CI-pinned versions were installed into a scratch virtual environment for this run.

## 7. Gaps found in Phase 0

1. **Packed struct left on the Pico.** `ledwall_sync/main.c:42` `hello_t` is `__attribute__((packed))`, so T-B2-02 acceptance check 4 fails today. It goes with T-B2-03 (REGISTER through `common/proto`), or the HELLO receiver is removed from the rig build now.
2. **Prototype files still in the tree.** `render.c/.h`, `frames.c/.h` and `gif_frames.h` are no longer built but were not deleted in `95aaf07`, against plan section 0.1 and the "delete in the same commit" rule. `render.c` is replaced by `display_engine` (T-B5-01), so it can go now; `frames.c` is replaced by `asset_store` (T-B3-04). `make_frames.py` and `upload_frames.sh` go with T-B1-02 and T-B1-04.
3. **Command Queue missing.** The prompt's Phase 2 item 3 asks for Command Queue length 3 (PROVISIONAL); `tile_net` has only the Time Sync Queue.
4. **`bench_checklist.md` lives on the Drive, which is read only.** Adding Part B steps needs a repo copy (proposed: `docs/week10/bench_checklist.md`).
5. **DEFENCE docs.** Only `panel_sync/main/timesync_DEFENCE.md` exists. The prompt asks for `docs/<module>_DEFENCE.md` for every subsystem; the existing one is in `panel_sync/main/`.
6. **Ownership header wording.** Headers say "Minimal implementation by Buddy 4 for T-B4-03. Owner: ..."; the prompt asks for "Implemented by Ahsanul (B4) for <owner>, task T-xx".
7. **Hook pattern.** Closed 29 Oct 2026.
8. **File names for `common/proto`.** The plan says `proto_layout.c/.h`; the code uses `proto.c/.h` so the plan's `proto_` function names carry their module prefix (Barr-C 6.1.i). Recorded as PROVISIONAL in `proto.h`, with no DEFENCE doc yet.
9. **W6B-2 regression in the rig build.** The tile no longer registers its Node ID; harmless for the rig, but T-B2-03 must restore it.
10. **IRAM at 16,383 of 16,384 B.** The ESP-IDF size report shows the IRAM region full and the rest of the code in DIRAM `.text` (76,179 B). Whether more IRAM-safe code in Week 13 (RMT ISR, display path) will still fit has to be checked when T-B5-03 lands.

## 8. Critical path

Today is 29 Oct 2026. Dates from Ahsanul (29 Oct 2026), to be corrected if xSITe differs: Week 10 is the week of 2 Nov 2026; Week 13 is the week of 23 Nov 2026; Tribunal 2 is Mon 23 Nov 2026 in the lecture slot; the Final Run is in the team's lab session in the week of 23 Nov 2026. That leaves 4 days to the start of Week 10 and 25 days to Tribunal 2. Because Tribunal 2 falls at the start of Week 13, OD-24 resolves to option (b): T-B1-07 is built from the artefacts that exist on 23 Nov and marks the rest "not yet produced"; Week 13 evidence tasks must land before 23 Nov, not by the end of Week 13. OD-24 stays open until the date is confirmed on xSITe.

**Week 10 (the two-tile skew rig, PostTri updated readiness position):** T-B4-01 S-1 to S-3 (no hardware) → T-B2-01 table and the beacon row frozen (OD-05, OD-07) → bench: T-B3-02 and T-B3-01 (tiles), T-B5-04 boot log, then the T-B4-03 Part B run with T-B3-03 spread log and T-B2-02 lateness log from the same session. The software for the rig is already built; the remaining Week 10 work is mostly bench time and Benjamin's table. T-B1-01 (Python twin) is the one Week 10 code task not started.

**Week 13:** T-B2-03 → T-B2-04 → T-B2-05 → T-B4-04 → (T-B4-05, T-B4-06, T-X-07) is the longest chain, and it sits on Benjamin's and Ahsanul's tasks. In parallel: T-B1-02 → T-B1-03 → T-B3-05 → T-B1-04 → T-X-01, T-X-04, T-X-08 → T-X-06 → T-B1-07, which needs T-B3-04 and T-B3-06 first. T-B5-02 → T-B5-03 → T-B5-07 and T-X-03 follows once T-B3-04 and T-B3-06 exist.

## 9. Phase 1 update (29 Oct 2026, integration branch `b4/phase1`)

| Task | New status | Branch |
|---|---|---|
| T-B4-01 | Partial: S-1 to S-3 measured (`docs/week13/before/idf_size.txt`, `sizeof_xtensa.txt` 50 B and 9 B, `pico_size.txt`); B-1 to B-3 need hardware | `b4/t-b4-01-before-sizes` |
| T-B2-01 | Done in code: `protocol/layout_table.md`, all 18 rows in `common/proto`, host tests 7 of 7; widths PROVISIONAL (OD-07) | `b2/t-b2-01-common-proto` |
| T-B1-01 | Done in code: `pc/proto_layout.py`, 58 vectors, C/Python 57 vectors 0 mismatches | `b1/t-b1-01-proto-layout-py` |
| T-B3-06 | Partial: `fault_word` module and host tests 5 of 5; telemetry send/ACK in `tile_net` not yet | `b3/t-b3-06-fault-word` |
| T-B5-04 | Done in code: boot log with 160 MHz, 26.8 s wrap and `vTaskList()`; bench check C1 not run | `b5/t-b5-04-tile-tasks` |
| T-B5-01 | `render.c/.h` deleted | `b5/t-b5-01-delete-render` |

Review round 1 (two reviewers): 2 BLOCKING (two functions over 100 lines; these strings in this file), both fixed; T-B2-01 and T-B1-01 SHOULD FIX items fixed; the rest are listed in `docs/HANDOVER.md`.
