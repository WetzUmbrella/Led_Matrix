# ST-01: Video sync between tiles

**Requirements.** NFR1 (skew at most 5 ms), NFR2, FR7, FR8

**Set-up.** 3x3 wall upright on the router `MATRIX`, video mode, the GIF `sync_3x3.gif` (each tile shows a counter that steps on every 60 Hz tick, so a picture of two tiles shows their frame difference). Tiles 1 to 3 were unpowered during these runs; tiles 4 to 9 played.

**Procedure.** Film two tiles at a time, square-on, with a phone in 240 fps slow motion (iPhone 17 Pro Max) for about 15 s of real time. Cut each video into 10 s pieces (about 1.25 s of real time, about 63 counter steps). `tools/sync_video.py` finds each tile's counter steps and measures B - A per step (`--width 960 --fps 240 --threshold 200`). A piece is used only when its implied frame rate is within 2 % of 240 fps. Every step over 4 ms is checked from both tiles' decoded values, and every step over 5 ms by eye on crops.

**Reader check first.** On synthetic videos with a known skew (`tools/synth_sync_video.py`), every median was within 0.15 ms of the true skew (0, 3, -7, 12 and 20 ms), so the reader is trusted.

**Pass rule.** Every real step in the usable pieces under 5 ms, and no tile a whole frame (16.7 ms) behind.

**Result.**

| Run | Tiles | Firmware | Result |
|---|---|---|---|
| `IMG_3429` | 4, 5 | before the drift fix | every real step under 5 ms; medians -0.4 to +1.8 ms |
| `IMG_3430` | 7, 8 | before the drift fix | **FAIL**: 7 real steps of 5.0 to 8.6 ms (tile 8 drifting ahead) |
| `IMG_3433` | 7, 8 | `cbe7763` (drift limited to 50 ppm) | PASS, medians +0.06 to +0.25 ms, no real step at or over 5 ms |
| `IMG_3434` | 4, 5 | before the fix | PASS |
| `IMG_3437` | 7, 8 | `cbe7763` | PASS |
| `IMG_3436` | 8, 9 | `cbe7763` | PASS |
| `IMG_3435` | 5, 6 | `cbe7763` | PASS (only 3 usable pieces) |

The failure led to the fix: the drift estimate came from two beacon minima only 0.8 s apart and reached 1,500 ppm (median), far beyond a crystal's 50 ppm, so it is now limited to 50 ppm (see ST-07 for the replay that found it). **Verdict: FAIL, fixed, then PASS** on tiles 4 to 9.

**Limits.** Each recording is about 15 s of real time, not the 10 minutes of NFR1. Tiles 1 to 3 were not filmed. Each step is timed to about one camera frame (4.17 ms). The videos are not in the repository (about 1 GB); the logs below are the analysis of each.

## Files

| File | What it is |
|---|---|
| `nr07_segments_3429.log, nr07_segments_3430.log` | per-piece analysis before the fix |
| `nr07_outliers.log` | every step over 4 ms before the fix, checked |
| `nr07fix_segments_343[3-7].log` | per-piece analysis after the fix |
| `nr07fix_outliers.log` | every step over 4 ms after the fix, checked |
| `tools/` | the video reader (`sync_video.py`), the synthetic check (`synth_sync_video.py`) and the helpers that cut, box and check the pieces |
