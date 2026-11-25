# orientation: design defence (T-B5-05, T-B5-06)

Owner: Jia Jun (Buddy 5). Written for Jia Jun in Jia Jun's session (29 to 30 Oct 2026), to review, change where needed, and defend. Files: `panel_sync/main/orientation.c/.h`, `panel_sync/main/orient_filter.c/.h`; host tests `panel_sync/test/test_display.c` O1 to O3. Procedure: `docs/week13/orientation_trials.md`. Coding standard: Barr-C:2018, deviation D-2.

**Names.** Public: `orientation_start()`, `orientation_current()`. The plan's `orientation_task()` and `qmi_read_accel()` keep their names as statics. The plan's `or_classify()` is `orient_filter_classify()` in the pure module `orient_filter`, with the debounce and fault rule (`orient_filter_step()`), so T-B5-05's host tests run without a tile.

## 1. Accelerometer only, upright tile

- Source: SE15 (1).txt bullet 9 "If the wall is upright, the accelerometer alone gives absolute rotation without gyro integration. State the mounting assumption"; VA-5.4.
- Choice: gravity in X and Y gives the quadrant; mounting within 20 degrees of vertical (proposed, stated in the README and `orient_filter.h`). The dominant axis must carry at least 0.5 g, otherwise there is no candidate.
- Examiner: "The tile is laid flat on the table. What does your task report, and why?"

## 2. 50 Hz with vTaskDelayUntil, 20 ms I2C timeout

- Source: T-B5-05 "50 Hz by `vTaskDelayUntil` every 20 ms"; "Every I2C transaction has a timeout no longer than one 20 ms sample period".
- Choice: absolute 20 ms schedule; every `i2c_master_*` call has a 20 ms timeout, so one stuck transaction costs at most one sample.
- Examiner: "Why `vTaskDelayUntil` and not `vTaskDelay(20)`?"

## 3. 26 equal samples before a change

- Source: IFM-6b; T-B5-05 "published only after 26 consecutive equal samples (0.5 s at 50 Hz)".
- Choice: a candidate different from the stable orientation must repeat 26 times in a row; any other sample restarts the count. Host test O1: 25 samples give 0 changes, 26 give 1; O3: a flicker at sample 26 restarts it.
- Examiner: "Someone carries the tile past the 45 degree line and back in 0.3 s. Does the picture rotate?"

## 4. Keep the last orientation on an I2C error

- Source: SRM-6E; T-B5-05 "on an error the last orientation is kept, the IMU condition bit is set, and monitoring retries".
- Choice: a failed read keeps the stable value, sets the IMU condition bit once (this module owns it and clears it on the next good sample) and the task re-initialises the IMU once a second while failures continue. Host test O2: 100 injected failures keep the orientation in 100 of 100, set the bit, and the bit clears when they stop.
- Examiner: "The IMU cable is unplugged during a show. What does the wall do, and what does the dashboard show?"

## 5. Length-1 mailbox to the Display Engine

- Source: VA-3.7 "`xQueueOverwrite()` only for a length-1 mailbox"; IFM-6a.
- Choice: ORIENTATION_CHANGED is `xQueueOverwrite()` of the 2-bit value; the Display Engine peeks it once per frame preparation, so it always uses the newest stable value and never blocks.
- Examiner: "Why a mailbox and not a queue of changes?"

## 6. Priority 5 on core 1

- Source: W6D-4a (proposed): Orientation 5, core 1.
- Choice: created with `xTaskCreatePinnedToCore()` from `tile_tasks.h`; below the Display Engine (20) on the same core, so a slow I2C read can never delay a tick.
- Examiner: "Your orientation task shares core 1 with the Display Engine. Prove it cannot make a tick late."

## 7. Tap detection (whack-a-mole re-plan, build step 2)

