# orientation: design defence (T-B5-05, T-B5-06)

Owner: Jia Jun (Buddy 5). Written for Jia Jun in Jia Jun's session (29 to 30 Oct 2026), to review, change where needed, and defend. Files: `panel_sync/main/orientation.c/.h`, `panel_sync/main/orient_filter.c/.h`; host tests `panel_sync/test/test_display.c` O1 to O3. Procedure: `docs/week13/orientation_trials.md`. Coding standard: Barr-C:2018, deviation D-2. Updated 25 Nov 2026 against `a6d4902`: section 2 is corrected to the 1 kHz read, tap detection moved to `tap_detect_DEFENCE.md`, and section 8 covers whole-wall rotation.

**Names.** Public: `orientation_start()`, `orientation_current()`. The plan's `orientation_task()` and `qmi_read_accel()` keep their names as statics. The plan's `or_classify()` is `orient_filter_classify()` in the pure module `orient_filter`, with the debounce and fault rule (`orient_filter_step()`), so T-B5-05's host tests run without a tile.

## 1. Accelerometer only, upright tile

- Source: SE15 (1).txt bullet 9 "If the wall is upright, the accelerometer alone gives absolute rotation without gyro integration. State the mounting assumption"; VA-5.4.
- Choice: gravity in X and Y gives the quadrant; mounting within 20 degrees of vertical (proposed, stated in the README and `orient_filter.h`). The dominant axis must carry at least 0.5 g, otherwise there is no candidate.
- Examiner: "The tile is laid flat on the table. What does your task report, and why?"

## 2. 50 Hz orientation from a 1 kHz read, 20 ms I2C timeout

- Source: T-B5-05 "50 Hz"; "Every I2C transaction has a timeout no longer than one 20 ms sample period"; DS-02 (tap detection needs about 1 kHz).
- Choice (changed 11 Nov 2026 for tap detection): a 1 ms `esp_timer` wakes the task, because the FreeRTOS tick is 100 Hz and cannot time 1 kHz. Every sample goes to the tap detector and every 20th to `orient_filter` (`OR_ORIENT_DIVIDER`), so the orientation still runs at 50 Hz and sections 3 to 5 are unchanged. The QMI8658 runs at +/-16 g and 1,000 Hz (CTRL2 0x33). Every `i2c_master_*` call has a 20 ms timeout.
- Examiner: "Your orientation is computed at 50 Hz, but you read the IMU at 1 kHz. Why, and what stops the extra reads from delaying a display tick?"

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

## 7. Tap detection lives in its own doc

The Orientation Task also runs the software tap detector on every 1 kHz sample. Its design, numbers and tests are in `tap_detect_DEFENCE.md`. The QMI8658's own tap engine and its interrupt pin are no longer used (bench 9 Nov 2026, F-05: it caught about 70% of taps).

## 8. Used by whole-wall rotation (24 Nov 2026)

Each tile's stable orientation goes to the Pico in TELEMETRY every 1 s. The Pico's `wall_turn.c` compares every tile with its own first report: if all active tiles turned by the same quarter turns within a 1.5 s window, the wall turned and the map rotates (video mode, D-17). NR-08 met on 24 Nov: 10 of 10 board turns, 0 of 10 single-tile turns read as a wall turn. The 26-sample debounce matters here, because a tile must settle before the Pico's window closes.

## Settled values (25 Nov 2026)

- QMI8658: WHO_AM_I 0x05; CTRL1 0x40 (auto-increment, little-endian); CTRL2 0x33 (+/-16 g, 1,000 Hz); CTRL7 0x01 (accelerometer on); data from 0x35. Address 0x6A or 0x6B, both tried. No interrupt pin is used.
- Quadrant mapping +Y = 0, +X = 90, -Y = 180, -X = 270; codes rise with clockwise turns; mount offset 3 quarter turns (5 Nov).
- 0.5 g on the dominant axis (1,024 LSB at +/-16 g), 26 equal samples at 50 Hz.
- Re-initialise every 1,000 failed samples (1 s).
- Mounting: the wall stands upright, within 20 degrees of vertical.

## Questions to practise before the demo

Answer each in your own words, without reading the code.

1. Why does an upright wall need only the accelerometer for rotation, and what would a flat wall need?
2. Explain how 26 samples at 50 Hz becomes about 0.5 s, and why a flicker restarts the count.
3. The IMU cable comes loose during a show. What does the wall do, and what does the website show?
4. How does the Pico tell a whole-wall turn from one tile being turned?
