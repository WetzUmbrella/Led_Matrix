# ST-10: Set-up time

**Requirements.** NFR7 (power-on to the first armed round within 3 minutes)

**Procedure.** As ST-09 run 1: Pico and tiles power-cycled, then the website started.

**Pass rule.** READY within 3 minutes of power-on, with the tiles already placed.

**Result.** Website restart alone: about 3 s. Cold start: each tile ready 2 to 3 s after it joins, the wall READY 2.3 s after the last tile. The tiles were plugged in by hand over about a minute, so the total is not a single figure, but every stage is seconds. **Verdict: PASS.**

**Files.** The logs are in [`../ST-09/`](../ST-09/).
