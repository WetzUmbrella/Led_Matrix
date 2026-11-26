# ST-11: Continue policy, tile unplugged during video

**Requirements.** FR20, NFR8, NFR10

**Set-up.** 3x3 wall playing a video with `pc/web.py --policy continue`.

**Procedure.** Unplug tile 9 for about 3 s, then for about 10 s.

**Pass rule.** The other tiles keep playing; tile 9 rejoins in step.

**Result.** 3 s pull: lost 14:33:37.3, back 14:33:44.1; 10 s pull: lost 14:34:28.6, back 14:34:39.1. The wall stayed in COORDINATING PLAYBACK both times and tile 9 rejoined in step with its neighbours, with no re-upload. The Pico's counters show no restart. **Verdict: PASS.**

## Files

| File | What it is |
|---|---|
| `powerpull_continue_state.log` | website state through both pulls |
| `pico_serial_1435.log` | Pico serial log |
| `powerpull_pauseA_state.log, pico_serial_1425.log` | the pause-policy pull (ST-12) |
