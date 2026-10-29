# Buddy 4 status (Phase 0, read-only)

Owner: Ahsanul (Buddy 4, Synchronisation and Playback Control). Written 29 Oct 2026 from `origin/master` at `3950a10`.
Sources: `SE15_PostTri1.docx` (PostTri), `SE15 (1).txt` (Week 6 feedback), `Plan/SE15_Implementation_Plan.md` (plan), `Plan/commitment_inventory.md` (inventory), `Week10_Kickoff/` (decision briefs, bench checklist, review log).

## 1. Buddy 4 tasks

| Task | Status | Evidence | Unmet dependencies (owner) |
|---|---|---|---|
| T-B4-01 `cr1-before` and before sizes | Partial | Annotated tag `cr1-before` (object `8353405`) points at `45e038c`, pushed (`git ls-remote --tags origin`). Host `sizeof` 50 B and 9 B and the derived rows filled: `docs/week13/before_after.md:18-21,40-42,80-95`. Blank: on-target `sizeof` (`:16-17`), on-air beacon and HELLO (`:22-23`), `idf.py size` DRAM/IRAM (`:24-25`), Pico size (`:26`), heap after WiFi (`:27-28`). | None for S-1 to S-3 (no hardware). B-1 to B-3 need 2 tiles, the Pico and the router (Benjamin supports B-2, B-3). |
| T-B4-02 `timesync` | Partial | Core and task exist: `panel_sync/main/timesync_core.c`, `timesync.c`; compiled by `panel_sync/main/CMakeLists.txt:1`. Host tests pass 4 of 4 (`make -C panel_sync/test`, run 29 Oct). Not wired: no task creates `timesync_task`; the prototype still runs on `lock` and `get_render_state` (`panel_sync.c:76,201-232`) and on 30 fps beacon extrapolation (`render.c:145-146`). `timesync_sync_ready()` is a stub returning false (`timesync_core.c:299-302`). Not Barr-C (Phase 2). | T-B3-03 `q_timesync` and the beacon receive (Cheston); T-B5-01 tick and `timesync_notify_tick()` call (Jia Jun); T-B5-04 task table (Jia Jun); OD-10 (mine). |
| T-B4-03 two-tile skew rig | Not started (preparation only) | `docs/week10/skew_2tile/RIG.md`, `pc/tools/skew_analyse.py`, `pc/tests/test_skew_analyse.py`. No capture. | T-B5-01 (Jia Jun), T-B3-03 (Cheston), T-B2-02 (Benjamin), plus T-B2-01 beacon row (Benjamin). |
| T-B4-04 `playback_control` | Not started | No `playback_control.c`. | T-B2-05 commands (Benjamin), T-B3-03 Command Queue (Cheston), T-B3-06 `fault_set()` (Cheston), T-B4-02 (mine), OD-10. |
| T-B4-05 holdover sizing | Not started | none | T-B4-04, T-B4-03 (mine); `CL_TEST_SUPPRESS_MS` in `coordinator_loop` (Benjamin). |
| T-B4-06 recovery timing | Not started | none | T-B2-04 (Benjamin), T-B3-05 (Cheston), T-B4-04 (mine). |
| T-B4-07 two-way fallback | Not triggered | Runs only if T-B4-03 or T-X-07 measures above 5 ms. | T-B4-03, OD-20. |
| T-X-05 torn-read stress | Not started (host forced-interleaving test exists: `panel_sync/test/test_timesync.c`, 200,000 interleavings) | No `TS_STRESS` build. | T-B5-01 core-1 reader (Jia Jun); stand-in task allowed by the refinement brief. |
| T-X-07 three-tile skew | Not started | none | T-B4-03, T-B4-04, OD-21 (Jolyn, Benjamin). |
| OD-10 `SYNC_READY` rule | Open | Stub, `timesync_core.c:299-302`. | Spread number from T-B3-03 for option (b). |
| OD-15 `DE_PROFILE` baseline | Ruled Round 0 (`critic_log.md`); implementation detail with Jia Jun open | none | T-B5-02 (Jia Jun). |
| OD-17, OD-24 lecturer questions | Open; no question sent | none | none (draft wording in Phase 3). |

