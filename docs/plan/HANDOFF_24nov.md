# Session handoff, 24 Nov 2026 (old router, freeze verdict, echo fix, CI green, requirements check)

Written by the orchestrating agent for itself (or any agent) starting cold. **This file supersedes `HANDOFF_19nov.md` for "where things stand".** Bench setup and method: `HANDOFF_18nov.md` sections 2 and 4, with the changes in section 2 below.

Read before acting, in this order:
1. This file.
2. `docs/final/requirements_check_24nov.md`: what is met, what is not, and the NR-03 decision.
3. `docs/bench/24nov/freeze_old_router.md`: the day's bench record (freeze runs, real rounds, both fixes).

## 1. Where things stand

| Item | State |
|---|---|
| Branch | `feature/whack-a-mole`, pushed to `263d5c4` and this file. |
| Pico freezes | **Verdict: use the old router for the demo.** Head firmware passed the 6 min reproducer twice and 6 real rounds (hard slaps to 21 g) with no freeze; the Asus froze in about 2.5 min. The Pico still hangs in the router join (`wd_step 0x0b`) when the router disappears; the watchdog recovers it. |
| Echo grouping | Fixed (`d7399f4`): collect wait 150 ms, 8 remembered groups, 32 tap slots. Wall round: 34 of 34 moles, 0 wrong. **Breaks NR-03** (decision pending, requirements check section 5). |
| Website | Mode resent to a restarted Pico once it is READY (`314f51e`). |
| CI | **Red 9 to 24 Nov** (clang-format), fixed in `263d5c4`; check the run on that commit. Formatter: `.venv/bin/clang-format` 18.1.8 (installed in the venv; the system one is 23 and formats differently). |
| Gate | Host tests (all suites, plain and ASan) pass, coordinator 39 of 39; barr_check 0 findings on 100 files; pytest 505 passed; Pico firmware builds. |

### Boards on the bench

| Board | Firmware | Notes |
|---|---|---|
| Pico `…E66430A64B595138` (MAC `2c:cf:67:d7:eb:c3`) | `d7399f4` (echo fix) | 192.168.1.154, on the laptop's USB for logs |
| Pico `…E66430A64B608538` | `0007a53` | Off the bench |
| Tiles 1 to 9 | `77e3165` | No tile change since 18 Nov; on a power bank |

## 2. Bench setup changes

- **Router: the old Matrix router** (BSSID `88:d7:f6:be:85:70`), SSID changed to `MATRIX` so the firmware needs no change, fixed channel 11. Move away from busy WiFi: in a crowded spot pings were 40 to 190 ms and uploads 27 to 39 s per tile; in a quieter spot 15 to 110 ms and 4 to 20 s.
- Laptop wired adapter is now `enp0s13f0u3u2` (.228).
- Website start unchanged: `.venv/bin/python pc/web.py --pico 192.168.1.154 --layout 3x3 --gif numbered_cells_3x3.gif`. A restarted website writes a new tap log (`pc/logs/taps_<start>.csv`).
- Tiles 3 and 6 dropped off together at 15:13 and came back only on a replug: check their power feed before the demo.

## 3. Next, in order

1. **NR-03 decision** (user): keep 150 ms and restate NR-03, go back to 20 ms, or flash locally (requirements check section 5).
2. **Whole-wall rotation E3 to E8** (user): build step 5 or record the descope with its reason in `game_plan_v2.md`.
3. **Measure video sync skew (NR-07)**: wire `pico_tap` (Pico `…3B6838`) to two tiles' GPIO4 and GND, run `pc/tools/skew_analyse.py`; the rig is in `docs/week10/skew_2tile/RIG.md`. This is the brief's core claim and has never been measured.
4. **Week 13 change-requirement evidence**: add the game rows to `docs/week13/data_representation.md` (bit widths from `protocol/layout_table.md`), re-measure the "after" sizes on head, fill the `[NEEDS MEASUREMENT]` cells (heap after WiFi, on-air bytes), and run or explicitly drop the five hardware tests (section 4.2 of the check).
5. **Defence docs**: each buddy's `docs/*_DEFENCE.md` needs its game part (NR-14).
6. Smaller: the Pico's `stale=928` after the 14:54 restart (not checked); 10 rounds in a row for NR-06; time A6, A7, A8 against the Pico log.

## 4. Method notes from today

- **After any `tap_burst.py` run, restart the Pico** before a real round: the fake TAPs push the Pico's per-tile TAP seq past the tiles' own, and real taps from tiles 1 to 8 are dropped as stale until a restart. In the same session, give the tool a FIRST_SEQ past the last run's (6th argument).
- The Pico's USB log keeps nothing from before the port is opened: start the capture before a reboot to see boot events. Use plain `cat`; a `stty` left in D state held the port once (kill -9 it).
- `docs/bench/24nov/tools/echo_sim.py PICO_LOG TAPS_CSV` replays the Pico's grouping on a round's real arrival times; it matched the Pico's judgement counts within 2.
- Barr output: read all of it, not `tail -1`; several findings hid behind the last line today.
- A restarted Pico passes SYNCHRONISING, where it refuses a mode change but ACKs it.
