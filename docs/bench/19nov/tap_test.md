# Tap test, 19 Nov 2026 afternoon

Full 3x3 wall on `MATRIX` (router 2.4 GHz extras off), threshold at the tiles' default (300 mg), website tap log `taps_20261119_160036.csv` (columns in `pc/tap_log.py`). Four tap tests; **each ended with the Pico freezing** (the watchdog and the website recovered it each time).

Latest test (16:04:23 to 16:04:47, session 196): 68 TAP reports, **23 physical taps** when grouped by the tiles' own time stamp (`t_tap_us` within 30 ms), all aimed at tile 5:

| Taps | Peak on tile 5 | Echoes on other tiles |
|---|---|---|
| 8 soft | 370 to 1,348 mg | none |
| 10 normal | 3,027 to 8,055 mg | tiles 4 and 6 at 416 to 1,010 mg; often 1 and 3 at 300 to 415 mg |
| 4 hard | 13,042 to 17,726 mg | up to 7 tiles, 306 to 1,983 mg |

The Pico froze right after the two hardest taps (8 and 5 reports arriving together). Findings:
- **Echoes stay well below the tapped tile** (at most about 11%), so "highest peak wins" picks the right tile when the reports are grouped.
- **Soft taps register at 300 mg** with no echoes; the default threshold suits children's taps.
- **Bursts of simultaneous TAPs look like the freeze trigger**: each TAP makes the Pico send an ACK, a relay to the PC and, in the tap test, a light command, back to back inside the WiFi driver.
