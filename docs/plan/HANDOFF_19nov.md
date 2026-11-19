# Session handoff, 19 Nov 2026 (watchdog, freeze investigation parked, website additions)

Written by the orchestrating agent for itself (or any agent) starting cold. **This file supersedes `HANDOFF_18nov.md` for "where things stand"**; read that one for the bench setup (section 2), the method additions (section 4) and the open items it lists, then this one.

Read before acting, in this order:
1. This file.
2. `docs/bench/19nov/tap_test.md`: the tap tests, the freeze investigation and its reproducer (parked).
3. `docs/bench/18nov/wall_test.md`, last two sections (watchdog, router settings).
4. `HANDOFF_18nov.md` sections 2 and 4.

## 1. Where things stand

| Item | State |
|---|---|
| Branch | `feature/whack-a-mole`, pushed; origin has every commit of 19 Nov. Working tree clean. |
| Website (step 4e) | Done and wall-tested. New today: arm confirmation, tap log, Remove action (section 2). |
| Pico freezes | **Parked by the user until the old Matrix router is back.** Reproducer ready (section 3). The wall heals itself meanwhile (watchdog, about 20 s per freeze). |
| Gate | Passes at head: C host tests and ASan (coordinator 38 of 38, tile tests incl. G7), barr_check 0 findings, pytest 503. |

### Boards on the bench

| Board | Firmware | Notes |
|---|---|---|
| **Pico `…E66430A64B608538`** (new bench Pico) | head (`0007a53` and later; no Pico change after it) | 192.168.1.153, MAC `2c:cf:67:d7:d0:72`. Start the website with `--pico 192.168.1.153`, or give it the .154 reservation in the router. |
| Pico `…E66430A64B595138` (old) | watchdog build | Off the bench; froze the same way, so not a bad board. |
| Tiles 1 to 9 | `77e3165` (Pico-restart fix) | No tile change since. |

Website now running: `pc/web.py --pico 192.168.1.153 --layout 2x2 --map 2,3,5,6 --gif frog_ripple_64x64.gif` (tap log in `pc/logs/`).

## 2. Changes of 19 Nov (all committed, tested)

Pico (`ledwall_sync/coordinator_loop.c`, Benjamin's module; tell him):
- **8 s hardware watchdog** (`7599f4f`): restarts a frozen Pico; the router join feeds it every 100 ms instead of blocking 10 s; fed only while `scratch[4]` is unchanged, so `picotool reboot` still works. Test build `cmake -DCL_TEST_HANG_S=30` in `ledwall_sync/build_hang` (gitignored).
- **Breadcrumbs** (`5690403`, `0007a53`): the loop step in watchdog `scratch[0]`, kept by a watchdog reset; `CL_STATS ... wd_boot wd_step wd_prev`. Decode: bits 0 to 7 the step (1 poll, 2 beacon, 3 scan, 4 timed copy, 5 outbox, 6 status, 7 game, 8 dispatch, 9 log, 10 join, 11 connect, 12 STA mode, 13 queued send), bits 8 to 23 detail (dispatch: first byte and port less 4210; queued send: port), bit 31 inside a UDP send.
- **A tile switched off goes blank** (`2137f5b`, host test C32): the Pico clears every registered unplaced tile on a new layout, at ARM and START, and when results end (A3).
- **One send per loop pass** (`77caae7`, host test C33): every send but BEACON and the timed copies goes through a 32-entry queue; lwIP `MEM_SIZE` 16000. Did not stop the freezes, kept as hygiene. `CL_STATS tx_drop tx_fail`.

Website (`pc/`):
- **Arm confirmation** (`1df4ea1`): the Pico ACKs a refused ARM, so the website waits 1.5 s for STATUS to show the round armed, else 409 with the Pico's state; round errors also show under the Game buttons.
- **Tap log** (`6b0784a`): every TAP to `pc/logs/taps_<start>.csv` (gitignored) with PC time, tile, peak, the tile's own `t_tap_us` (group echoes by it within 30 ms), mode, tap test, round, session. `--tap-log none|path`.
- **Remove a tile** (`f9d4189`): the x on a grid cell takes the tile out of the grid (cell freed; off keeps it). `POST /api/remove {slot}`.

Findings:
- `/score` "stopping at 4 s" was Chrome throttling a background tab; on a visible screen it counts to 0.
- Tap strengths on the 3x3 board (tile 5): soft 370 to 1,350 mg (no echoes), normal 3 to 8 g (echoes 300 to 1,000 mg on neighbours), hard 13 to 17.7 g (echoes up to 2 g on up to 7 tiles). The 300 mg default suits children; echoes stay under about 11% of the real tap.
- Router 2.4 GHz extras turned off on the Asus RT-AC87U (Turbo QAM, beamforming, TX bursting, airtime fairness, roaming assistant; WPA2-AES): three clean rounds first, but hard-tap freezes continued.

## 3. Next, in order

1. **INF2001 M2** due Fri 20 Nov, 23:59 (not this repo).
2. **Tribunal 2, Mon 23 Nov: defence prep with Cheston** (his modules: `tile_net` + `game_tile` restart fix `77e3165` and G7, `asset_rx` held upload, parallel uploads in the PC uploader). Messages to send: Benjamin (coordinator changes above, plus `HANDOFF_12nov_late.md` section 7 drafts), Ahsanul (time sync note in the same section).
3. **When the old Matrix router is back:** run the reproducer on it for at least 6 minutes, website up and the tap test on: `.venv/bin/python docs/bench/19nov/tools/tap_burst.py <pico ip> <session> 8 0.7 360`. No freeze: the new router is the cause, use the old one for the demo. Freeze: in the tap test light only the strongest tile of a burst, and keep relying on the watchdog.
4. Smaller: group taps by tile time stamp on the Pico (echo splitting on hard slaps); animated preview (not asked).

## 4. Method notes from today

- A freeze is real only if the website shows no STATUS for over 3 s **and** the Pico's USB reconnects (watchdog restart); one lost ping is not a freeze.
- The breadcrumb of the boot before is in `wd_prev`; a freeze while rejoining no longer hides the first one.
- `CYW43_PIO_CLOCK_DIV_INT=4` stops the WiFi chip from starting; do not slow the SPI that way.
- The bench shell refuses `sleep` before other commands; wait with until-loops on a condition (ping, STATUS age).
