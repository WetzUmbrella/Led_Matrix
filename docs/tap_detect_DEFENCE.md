# tap_detect: design defence (software tap detection, DS-02)

Subsystem: tap detection (ESP32-S3 tile). Files: `panel_sync/main/tap_detect.c/.h`, run by the Orientation Task (`panel_sync/main/orientation.c`); host tests `panel_sync/test/test_tap_detect.c`, cases K1 to K8 ("Tap: Pass = 15 of 15" on 25 Nov, plain and with ASan). Coding standard: Barr-C:2018. Written 25 Nov 2026 against `a6d4902`.

**Names.** Public: `tap_detect_init()`, `tap_detect_set_threshold()`, `tap_detect_sample()`, `tap_detect_last_mg()`. Statics: `td_deviation_mg()`, `td_baseline()`, `td_phase()`. The module is pure C with no ESP-IDF include, so every rule runs in the host test on a fake clock.

Each entry gives the design choice, where it comes from (`docs/plan/game_plan_v2.md` DS-02, the bench records in `docs/bench/`), and a question an examiner is likely to ask.

## 1. Software detection instead of the IMU's tap engine

- Source: bench 9 Nov 2026, F-05 (`docs/bench/9nov/feasibility.md`); DS-02.
- Choice: the tile detects taps itself from raw accelerometer samples. The QMI8658's tap engine at 100 Hz caught only about 70% of taps, saw too little of a tap's peak, and its 150 ms refractory let a slap that rings for up to 600 ms count twice. Software at 1 kHz fixes all three, and its rules are host-tested.
- Examiner: "Why write your own detector when the IMU has one built in?"

## 2. 1 kHz at +/-16 g

- Source: DS-02; F-05 and F-10.
- Choice: the Orientation Task reads the IMU every 1 ms (a 1 ms `esp_timer`, because the FreeRTOS tick is 100 Hz) at +/-16 g, 2,048 LSB per g. Slaps reached 15 to 28 g on the bench. At +/-8 g a slap and its echo on the next tile would both clip at the same value and tie under the Pico's "highest peak wins" rule.
- Examiner: "Why +/-16 g, when a fingertip tap is well under 1 g?"

## 3. Deviation from a held baseline

- Source: DS-02 ("baseline per axis, EMA, tau 500 ms, held during a spike").
- Choice: each axis keeps a running average with a 500-sample time constant (`TAP_DETECT_EMA_SAMPLES`), which stands for gravity. The deviation is the distance from the baseline over the three axes, in mg. The baseline is held while a tap or any sample over 100 mg is under way (`TAP_DETECT_HOLD_MG`), so the tap does not drag gravity with it. After 1,000 held samples (1 s) it is reseeded from the current sample. That recovers from a bad first sample, or from a tile turned to a new orientation (K6, K7).
- Examiner: "Why subtract a baseline at all, instead of a fixed 1 g?" (Gravity sits on a different axis for each of the four tile orientations, and the mount is never exactly upright.)

## 4. Threshold 300 mg, set from the website

- Source: DS-02 (200 mg from F-05: 17 to 20 of 20 soft taps at 150 to 300 mg); bench 12 Nov 2026, step 3 round 5.
- Choice: the first sample at or over the threshold starts a tap, and its time is the tap time. The default was 200 mg until 12 Nov. On the wall, moving a held tile gave peaks of 200 to 300 mg while real taps had a median of 561 mg, so the default became 300 mg (`TAP_DETECT_THRESHOLD_MG`). One soft tap of 30 went unseen, and the operator chose to keep 300 mg. ARM carries a threshold from the website, 50 to 5,000 mg in 20 mg steps (A12), so the operator can tune it per room without reflashing (K8).
- Examiner: "A child taps softly and nothing happens. What do you change, and where?"

## 5. Peak over 10 ms, reported at the end of the window

