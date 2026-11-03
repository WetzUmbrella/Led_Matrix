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

## PROVISIONAL list for Jia Jun to confirm

- QMI8658 register values (CTRL1 0x40, CTRL2 0x06, CTRL7 0x01, WHO_AM_I 0x05, data from 0x35), from the datasheet register map; check on the board.
- Quadrant mapping (+Y = 0, +X = 90, -Y = 180, -X = 270) and the 0.5 g threshold.
- Re-initialise every 50 failed samples.
- 20 degree mounting assumption (proposed).

## Questions for Jia Jun before Tribunal 2

1. Explain how 26 samples at 50 Hz becomes "0.5 s", and why the first sample counts.
2. Describe the T-B5-06 result for each orientation and what a wrong-direction result would have made you change.
3. Explain why raw IMU samples stay 16-bit while the orientation is 2 bits (VB-5c).
