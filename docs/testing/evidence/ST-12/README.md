# ST-12: Pause policy, tile unplugged during video

**Requirements.** Failure policy option (pause)

**Procedure.** As ST-11, with the pause policy: unplug tile 9 for about 3 s.

**Pass rule.** The wall pauses, then returns to PAUSED when the tile is back.

**Result.** The wall paused, but 5 s after the loss the Pico went from RECOVERING to FAULT. In FAULT the Pico's loop only re-joins the router, although the link was up, and the join ran past the 8 s watchdog, which restarted the Pico; the website then re-published the wall by itself. **Verdict: FAIL (open).** The fix (a tile loss must not re-join a router that is still up) was not made before the demo, so the demo runs the continue policy (ST-11).

**Files.** `powerpull_pauseA_state.log` and `pico_serial_1425.log` in [`../ST-11/`](../ST-11/).