- Source: DS-02; DS-03 (the Pico picks the tapped tile by the highest peak).
- Choice: the peak is the largest deviation in the 10 ms after the first crossing (`TAP_DETECT_PEAK_WINDOW_US`). The event (tap time, peak in mg) is reported when the window ends, so it costs at most 10 ms. The tile then sends the TAP to the Pico at once (`tile_net_DEFENCE.md` section 11). The peak matters because a hard tap shakes the neighbours too: on sponge mounts the tapped tile had the highest peak in 40 of 40 taps and slaps (F-10), against 34 of 40 slaps on the bare box (K2).
- Examiner: "Why send the peak at all, instead of just 'I was tapped'?"

## 6. One tap per slap: dead time, quiet, cap

- Source: DS-02; bench 11 Nov 2026, round 13.
- Choice: after a tap the detector re-arms no sooner than 250 ms later (`TAP_DETECT_DEAD_US`), then once the deviation has stayed under half the threshold for 30 ms (`TAP_DETECT_QUIET_US`), and in any case at most 600 ms after the tap (`TAP_DETECT_REARM_MAX_US`, so continuous shaking cannot block it forever). The 250 ms came from round 13: a 16.9 g slap rebounded with 652 mg 146 ms later, after a short quiet spell, and counted again. A mole is gone after its hit and the next mole is never on the same tile, so a second tap within 250 ms means nothing (K3, K4, K5).
- Examiner: "Two quick taps 200 ms apart on different tiles. Does the second one count?" (Yes: each tile has its own detector. Only the same tile is blind for 250 ms.)

## 7. Where the rest of the tap path lives

| Step | Where | Doc |
| --- | --- | --- |
| Detect, peak, time (tile local clock) | `tap_detect.c` in the Orientation Task, core 1, priority 5 | this doc |
| 2 ms GPIO4 pulse on detection | `display_engine_tap_pulse()` | `display_engine_DEFENCE.md` section 9 |
| Convert to Pico time, send TAP, resend every 50 ms until ACKed | `tile_net.c` | `tile_net_DEFENCE.md` section 11 |
| Group echoes, highest peak wins, 150 ms collect wait, score | `game_rules.c` on the Pico | `game_rules_DEFENCE.md` |
| Tap test on the website (every tap lights its tile, peak shown) | `pc/web.py`, coordinator | `pc_tools_DEFENCE.md` section 9 |

## 8. Results and limits

| Requirement | Target | Result | Verdict |
| --- | --- | --- | --- |
| NR-01 taps detected | 95% of 50 deliberate taps per tile | soft taps of 370 to 1,348 mg all registered (19 Nov, 8 taps); 1 soft tap of 30 missed at 300 mg (12 Nov) | Built, not measured (the 50-tap card was never run) |
| NR-02 false taps | at most 1 false tap per round | 0 wrong in the 24 Nov one-tapper rounds (34 of 34 hits) | Built, not measured |
| NR-03 tap to hit burst | under 300 ms | median about 171 ms, worst about 196 ms; this module's share is at most 10 ms | Met (25 Nov) |

Limits to say before you are asked:

1. **A twist reads as a tap.** The detector cannot tell a tap from the jolt of turning a tile (F-06). The gyroscope check (F-09: taps under 2.4 degrees at 50 ms, turns over 8 degrees) is parked with DS-04, and any touch on a mole whacks it (option A, 9 Nov).
2. **Moving a held tile** peaks at 200 to 300 mg, just under the threshold. Children do not hold the tiles, and a gripped tile held still triggers nothing.
3. NR-01 and NR-02 were never run as formal cards.

## Questions to practise before the demo

Answer each in your own words, without reading the code.

1. Why did the IMU's own tap engine fail on the bench, and what three things does software at 1 kHz fix?
2. Walk through one tap from contact to the TAP leaving the tile, with the time each step takes.
3. Why 250 ms of dead time? Describe the bench event that set it.
4. Why does the tile send its peak, and what bench result shows the highest peak finds the tapped tile?
5. Why did the default threshold move from 200 to 300 mg, and what did that cost?
