# Handoff, 25 Nov 2026 (eve of the final demo)

Read this first; it supersedes `HANDOFF_24nov_evening.md` where they differ. Final demo **Thu 26 Nov 15:30** (video and game). Final submission is the repo link; `feature/whack-a-mole` was merged into `master` (PR #8, merge commit `8605ea4`), CI green. Both branches now point at the same work.

## 1. State

- Firmware on the wall: tiles `630c390` (all 9), Pico `…595138` on `f9d313c` at 192.168.1.154, router `MATRIX` (old router, channel 11). No firmware changed today.
- Website: new option `--policy pause|continue` (`2982bb3`). **Demo start command:**

  ```
  .venv/bin/python pc/web.py --pico 192.168.1.154 --layout 3x3 --gif frog_ripple_64x64.gif --policy continue
  ```

- Demo cheat sheet (team doc, run order, setup checklist, fallbacks, numbers, known limits, Tribunal 1 proof, defence Q&A): https://claude.ai/artifact/V3FikdhHcRCWvytpHoZRsr. The user shares it with the team; the message to the team was drafted in the session.

## 2. Done today

| What | Result | Record |
|---|---|---|
| Cold start | website restart to READY 3 s; each tile 2 to 3 s after it joins | `docs/bench/25nov/coldstart.md` |
| Power pulls | continue policy passes 3 s and 10 s pulls; **pause policy restarts the Pico** | `docs/bench/25nov/powerpull.md` |
| NR-03 | met: tap to squash median about 171 ms, worst about 196 ms | `docs/bench/25nov/nr03.md` |
| Rehearsal | demo run fine; 13 rounds, no Pico restart (NR-06 met); E9 partly | `docs/bench/25nov/rehearsal.md` |
| Week 13 CR1 evidence | game fields, 24 message sizes, final-build RAM and heap, correctness note | `docs/week13/` |
| Test report | 117 C cases and 518 pytest pass (1 skipped); 14 system tests with input, expected, actual, verdict | `docs/testing/TEST_REPORT.md`, `docs/testing/results/2026-11-25/` |
| Test tooling | `tools/run_all_tests.sh` runs every suite and writes the logs; `tools/test_cases.py` lists every case; pico_tap tests print per case and run in CI | `8d17446` |
| Flaky test fixed | session ids may repeat an older one; the test now checks neighbours only | `f3022be` |
| Requirements check | section 1.1 with today's results | `docs/final/requirements_check_24nov.md` |

## 3. Open items (none blocks the demo)

1. **Pause policy restarts the Pico** (ST-12). A tile away over 5 s: RECOVERING to FAULT (`cl_scan_heartbeats`); FAULT only runs `cl_rejoin_routine()`, which re-joins the router although the link is up; the join outlasts the 8 s watchdog (`wd_step=0x0a`, JOIN; the 24 Nov 21:28 restart has the same breadcrumb). Fix: a tile-loss FAULT must not re-join a router that is up. Avoided by `--policy continue`.
2. **A tile restarted mid-round shows nothing until the results** (ST-13, user's decision: known limit). The round command goes out once; `cl_game_sync_active()` puts the tile back in the mole choice, so a mole sent to it is invisible. Fix: resend the round state to a returning tile, or keep it out of the choice until the round ends.
3. **Several tappers at once** read as wrong taps (121 wrong to 412 hits in the rehearsal; 24 Nov with one tapper: 0 wrong). Demo: one tapper.
4. **Power**: every dropout today was a flat power bank (tiles 2, 4, 5, 6, 7, 8, 9 at times). Charge all banks before the demo.
5. Week 13 gaps, stated in the docs: no packet-sniffer capture of on-air sizes; heap on 2 boots, not 10; no 8-bit vs packed timing profile.
6. README sections 1 to 7 still describe the October prototype (marked as history under the final-submission section). `pc/gifs/` is git-ignored, so a clone has no demo GIF.

## 4. Method notes from today

- **Use `.venv/bin/clang-format` (18.1.8, the CI pin).** `/usr/bin/clang-format` on the Linux laptop is version 23 and flags dozens of untouched files.
- Re-run the tests for evidence with `./tools/run_all_tests.sh` on a clean tree; stash the two growing 24 Nov logs (`docs/bench/24nov/long_pico.log`, `long_tile8.log`, never committed) first so the summary shows 0 changed files. Do not delete the results folder before a run: it counts as changed files.
- Never stop a process with `pgrep -f`/`pkill -f` on a pattern that appears in the same shell command: it matched its own shell today. Find the PID in a separate command.
- Reading the Pico's USB serial (`/dev/ttyACM0`) does not reset it; opening a tile's serial port does. Only one reader at a time on a tty, or the output is split.
- The start sprite times out after 60 s; arm only when the player is ready.
- NR-03 method without a side view: the Pico plans the next mole at FEEDBACK + 200 ms (`GAME_LEAD_MS`) and tiles draw it at `t_show` on the synced clock, so squash-to-next-mole in a 240 fps video gives the display lag; the Pico's `GAME_TAP` value gives tap to arrival.
- iPhone slow motion is stored at 30 fps playback; the capture rate (240) was confirmed from the burst spacing against the tap log.

## 5. Next

Demo day: set up early with the cheat sheet checklist. After the demo, if work continues: fix open items 1 and 2 with host tests first (C-suite cases), then a wall re-test, and update `TEST_REPORT.md` section 4 and its results folder.
