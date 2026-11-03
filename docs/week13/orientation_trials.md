# Orientation trials (T-B5-06)

Owner: Jia Jun (Buddy 5). Plan row T-B5-06: "20 trials per orientation (0, 90, 180, 270) on an upright tile within the 20 degree mounting assumption." Pass: "At least 19 of 20 correct for each of the 4 orientations (indicative, not a proven 95%, A9)." Nothing has run yet.

## Set-up

- Build with patch 12 or later (Orientation Task, T-B5-05), tile log open (`idf.py monitor | tee docs/week13/orientation_tile.log`). Check `ORIENT imu ready` in the boot log; `ORIENT imu not found` means the QMI8658 register values in `orientation.h` (PROVISIONAL) need checking first.
- Play the numbered pattern (bench Part F, F1) so the Node ID LEDs show which corner is "top left".
- Mounting: the tile stands upright, within 20 degrees of vertical (the proposed assumption in `orient_filter.h`). A protractor or phone level app gives the angle; note it for each trial.

## One trial

1. Rotate the tile to the target orientation and hold it still.
2. Within 1 s (0.5 s debounce plus margin) the log shows `ORIENT changed <n>` with n = 0, 1, 2 or 3 for 0, 90, 180 and 270 degrees, unless the tile was already there.
3. Correct = the logged n matches the target and the pattern reads upright (Node ID LEDs top left).
4. Return to 0 degrees between trials so each trial is a real change.

The first trial of each orientation also confirms the PROVISIONAL directions: which physical turn is "90" (`orient_filter.h` quadrant table) and which way the picture is turned back (`display_frame.h` remap). If either is the wrong way round, record it, fix the table, and restart the trials.

## Results

| Orientation | Trials | Correct | Wrong n | No change | Angle range | Pass (at least 19) |
|---|---|---|---|---|---|---|
| 0 | 20 | | | | | |
| 90 | 20 | | | | | |
| 180 | 20 | | | | | |
| 270 | 20 | | | | | |

Evidence: this table, `docs/week13/orientation_tile.log`, and a video of one trial per orientation.
