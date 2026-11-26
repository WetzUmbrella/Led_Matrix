# ST-06: Pico restart during playback

**Requirements.** FR21, NFR8 (back to READY within 30 s)

**Set-up.** 3x3 wall playing a video, website running.

**Procedure.** Restart the Pico with `picotool reboot -f` while the video plays, and touch nothing.

**Pass rule.** The wall comes back to READY by itself.

**Result.** The website saw the STATUS stop, then "Pico back: publishing again" 6 s later; all 9 uploads succeeded and the wall was READY with every tile ready **12 s after the restart** (16:52 run, these logs). Repeated at the end of ST-07 with all tiles on the final firmware: **READY 27 s after the restart**. Before the tile fix of 24 Nov the same restart left the tiles playing and refusing uploads until they were power-cycled. **Verdict: PASS.**

## Files

| File | What it is |
|---|---|
| `restart_stop_monitor.log` | website state every 0.1 s across the restart |
| `pico_restart_stop.log` | Pico serial log |
