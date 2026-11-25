# Cold start, 25 Nov 2026 (Linux bench, router `MATRIX` left on)

Demo-day setup: the router is on beforehand; the Pico and the 9 tiles are powered by hand, then the website starts.

## Run 0: website restart only (14:04:37)

Pico and tiles had stayed powered since 24 Nov. Website started 14:04:37.5 (new session); all 9 tiles uploaded in parallel (about 2 s each, 0 chunks resent); wall READY by 14:04:40, **about 3 s**. Log `web_coldstart.log`.

## Run 1: Pico and tiles power-cycled (14:10:01)

Website started when the user typed "on" (`coldstart2_t0.txt`, log `web_coldstart2.log`). The tiles were plugged in one by one over about a minute, so the total is not a system figure. Per-device figures:

| Stage | Time |
|---|---|
| Pico first STATUS after "on" | 32 s (includes plugging the Pico in) |
| Tile online to its video ready | 2 to 3 s, every tile |
| Last tile (9) back to wall READY | about 2.3 s (upload retry 14:13:35, all ready 14:13:37.8) |

Problems seen:
- Tile 9 did not come online and tile 2 came up, then dropped at 139 s. Both were power: the user re-seated them and both came back (tile 2 kept its cached video). Check the power at every tile before the demo.
- While a tile is missing, the website shows `pc_state` **ERROR** ("node 9 not uploaded: no TELEMETRY yet"). Harmless, but it looks bad on screen. UI fix to consider after the bench.

Not measured: one tile's power-on to online (needs a "now" call at the plug).
