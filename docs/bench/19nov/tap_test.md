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

## Freeze investigation, 19 Nov 2026 afternoon (parked until the old router is back)

Paced sends (`77caae7`) did not stop the freezes: the next tap test froze after 9 hard taps. A second Pico W (`…E66430A64B608538`, MAC `2c:cf:67:d7:d0:72`, 192.168.1.153, now the bench Pico) froze the same way after 3 hard taps, and twice during the router join after a restart.

**Reproducer, no tapping needed** (`tools/tap_burst.py`, run from `Led_Matrix` with the website up and the tap test ON):
`.venv/bin/python docs/bench/19nov/tools/tap_burst.py <pico ip> <session> 8 0.7 360`
It sends valid TAPs for tiles 1 to 8 at once every 0.7 s (a hard tap felt by the wall), so the Pico ACKs, relays and lights 8 tiles per burst. It stops when the website sees no STATUS for over 3 s. **Froze after about 2.5 min** (watchdog restart; breadcrumb `0x8010720d`: inside a UDP send to a tile, port 4210).

| Test | Result |
|---|---|
| Two Pico boards | both freeze: not one bad board |
| Pico on a phone charger | froze: not laptop power |
| Pico on the table, off the tapped board | froze: not vibration |
| Paced sends, lwIP heap 16 KB | froze |
| 480 junk datagrams in bursts of 8 (`tools/junk_burst.py`) | no freeze |
| 90 s of valid TAP bursts, tap test off | no freeze |
| valid TAP bursts, tap test on (tiles lit) | **froze after about 2.5 min** |
| WiFi SPI at half speed (`CYW43_PIO_CLOCK_DIV_INT=4`) | the chip failed to start (`[CYW43] Failed to start CYW43`); reverted |

The driver's own waits in a send are bounded (credit stall 1 s, bus wake 1 s, `cyw43_ll.c`, cyw43-driver v1.1.1), so the hang that outlasts the 8 s watchdog is below them, in the PIO/DMA SPI transfer to the chip. On the old Matrix router (12 Nov) the same firmware ran full rounds on nine tiles with no freeze.

**Next, when the old router is back:** run the reproducer on it for at least 6 min. No freeze there means the new router (Asus RT-AC87U) is the cause; use the old one for the demo. If it still freezes: in the tap test light only the strongest tile of a burst (about half the traffic), and rely on the watchdog (about 20 s and a lost round per freeze). Until then the watchdog keeps the wall recovering by itself; soft and normal taps never froze it.
