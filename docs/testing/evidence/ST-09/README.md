# ST-09: Upload of the packed asset

**Requirements.** FR5, NFR5, NFR6 (within 60 s)

**Set-up.** Demo set-up: router on, Pico and 9 tiles powered, then the website started (a new session uploads every tile).

**Procedure.** Start the website and time each tile's upload and the wall's READY, first with the Pico and tiles already powered (run 0), then after power-cycling them (run 1).

**Pass rule.** Every tile ready, every chunk CRC-checked, within 60 s.

**Result.** Run 0: all 9 tiles uploaded in parallel, about 2 s each, 0 chunks resent, the wall READY **about 3 s** after the website started. Run 1: each tile's video ready 2 to 3 s after the tile came online. **Verdict: PASS.**

## Files

| File | What it is |
|---|---|
| `web_coldstart.log` | website log of run 0 |
| `web_coldstart2.log, coldstart2_t0.txt` | website log and start time of run 1 |
| `coldstart_t0.txt` | start time of run 0 |
