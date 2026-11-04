# Bench notes, 5 Nov 2026: Benjamin (Buddy 2)

Pico coordination and Wireshark, on station S1 (sync rig, Net A).
One row per test I ran or owned. Quote the log line that decided the result.
Raw logs go to the evidence paths named in `docs/bench/2026-11-05_plan.md`, never edited by hand.

Build tag flashed: `bench-5nov` (write `bench-5nov-2` etc. if the tag moved)

## Tests I own

| Test | Status (PASS / FAIL / PARTIAL / NOT RUN) | Deciding line or number | Evidence path | Tag | Time |
|---|---|---|---|---|---|
| D1 Pico 10 min beacon run, 2 tiles (T-B2-02) | NOT RUN | | `docs/week10/pico_lateness_10min.log` | | |
| D2 callback context, same run (OD-01) | NOT RUN | | `docs/week10/pico_lateness_10min.log` | | |
| D7 registration check, same run (W6B-2) | NOT RUN | | `docs/week10/pico_lateness_10min.log` | | |
| D3 one beacon decoded on the PC (T-B2-02) | NOT RUN | | `docs/week10/beacon_decode.txt`, screenshot | | |
| G4 re-upload during play (T-X-04) | NOT RUN | | `docs/week13/reupload_lateness.log`, skew report, `.pcapng` (folder to agree with Ahsanul), `docs/week13/message_sizes.md` | | |
| H1 router 30 min, inside the 5-tile combined run (T-B2-07) | NOT RUN | | `docs/week13/router_30min/` | | |

## Tests I run or support for others

| Test | Owner | Status (PASS / FAIL / PARTIAL / NOT RUN) | Deciding line or number | Evidence path | Tag | Time |
|---|---|---|---|---|---|---|
| Two-Pico test (I run Pico B) | Ahsanul | NOT RUN | | `docs/week10/two_pico/` | | |
| H3 recovery timing | Ahsanul | NOT RUN | | `docs/week13/recovery_timing.csv` | | |
| F9 dashboard | Jolyn | NOT RUN | | `docs/week13/dashboard_run.log`, photo | | |
| B1 to B9 two-tile skew (I watch the Pico log) | Ahsanul | NOT RUN | | `docs/week10/skew_2tile/` | | |

## Pass criteria to check against (from the plan cards and the bench checklist)

- D1: in the last `LATE_HIST` line, every bin from 5 ms up is 0 (the line lists 0 to 9 ms then 10 ms and over, counted since the Pico booted (checked in `coordinator_loop.c` on phase2), so the Pico is reset and its log started before the tiles are powered); in the last `CL_STATS`, `last_seq - first_seq + 1 = sent` and `send_fail=0`.
- D2: `exc_max=0` in the last `CL_STATS` line. This settles OD-01.
- D7: one `CL_EVENT ... JOIN node=<n>` per tile and `online=0x003` in `CL_STATS`. The Pico log must be running before the tiles are powered.
- D3: the decode of one captured payload (Wireshark filter `udp.dstport == 4210`) matches the fields byte for byte with `pc/proto_layout.py`.
- G4: `LATE_HIST` bins from 5 ms up stay 0 during the upload; max skew between the 2 playing tiles at most 5 ms; no `[NEEDS MEASUREMENT]` left in the on-air column of `message_sizes.md`, then `python pc/tools/gen_week13_docs.py`.
- H1: router DTIM 1 and AP isolation off (written into these notes; the checklist's `README.md` is not edited on the day); power save off on the PC, Pico and tiles (tiles log `Set ps type: 0`); 5 tiles registered for 30 min; 0 false heartbeat-timeout drop-outs at the 2.5 s timeout (no `LEAVE` for a powered tile); beacon loss (`TN_STATS lost=` change / beacons sent) reported beside the 4.5% DTIM 3 baseline. A drop-out selects 3 s under OD-18 and the run repeats.
- Two-Pico test: step 1 `foreign=0`; step 2 `foreign=` rises on both tiles, about 10 `BCN,` per second, `t_p` from A only (no jumps); step 3 one `TS reseed n=1` per tile within about 2 s, SYNC_READY drops, the tile REGISTERs to B (3 copies, R1-1) and B logs `CL_EVENT ... JOIN`; no JOIN within 5 s: power-cycle the tile and write it down (a finding for me); step 4 tiles stay on B. Record A-off to reseed and to JOIN per tile.
- H3: rejoin within 2 s of reconnection, reconnect plus rejoin within 5 s, join plus DHCP within 10 s, each 20 of 20.
- B (support): watch the Pico log for `LATE_HIST` and `foreign=` during the capture.
- F9 (support): `tile <n> lost` within 2 s of the Pico log entry, 10 of 10; Start disabled until the bitmap is complete.

## Notes

- Hardware used (tile board numbers and MACs, Pico A/B, network (Net A/B label only, no SSID or password), analyser or pico_tap):
- Wireshark: a laptop on WiFi sees only broadcasts (beacons, telemetry) and its own traffic, so G4's on-air sizes for tile/Pico unicast and upload chunks come from the uploader's laptop or are calculated; record `udp.length` (payload + 8) as in checklist A6, and state the unit used in the on-air column.
- Anything that went wrong, with the time and what you changed:
