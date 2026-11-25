# Power-pull test, 25 Nov 2026 (video playback, 9 tiles, tile 9 pulled)

Pico `…595138` at .154 on `f9d313c`, tiles on `630c390`, router `MATRIX`. State logs from the website's `/api/state` (read only): `powerpull_pauseA_state.log`, `powerpull_continue_state.log`; Pico serial `pico_serial_1425.log`, `pico_serial_1435.log`.

## Pause policy (website default)

| Pull | Result |
|---|---|
| A, about 3 s | 14:22:33 tile 9 lost, wall paused (RECOVERING). 14:22:40 Pico silent. 14:23:06 Pico back with a fresh session; website re-uploaded tiles 1 to 8 by itself. CL_STATS after: `wd_boot=1 wd_step=0x0a` (JOIN), `wd_prev=0x0a` |

**Bug found.** Under the pause policy a tile away more than 5 s sends the Pico from RECOVERING to FAULT (`cl_scan_heartbeats`). FAULT is the router-lost state: the main loop only runs `cl_rejoin_routine()`, which re-joins the router although the link is up, and the join hung past the 8 s watchdog. A 3 s cable pull is enough, because the tile then boots and joins WiFi before it is heard again. The 24 Nov 21:28 watchdog restart "while rejoining" has the same breadcrumb. Not fixed (after the demo): a tile-loss FAULT should not re-join a router that is still up.

Pull B (10 s) under pause was not run: pull A already showed the path.

## Continue policy (`pc/web.py --policy continue`, added 25 Nov)

| Pull | Lost | Back | Result |
|---|---|---|---|
| A, about 3 s | 14:33:37.3 | 14:33:44.1 | Wall stayed in COORDINATING PLAYBACK; tile 9 rejoined in sync with its neighbours (user, by eye); no re-upload |
| B, about 10 s | 14:34:28.6 | 14:34:39.1 | Same: playback never stopped, tile 9 rejoined |

Pico after both: `state=8`, `online=0x1ff`, `sent` rose from 1701 (14:25) to 7201 (14:35), so no restart since 14:23.

## Also seen

- Tiles 2, 5, 6, 7 and 9 dropped out on their own between 14:10 and 14:25 (power banks, per the user).
- At start the website's map was the 1 to 9 map turned 180 degrees ([9..1]), probably a whole-wall turn while the board was handled during plugging; the user re-placed every slot. Not confirmed from a log.
