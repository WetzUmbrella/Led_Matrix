# Bench notes, 5 Nov 2026: Jia Jun (Buddy 5)

Station S3 lead (bench builds), display and orientation, analyser wiring on S1.
One row per test I ran or owned. Quote the log line that decided the result.
Raw logs go to the evidence paths named in `docs/bench/2026-11-05_plan.md`, never edited by hand.

Build tag flashed: `bench-5nov` (write `bench-5nov-2` etc. if the tag moved)

## Tests I own

| Test | Status (PASS / FAIL / PARTIAL / NOT RUN) | Deciding line or number | Evidence path | Tag | Time |
|---|---|---|---|---|---|
| C1 boot log on phase2 (T-B5-04) | NOT RUN | | `docs/week10/tile_tasks_boot.log` | | |
| F7 Orientation Task (T-B5-05) | NOT RUN | | tile log (`docs/week13/orientation_tile.log`) | | |
| F8 orientation trials, 80 rotations (T-B5-06) | NOT RUN | | `docs/week13/orientation_trials.md`, video | | |
| G3 preparation profile, 8-bit run (T-X-03) | NOT RUN | | `docs/week13/prep_profile_8bit.log` | | |
| G3 preparation profile, packed run (T-X-03) | NOT RUN | | `docs/week13/prep_profile_packed.log` | | |
| F6 frame path on the analyser (T-B5-03) | NOT RUN | | `docs/week13/display_frame_path.*` | | |
| G8 display timing with WiFi, 30 min (T-B5-07 a) | NOT RUN | | `docs/week13/display_timing/` | | |
| G9 slow preparation (T-B5-07 b) | NOT RUN | | `docs/week13/display_timing/slow_prep.*` | | |
| G10 flash erase stall (T-B5-07 c) | NOT RUN | | `docs/week13/display_timing/flash_erase.log` | | |
| G11 power and heating (T-B5-08), only with meter and thermometer | NOT RUN | | `docs/week13/power_thermal.md` | | |
| B1 test pin number | NOT RUN | | `docs/week10/skew_2tile/bench_log.md` notes | | |
| B0.11 analyser wiring | NOT RUN | | notes below | | |

## Tests I run or support for others

| Test | Owner | Status | Deciding line or number | Evidence path | Tag | Time |
|---|---|---|---|---|---|---|
| G2 fault word stress (run at S3) | Cheston | NOT RUN | | `docs/week13/fault_stress.log` | | |
| G5 seqlock stress (run at S3) | Ahsanul | NOT RUN | | `docs/week13/ts_stress.log` | | |
| B2 tick-identity marker | Ahsanul | NOT RUN | | `docs/week10/skew_2tile/bench_log.md` | | |
| G6 three-tile skew (wiring) | Ahsanul | NOT RUN | | `docs/week13/skew_3tile/` | | |
| G1 round trip | Jolyn | NOT RUN | | `docs/week13/round_trip_tile.log` | | |

## Pass criteria to check against (from the plan cards)

- C1: `vTaskList` lists every task at its priority and core (display 20/1, orientation 5/1, timesync 16/0, playback 15/0, tile_net 12/0, asset_store 8/0, tiT on 0).
- F7: `orientation 5 ... 1` in `vTaskList`, and `ORIENT imu ready`.
- F8: at least 19 of 20 correct per orientation (0, 90, 180, 270).
- G3: highest non-zero `DE_PROF` bin under 14 ms in both runs, `missed=` unchanged; record median, p99, max and `cpu_mhz=`.
- F6: RMT data starts after the tick pulse on 100% of ticks, `missed=` unchanged, `rmt_busy=0` (capture at 4 MS/s or more).
- G8: every interval 16.67 ms +/- 1 ms for 30 min, `missed=` unchanged.
- G9: `rmt_busy=0`, `missed=` unchanged over 10 min.
- G10: every `DE_ERASE ... us=` line recorded; no pass limit.
- G2: `lost=0` on both `FAULT_STRESS bit=` lines, `max_wait_us` under 3,000,000.
- G5: `torn=0` on both `TS_STRESS reader=` lines, `reads=` at least 1,000,000; record `stale=`.

## Notes

- Decision 3 (test pin on the Waveshare header): GPIO4 reachable? ______ (if not, pin used: ______)
- Hardware used (tile board numbers and MACs, Pico A/B, network, analyser or pico_tap):
  - ESP-4 is MAC `9c:13:9e:f1:2e:b0`; it boots into download mode on a software reset from a Mac, so replug USB instead of pressing RESET.
- Previous C1 run (29 Oct, tile ESP-4, pre-phase2 build) passed; this run replaces that log with the phase2 build.
- Anything that went wrong, with the time and what you changed:
