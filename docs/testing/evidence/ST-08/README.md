# ST-08: Whole-wall rotation

**Requirements.** Whole-wall rotation in video mode (tests C35 to C38); FR17

**Set-up.** 3x3 wall playing a video.

**Procedure.** Part A: turn the whole board 10 times (clockwise, anticlockwise, 180 degrees), waiting for the video each time. Part B: turn one tile a quarter turn and back, 10 times, on different tiles.

**Pass rule.** Every board turn detected with the right turn within 2 s, the map turned correctly and the video playing again within 10 s; no single-tile turn read as a wall turn.

**Result.** Part A: **10 of 10 detected with the right turn**, detection at most 1.8 s, playing again at most 7.9 s (mean 7.2 s). Part B: **0 of 10** single-tile turns moved the wall; the video played throughout. **Verdict: PASS.**

## Files

| File | What it is |
|---|---|
| `nr08_monitor.log, pico_nr08.log` | website state and Pico log of part A |
| `turn2_monitor.log, pico_turn2.log` | the first turn test, with the timing of each step |
