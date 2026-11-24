# Requirements check, 24 Nov 2026

Checked by the orchestrating agent against `feature/whack-a-mole` at `263d5c4`, the bench records in `docs/bench/`, the host tests, and the Tribunal 1 post-tribunal report (`../Tribunal_1/SE15_PostTri1 (1).docx`, outside the repo). Requirements: `docs/plan/game_plan_v2.md` sections 4 and 5 (frozen 9 Nov).

Verdicts: **Met** (built, and the evidence named passes), **Built, not measured** (works on the wall, but the pass criterion was never measured), **Partly** (some of the row), **Not met**, **Stretch** (optional, not built). "Wall" means seen in a bench round; it is not a measurement unless a number is given.

## 1. Summary

| | Count |
|---|---|
| Must and Should rows met | 42 (updated 24 Nov evening: rotation E3 to E6, E8 and NR-08 met; NR-07 met at 21:07 with the drift fix `cbe7763`) |
| Built, not measured or partly met | 15 (including NR-03 latency) |
| Not met | 1: NR-14 defence docs |
| Deferred | E7 rotation in a round (D-17) |
| Stretch, not built | B8, C7, G1 to G9, F1 to F5 |

Biggest gaps, in order of risk for the final demo and report:

1. ~~Video sync skew was never measured~~ (NR-07; the brief's core claim, W6 NFR2: 5 ms target, 16.67 ms limit). **Measured 24 Nov by 240 fps camera** (the tiles have no GPIO4 access for `pico_tap`): it failed on tiles 7 and 8 (up to 8.6 ms), the cause was the tiles' drift estimate, and with the fix `cbe7763` every pair filmed on tiles 4 to 9 is under 5 ms (section 3).
2. ~~Whole-wall rotation (E3 to E8) never built.~~ **Built 24 Nov evening for video mode (D-17); E3 to E6, E8 and NR-08 met on the wall; E7 deferred.** D-15 kept it Must after the prototype; later handoffs call step 5 "stretch", but no decision moving it is recorded. Either build it or record the descope with its reason.
3. **NR-03 (tap to hit burst under 100 ms) now fails by design.** The tile flashes only on the Pico's FEEDBACK (DS-07), so the 24 Nov collect wait of 150 ms (user's choice, `d7399f4`) puts tap to flash at about 240 ms. It was never measured before either; on the 24 Nov network the 20 ms wait gave about 110 to 130 ms. Options in section 5.
4. **The Week 13 change-requirement evidence (Tribunal 1) is incomplete and dated 3 Nov**, before the game added six message types (section 4).
5. **Defence docs do not cover the game** (NR-14): only `orientation_DEFENCE.md` mentions it.
6. **CI was red from 9 Nov to 24 Nov** (clang-format), which skipped barr_check, pytest and every host test in CI. Fixed in `263d5c4`; all steps pass locally.

## 2. Functional requirements (game_plan_v2 section 4)

### 2.1 Operator website (A)

| ID | P | Verdict | Evidence |
|---|---|---|---|
| A1 sparse grid to 9x9 | M | Met | Host tests (pc/tests, C19c); 4d wall test 12 Nov (`docs/bench/12nov/step4.md`) |
| A2 tap-to-place | M | Met | Host tests; step 4 wall rounds |
| A3 tile on/off | M | Met | `2137f5b`, host test C32; 19 Nov wall |
| A4 mode switch only while stopped | M | Met | Host tests; 24 Nov wall (and the restart resend fix `314f51e`) |
| A5 level | M | Met | Host tests; 24 Nov rounds at Easy and Hard |
| A6 arm and abort (abort clears within 500 ms) | M | Built, not measured | Host tests (C25); abort used on the wall 24 Nov; the 500 ms was never timed |
| A7 live panel within 500 ms | M | Built, not measured | STATUS every 250 ms (`CL_STATUS_PERIOD_US`); not timed against the Pico log |
| A8 tile offline within 3 s | M | Built, not measured | Seen 24 Nov (tiles 3 and 6 dropped); not timed |
| A9 results | S | Met | Website results match the Pico counts (24 Nov round: 34 of 34, Pico log 34 FEEDBACK hits) |
| A10 best per level | S | Met | Host test; "new best" seen 24 Nov |
| A11 tap test | S | Met | 19 Nov tap tests; 24 Nov |
| A12 tap threshold from the website | S | Met | Host test; 620 mg used 24 Nov |
| A13 website runs the whole demo | M | Met | `docs/bench/18nov/wall_test.md` |
| A14 game needs no video | M | Met | Host tests; 12 Nov round 6 |
| A15 pictures readable on every grid | M | Met | Host test; 18 Nov decision: whole picture on every tile |
| A16 `/score` page | S | Built, not measured | Page works (19 Nov); "readable from 3 m" and "within 0.5 s" not checked |

### 2.2 Round flow (B)

