# Session handoff, 24 Nov 2026 (old router, freeze verdict, echo fix, CI green, requirements check)

Written by the orchestrating agent for itself (or any agent) starting cold. **This file supersedes `HANDOFF_19nov.md` for "where things stand".** Bench setup and method: `HANDOFF_18nov.md` sections 2 and 4, with the changes in section 2 below.

Read before acting, in this order:
1. This file.
2. `docs/final/requirements_check_24nov.md`: what is met, what is not, and the NR-03 decision.
3. `docs/bench/24nov/freeze_old_router.md`: the day's bench record (freeze runs, real rounds, both fixes).

## 1. Where things stand (updated 24 Nov, 17:20)

| Item | State |
|---|---|
| Branch | `feature/whack-a-mole`, pushed (head after `ac53533`). CI green from `263d5c4`. |
| Pico freezes | Old router for the demo. One freeze in the evening, during a re-upload after a wall turn back; 30 plain re-uploads and 10 turn re-uploads afterwards had none. The watchdog recovers it. |
| Echo grouping | Fixed (`d7399f4`, collect wait 150 ms); NR-03 restated to under 300 ms (user's decision). |
| **Whole-wall rotation (D-17)** | **Built and passing on the wall, video mode only.** The Pico listens to the tiles' TELEMETRY (port 4215), decides a wall turn (`wall_turn.c`), stops and saves the frame, reports STATUS `wall_turn`; the website re-cuts and re-uploads; the Pico resumes at the saved frame. NR-08: 10 of 10 board turns (detection at most 1.8 s, playing again at most 7.9 s), 0 of 10 single-tile turns. E7 (rotation in a round) deferred. |
| Robustness fixes today | Website resends the mode once a restarted Pico is READY (`314f51e`); tiles stop playing when the Pico restarts (`6b1e446`); failed uploads retried once the tile rejoins (`a18bce7`); Pico UDP pcbs raised to 6 (`8988105`); two wall-turn fixes (`f9d313c`). |
| Gate | All host suites plain and ASan (coordinator 43 of 43, playback 7 of 7), barr_check 0 findings on 104 files, pytest 511, both firmwares build. |

### Boards on the bench

| Board | Firmware | Notes |
|---|---|---|
| Pico `…E66430A64B595138` (MAC `2c:cf:67:d7:eb:c3`) | `f9d313c` | 192.168.1.154, on the laptop's USB for logs |
| Tiles 1 to 9 | `6b1e446` | Power bank; tiles 8 and 9 share a power run, and 3 and 6 dropped together once |

## 2. Bench setup changes

- **Router: the old Matrix router** (BSSID `88:d7:f6:be:85:70`), SSID changed to `MATRIX` so the firmware needs no change, fixed channel 11. Move away from busy WiFi: in a crowded spot pings were 40 to 190 ms and uploads 27 to 39 s per tile; in a quieter spot 15 to 110 ms and 4 to 20 s.
- Laptop wired adapter is now `enp0s13f0u3u2` (.228).
- Website start unchanged: `.venv/bin/python pc/web.py --pico 192.168.1.154 --layout 3x3 --gif numbered_cells_3x3.gif`. A restarted website writes a new tap log (`pc/logs/taps_<start>.csv`).
- Tiles 3 and 6 dropped off together at 15:13 and came back only on a replug: check their power feed before the demo.

## 3. Next, in order

1. **Measure video sync skew (NR-07)**: wire `pico_tap` (Pico `…3B6838`) to two tiles' GPIO4 and GND and run `pc/tools/skew_analyse.py` (rig: `docs/week10/skew_2tile/RIG.md`). Never measured; it is the brief's core claim.
2. **Week 13 change-requirement evidence**: game rows and the new STATUS `wall_turn` in `docs/week13/data_representation.md`, re-measure the "after" sizes on head, fill the `[NEEDS MEASUREMENT]` cells, run or explicitly drop the five hardware tests (`requirements_check_24nov.md` section 4.2).
3. **Defence docs** (NR-14): each buddy's game part, now including `wall_turn` (Benjamin), the TELEMETRY-on-turn and restart stop (Cheston, Ahsanul for playback), the website turn and retry (Jolyn).
4. Smaller: the Pico's `stale` counter after restarts (not checked); 10 rounds in a row for NR-06; time A6, A7, A8; whether turns while paused behave (built, not wall-tested).

## 4. Method notes from today

- **After any `tap_burst.py` run, restart the Pico** before a real round: the fake TAPs push the Pico's per-tile TAP seq past the tiles' own, and real taps from tiles 1 to 8 are dropped as stale until a restart. In the same session, give the tool a FIRST_SEQ past the last run's (6th argument).
- The Pico's USB log keeps nothing from before the port is opened: start the capture before a reboot to see boot events. Use plain `cat`; a `stty` left in D state held the port once (kill -9 it).
- `docs/bench/24nov/tools/echo_sim.py PICO_LOG TAPS_CSV` replays the Pico's grouping on a round's real arrival times; it matched the Pico's judgement counts within 2.
- Barr output: read all of it, not `tail -1`; several findings hid behind the last line today.
- `docs/bench/24nov/tools/upload_soak.py RUNS LOG` re-uploads the whole wall from the website and counts freezes.
- The Pico prints `tel_rx`, `wt_known`, `wall_turn` in CL_STATS: `tel_rx` must rise about 9 per second, `wt_known=0x1ff` for a 9-tile wall.
- After any layout change by hand, check the website's map against the tiles' orientations before a turn test.
- Host tests inject datagrams without opening sockets, so a port that fails to open (lwIP pcb limit) passes them; read the Pico's boot lines after adding a port.
- A restarted Pico passes SYNCHRONISING, where it refuses a mode change but ACKs it.