- Source: `docs/plan/whack_a_mole_replan.md` "A press is a QMI8658 IMU tap"; TAP carries a peak, and dedupe keeps the highest peak.
- Choice (rewritten 11 Nov 2026, build step 2, DS-02 in `docs/plan/game_plan_v2.md`): taps are detected in software from the accelerometer samples; the QMI8658 tap engine and its interrupt pin are no longer used. On the 9 Nov bench (`docs/bench/9nov/feasibility.md`, F-05) the tap engine at 100 Hz caught about 70% of taps, and its 150 ms refractory let a slap that rings for up to 600 ms count twice.
- Sampling: a 1 ms `esp_timer` wakes the Orientation Task, because the FreeRTOS tick is 100 Hz and cannot time 1 kHz. Every sample goes to `tap_detect`; every 20th goes to `orient_filter`, so sections 2 and 3 still hold at 50 Hz. The task (priority 5) stays below the Display Engine (20) on core 1, so a read never delays a frame.
- Detector (`tap_detect`, pure C, host tests K1 to K8): a per-axis baseline (500 ms average) stands for gravity and is held during a tap; the deviation from it, in mg, must reach the threshold (200 mg by default, F-05: 17 to 20 of 20 soft taps); the peak is the largest deviation in the 10 ms after that; the detector re-arms only after 30 ms under half the threshold, or 600 ms at most, so a ringing slap counts once. A bad first sample, or a tile turned to a new orientation, is reseeded after 1 s of held baseline.
- Range: +/-16 g (2,048 LSB per g; the 0.5 g orientation threshold is 1,024). Slaps reached 15 to 28 g on the bench; at +/-8 g a slap and its echo on the next tile would both clip and tie under "highest peak wins". The TAP peak is sent in mg.
- Limit: the detector cannot tell a tap from the jolt of turning a tile (F-06); the gyroscope check for that (F-09) is parked until whole-wall rotation (DS-04).
- Examiner: "Why not use the IMU's own tap engine?" (It missed about 30% of taps on the bench, at 100 Hz it saw too little of a tap's peak, and its refractory let a ringing slap count twice; software at 1 kHz fixes all three and is host-tested.)

**Tuning on the board.** Each tap prints `TAP t_us= peak_mg= count=`. Tap each tile, then tap its neighbours and knock the board, and compare the peaks. The threshold is `TAP_DETECT_THRESHOLD_MG` in `tap_detect.h` (to be set from the website, A12); the windows are `TAP_DETECT_PEAK_WINDOW_US`, `TAP_DETECT_QUIET_US` and `TAP_DETECT_REARM_MAX_US`.

## PROVISIONAL list for Jia Jun to confirm

- QMI8658 register values (CTRL1 0x48, CTRL2 0x24, CTRL7 0x01, CTRL8 0xC1, CTRL9 command 0x0C, WHO_AM_I 0x05, data from 0x35, STATUS1 bit 1, TAP_STATUS 0x59). The tap values were written from memory of the datasheet, with no copy to hand: check them against it before tuning.
- INT1 on GPIO10 (default) or INT2 on GPIO13, picked by `OR_TAP_INT_LINE` in `orientation.c`: both unconfirmed, check the board schematic. ESPHome's board page notes IRQ1 works less well than IRQ2, so try 2 if no `TAP` lines print.
- Tap engine timing (20 + 30 + 20 ms windows at 500 Hz, about 70 ms before the interrupt). This takes most of the 100 ms tap-to-flash target.
- Quadrant mapping (+Y = 0, +X = 90, -Y = 180, -X = 270) and the 0.5 g threshold.
- Re-initialise every 100 failed samples (1 s).
- 20 degree mounting assumption (proposed).

## Questions for Jia Jun before Tribunal 2

1. Explain how 26 samples at 50 Hz becomes "0.5 s", and why the first sample counts.
2. Describe the T-B5-06 result for each orientation and what a wrong-direction result would have made you change.
3. Explain why raw IMU samples stay 16-bit while the orientation is 2 bits (VB-5c).