| ID | P | Verdict | Evidence |
|---|---|---|---|
| B1 countdown | M | Met | Host tests (C24); wall |
| B2 30 s round | M | Met | Host test C17 |
| B3 no tile twice in a row | M | Met | Host test C17 |
| B4 level speeds | M | Met | Host test C17 (curves pinned) |
| B5 round end, score and stars | M | Met | Host tests; wall 24 Nov |
| B6 abort clears the wall | M | Built, not measured | See A6 |
| B7 child start sprite | S | Met | Host test C24; 24 Nov (worked once the Pico's TAP counters were reset) |
| B8 last 5 s warning | X | Stretch | Not built |

### 2.3 Scoring (C)

| ID | P | Verdict | Evidence |
|---|---|---|---|
| C1 hit +1 | M | Met | Host test C18 |
| C2 no penalty, wrong counted | M | Met | Host test C18 |
| C3 missed counted | M | Met | Host test C18b |
| C4 taps within 50 ms count once | M | Met (30 ms by DS-07) | Host tests C18, C18c, C18d; 24 Nov round 0 wrong |
| C5 late tap up to 100 ms still hits | M | Met | Host test C18b |
| C6 stars | S | Met | Host test |
| C7 golden mole | X | Stretch | Not built |

### 2.4 Tile visuals (D)

| ID | P | Verdict | Evidence |
|---|---|---|---|
| D1 to D6 mole sprite, pop-up, burst, X, duck, holes | M/S | Met | Tile host tests (test_game_tile); step 3 wall (`docs/bench/12nov/step3.md`) |
| D3 "the tile shows it locally" | M | Partly | Built as Pico-confirmed (DS-07, 11 Nov); see NR-03 |
| D7 recognisable in greyscale | M | Built, not measured | Shapes differ by design; the greyscale photo check was never done |

### 2.5 Wall and rotation (E)

| ID | P | Verdict | Evidence |
|---|---|---|---|
| E1 any layout, video and game | M | Partly | 2x2, 3x3 and the 5x5 sparse diagonal on the wall; 1x5 and 1x9 not run as bench cards |
| E2 each tile upright | M | Met | Host tests D4 to D6; 11 Nov step 1 |
| E3 to E5, E8 whole-wall rotation (video mode, D-17) | M | Met (24 Nov evening) | Pico decides (`wall_turn`, C35 to C38); NR-08 bench: 10 of 10 turns, detection at most 1.8 s, map turned right |
| E6 video resumes after a turn | M | Met | Playing again at most 7.9 s after the first tile moved (mean 7.2 s), at the saved frame (`freeze_old_router.md`) |
| E7 rotation in a round | M | Deferred (D-17) | Turns are ignored in game mode until a justification is found |
| E9 tile drops out mid-round | M | Built, not measured | `game_rules_set_active`; never unplugged during a round on purpose |
| E10 video as on master | M | Met | 12 Nov V2b (9 tiles), 18 Nov |

### 2.6 Rotate sprite (G) and stretch (F)

Stretch since D-15 (10 Nov): not built. The professor consult on a workaround (12 Nov) has no recorded outcome.

## 3. Non-functional requirements (section 5)

| ID | Verdict | Evidence or gap |
|---|---|---|
| NR-01 taps detected (95% of 50) | Built, not measured | Soft taps at 370 to 1,348 mg all registered (19 Nov, 8 taps); the 50-tap card was never run |
| NR-02 false taps | Built, not measured | 24 Nov round 0 wrong at 620 mg; the 300 mg default had echoes; no formal card |
| NR-03 tap to burst | Built, not measured | Revised 24 Nov (user): under 300 ms, accuracy over latency (about 240 ms by design); never measured (GPIO4 pulses not wired) |
| NR-04 rotate feedback | n/a | Rotate sprite is stretch |
| NR-05 no double counts (50 hits) | Partly | 24 Nov: 34 of 34 hits, 0 wrong after the echo fix; not the 50-hit card |
| NR-06 10 rounds without reboot | Partly | 24 Nov: 6 rounds and 2 x 6 min reproducer runs without a freeze on the old router; not 10 in a row; the watchdog recovers a freeze in about 20 s |
| NR-07 video sync under 5 ms | **Met** (24 Nov, 21:07, by camera, tiles 4 to 9 on `cbe7763`) | 240 fps slow-motion video, two tiles at a time, threshold 200, pieces within 2 % of 240 fps, every step over 4 ms checked (`docs/bench/24nov/freeze_old_router.md`, "NR-07 by camera" and "NR-07 with the drift fix"). Before the fix tiles 7 and 8 failed (tile 8 up to 8.6 ms ahead; cause: a drift estimate of about 1,500 ppm from beacon delay). With `cbe7763` (drift limited to 50 ppm): 7-8 (twice), 8-9 and 5-6 have no real step at or over 5 ms, medians -0.38 to +0.31 ms; misreads shown by eye and excluded. Limits: about 15 s of real time per recording, 5-6 on 3 pieces, 4-5 filmed only on the old firmware (passed), tiles 1 to 3 not tested; steps timed to about one frame (4.17 ms) |
| NR-08 rotation detection | Met (24 Nov) | 10 of 10 board turns, 0 of 10 single-tile turns |
| NR-09 brightness cap and 4-bit packing | Met | `check_assets` host test; `docs/week13/check_assets.log` |
| NR-10 code standard, CI green | Met from 24 Nov | barr_check 0 findings on 104 files; CI green from `263d5c4` (red 9 to 24 Nov) |
| NR-11 score consistency | Met | Website, Pico STATUS and wall agree (24 Nov) |
| NR-12 setup in 3 min | Built, not measured | Website start plus uploads of 3 to 40 s; never timed from power-on |
| NR-13 upload in 60 s | Met | 3 to 40 s per tile for a whole wall (18 and 24 Nov) |
| NR-14 defence docs cover the game | **Not met** | Only `orientation_DEFENCE.md` mentions the game |

## 4. Tribunal 1 commitments

### 4.1 Gap-check fixes (Version C): all in the code

| Commitment | Where |
|---|---|
| Seqlock over offset, drift and reference | `panel_sync/main/timesync.c` (`seqlock_word_t`) |
| Slew stored as int8_t, -100 to +100 us | `timesync.h:51` |
| GIFs over 1023 frames refused | `pc/asset_builder.py:15`, `PROTO_FRAME_MAX` |
| Fault bits cleared only on TELEMETRY_ACK | `telemetry_book.c`, `tile_net.c` |
| Format version in the manifest; unknown version NACKed, no ASSET_READY | `asset_rx.c` (`asset_rx_check_manifest`) |
| Brightness scaled once, on the PC (cap 1 to 8) | `pc/asset_builder.py`; no scaling in `display_engine.c` |
| Re-upload rate-limited on the PC | `pc/uploader.py` (100 chunks/s, provisional) |

### 4.2 Week 13 change-requirement evidence (minimum-bit representation)

| Required in the repo | State |
|---|---|
| Data representation table | `docs/week13/data_representation.md`, 3 Nov: **does not cover the game fields** (protocol rows 18 to 23, added after); `protocol/layout_table.md` has their bit widths and could be cited |
| RAM before and after | `docs/week13/before_after.md`: static sizes measured; "after" is a 3 Nov build, before the game; free heap after WiFi still `[NEEDS MEASUREMENT]` |
| Message size before and after | `docs/week13/message_sizes.md`: encoded sizes derived; on-air bytes `[NEEDS MEASUREMENT]` |
| Correctness and performance | Packed round trip, fault-word stress, tick timing 8-bit vs packed, re-upload beacon lateness, torn-read stress: **all NOT RUN on hardware**; their logs (`docs/week13/round_trip_tile.log`, `fault_stress.log`, `prep_profile_*.log`, `reupload_lateness.log`) do not exist. Host tests cover the logic (timesync seqlock, fault word, proto vectors). |

## 5. NR-03 against the echo fix (decided 24 Nov: keep 150 ms, NR-03 restated to under 300 ms)

The tile flashes only when the Pico's FEEDBACK arrives, so the Pico's collect wait adds directly to tap-to-flash. Replay of 24 Nov (84 taps, `docs/bench/24nov/tools/echo_sim.py`):

| Option | Taps judged on an echo | Tap to flash (estimate) | NR-03 |
|---|---|---|---|
| Keep 150 ms (now on the Pico) | 5 of 84 | about 240 ms | Fails; change NR-03 to about 300 ms and justify by accuracy |
| 20 ms plus 8 remembered groups | 25 of 84 | about 110 to 130 ms | Still over 100 ms on this network |
| Local flash on the tapped tile at once, Pico verdict decides the score (D3 as first written) | as the chosen wait | about 20 to 40 ms | Meets it, but a tap that loses to a stronger neighbour flashes green and is then not scored (DS-07 chose "nothing is taken back") |

None of the three meets 100 ms and keeps accuracy without a design change; the report should state the trade-off whichever is chosen.

## 6. What the demo can claim today

A full child round on a 3x3 wall from the website, with level choice, tap tuning, start sprite, countdown, scoring without penalties, results and `/score`; video of any GIF on any layout up to 9x9 sparse; recovery from a Pico restart by itself; 34 of 34 hits with 0 wrong on the old router after the echo fix; whole-wall rotation in video mode (E3 to E6 and E8; NR-08: 10 of 10 wall turns, 0 of 10 single-tile turns); and a measured video sync skew under 5 ms between neighbouring tiles (NR-07, tiles 4 to 9, firmware `cbe7763`). It cannot yet claim a measured tap latency (NR-03), and rotation during a round (E7) is deferred.