## 2. PostTri rows that land on Buddy 4

| PostTri row | Implementing task | State in code | Contradiction |
|---|---|---|---|
| Version A Buddy 4 (16-beacon minimum-delay filter; 10 Hz beacons; integer tick; slew; holdover 1 s; START_AT 600 ms, 3 sends) | T-B4-02, T-B4-04, T-B2-02, T-B2-05 | Filter, drift, slew and tick arithmetic coded and host-tested. Beacons are still the prototype's 30 fps `frame_no` beacon (`ledwall_sync/main.c:259-288`). No START_AT, no holdover. | Prototype beacon carries a frame number, not t_p (Pico side, T-B2-02). |
| Defence snapshot row 1 (frame from corrected Pico time; tick at origin + n x 1,000,000 / 60 us, one-shot) | T-B4-02, T-B5-01 | `ts_deadline_pico()` and `frame_index()` coded. No gptimer one-shot. | `render.c:145-146` still extrapolates a free-running 30 fps count from the last beacon (inventory W6D-1a/VA-4.5 "Contradicts"). |
| Defence snapshot row 2 (600 ms lead, 3 sends, 1 s holdover, 5 s recovery, 10 s CONNECTING, STOP from PAUSED, session_id and seq on every command) | T-B4-04, T-B2-05, T-B4-06 | None in code. | `render.c:149` shows a red corner after 1 s but keeps extrapolating; PostTri says pause safely (W6D-2b "Contradicts"). |
| Shared resource "Corrected Pico time and drift estimate" | T-B4-02 (VC-1 supersedes SRM-5) | Seqlock over offset, drift, reference: `timesync_core.c:41-110`. | Prototype `portMUX` `lock` (`panel_sync.c:76`) still guards a different data set; removed when its callers go (`timesync_DEFENCE.md` section 16). |
| Interface map row `timesync` (`now_corrected()`, `frame_index(t)`, `deadline_local(n)`, SYNC_READY; single writer; slew at most 100 us per tick) | T-B4-02 | Functions exist with a reader parameter (`timesync_core.h:66-68`); SYNC_READY stub. | Signatures differ from the bare row signatures (open interface item 1, Jia Jun). Names lack the `timesync_` prefix that Barr-C 6.1.i requires (Phase 2 rename). |
| A6 (two-tile rig, then three) | T-B4-03, T-X-07 | Tooling only. | none |
| A7 (beacon suppression sizes holdover) | T-B4-05 | none | none |
| Version B Buddy 4 (seqlock, 64-bit time, frame index 10 bits, correction 7 bits) | T-B4-02 | Seqlock and 64-bit time coded. | 7-bit correction superseded by VC-2 (`int8_t`), coded. 10-bit frame_count limit (max 1023) is not enforced on the tile: `frame_index()` accepts any `uint32_t` frame_count. Phase 3 adds the check in `playback_control`. |
| Change log: seqlock; 64-bit time kept | T-B4-02 | Coded. | none |
| Version C: seqlock over three fields with core-1 reader; `int8_t` slew; 10-bit frame index | T-B4-02, T-X-05 | Seqlock host-tested with forced interleaving; `int8_t` slew test 1. | On-target core-1 reader not tested (T-X-05). 10-bit limit as above. |
| Risk R1 (unpack or locking adds tick lateness) | T-X-03 (Jia Jun), T-X-05 | none measured | none |
| Tests: tick timing (T-X-03), re-upload (T-X-04), torn-read (T-X-05) | T-X-03 Jia Jun, T-X-04 Benjamin, T-X-05 Ahsanul | none run | none |
| Curveball row B4 (sequence counter; 64-bit time; frame index 10 bits) | T-B4-02, T-B2-01 | Counter and 64-bit coded; no wire format yet. | none |

## 3. Week 6 feedback bullets on sync and playback

| Feedback (`SE15 (1).txt`) | What closes it | State |
|---|---|---|
| Line 4: free-running 60 Hz timers break NFR2; schedule tick at origin + n/60 s in corrected Pico time, re-armed every frame; target 5 ms or less | T-B4-02 arithmetic, T-B5-01 one-shot, T-B4-03 measurement | Arithmetic done; tick and measurement open. |
| Line 6: specify beacon period, transport, minimum-delay filter, drift, tolerances; timestamp early; power save off; measure jitter (A6) | T-B4-02 (filter 16, drift, slew 100 us), T-B2-02 (100 ms absolute), OD-09 and T-B3-03 (early timestamp, spread log) | Filter and drift done; power save already off (`panel_sync.c:144`); hook and spread log open. |
| Line 8: numeric priorities, cores, queue lengths and overflow | T-B5-04 `tile_tasks.h`; OD-11 | Open (Phase 3 writes the minimal `tile_tasks.h`). |
| Line 12: one readiness authority, session_id agreement | T-B2-04 (Pico bitmap); OD-10 for SYNC_READY | Open. |
| Line 13: CONNECTING timeout, STOP from PAUSED, recovery timeout, holdover, START_AT lead, sequence numbers, stale-command rejection | T-B4-04, T-B2-05, T-B4-06 | Open. |
| Key risk 1 (skew with one-way beacons) and the closing line (two-tile rig this week) | T-B4-03 | Open; tooling ready. |
| Key risk 5 (rejoin within 2 s, NFR8) | T-B4-06 | Open. |

## 4. Local against origin

- Local `master` equals `origin/master` at `3950a10`; `git log origin/master..HEAD` and `HEAD..origin/master` are both empty; the working tree was clean.
- Remote branches `b4/cr1-baseline`, `b4/timesync`, `b4/skew-tools`, `render-module` and `router-mode` are all merged into `origin/master`.
- CI on `origin/master` `3950a10` (run 39788925466): job "ESP32 (panel_sync, boardtest)" success, job "Pico W (ledwall_sync)" success. This closes review-log MINOR-5.
- Git identity: `WetzUmbrella <ahsanulpro@gmail.com>` matches every non-merge commit; the merge commits carry the GitHub web identity `Mohammad Ahsanul Haque <125888548+WetzUmbrella@users.noreply.github.com>`.
- `~/.claude/settings.json` already has `"includeCoAuthoredBy": false`. No local `commit-msg` hook existed before this session.

## 5. Contradictions and gaps found in the inputs

1. **Barr-C 5.1.c** requires every public type to carry the module prefix, so `ts_snapshot_t`, `ts_reader_t`, `ts_pair_t` and `ts_hook_point_t` must also be renamed (`timesync_...`). The brief lists only function renames.
2. **Barr-C 8.6.a** names only `==`. Flagging `!=` in `barr_check.py` is a project extension, labelled as such.
3. **Barr-C 3.3.c** (end-of-file comment) and **6.5.b** (`_isr` suffix) are not in the brief's findings list but apply to my files and to the dependency code.
4. **Plan names against Barr-C 6.1.i:** the T-B4-02 row names `ts_publish()`, `ts_read_snapshot()`, `now_corrected()`, `frame_index(t)` and `deadline_local(n)`. The rename follows Barr-C and the brief; the plan wording is superseded and logged in `timesync_DEFENCE.md`.
5. **Deadline rounding (review-log item 4):** T-B4-04's test "frame_index at t = origin + k x 1,000,000 / 60 equals k mod 600" holds only if t is rounded up; the brief resolves it by taking t from `ts_deadline_pico()` (interface item 3).
6. **Barr-C PDF licence:** the PDF grants publication "for your own internal use" only, and `WetzUmbrella/Led_Matrix` is public. `docs/coding_standard.md` therefore cites rule numbers with short paraphrases, not the rule text.
7. **10-bit frame_count** (VC-3) is not enforced on the tile (section 2).
