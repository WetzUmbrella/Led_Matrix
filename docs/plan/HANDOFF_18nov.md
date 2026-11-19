# Session handoff, 18 Nov 2026 (new router, website built and wall-tested)

Written by the orchestrating agent for itself (or any agent) starting cold. **This file supersedes `HANDOFF_12nov_late.md` for "where things stand".** The method (sections 2 and 2a of `HANDOFF_12nov.md`) still governs; additions are in section 4.

Read before acting, in this order:
1. This file.
2. `docs/bench/18nov/wall_test.md` (the website on the wall, seven bugs fixed, open items) and `docs/bench/18nov/router.md` (the new router, rounds 37 to 39).
3. `docs/plan/game_plan_v2.md` DS-15 and DS-16 (decisions of 18 Nov at the end of each).

## 1. Where things stand

| Item | State |
|---|---|
| Branch | `feature/whack-a-mole`, head after `6e3c526` (this file). Working tree clean. |
| Step 4e website | **Done and wall-tested**: the whole demo runs from the website (A13). Tabs Setup, Video, Game; `/score`; GIF library in `pc/gifs` (gitignored); preview (first frame); parallel uploads about 3 s for nine tiles. |
| Pictures | Countdown, score and stars show **whole on every tile** (decided 18 Nov, replaces DS-15 decision 3). |
| Pico restart | **Recovers by itself**: the website resends the layout, publishes again in a fresh session and restores the mode; tiles forget the old Pico's game state (`game_tile_new_pico`). Proved on the wall. |
| Gate | Passes at head: C host tests and ASan, barr_check 0 findings, pytest 498 passed (tools and pc). clang-format 18.1.8 not run (offline); CI checks it. |
| Pushed | Yes: origin `feature/whack-a-mole` has every commit of 18 Nov. |

### Firmware on the bench

| Board | Built from | Notes |
|---|---|---|
| Pico `…E66430A64B595138` | watchdog build (19 Nov) | SSID `MATRIX`; whole pictures and the 8 s watchdog. |
| Tiles 1 to 9 | `77e3165` (restart fix) | All nine flashed 18 Nov evening. SSID `MATRIX`. |

## 2. Bench setup (current)

- **Router `MATRIX`** (Asus RT-AC87U; 2.4 GHz; `MATRIX_5G` unused), same password as Matrix. Settings that matter: **fixed channel 11, 20 MHz, DTIM 1, nothing else joined; WPA2-AES only; Turbo QAM, beamforming, TX bursting, airtime fairness and roaming assistant off** (19 Nov: the Pico froze about once a round until these extras were turned off; `wall_test.md` last section); DHCP reservations for the Pico and the tiles (addresses as in `HANDOFF_12nov_late.md` section 4). On auto channel the wall missed ticks and overflowed queues (`router.md`).
- **Laptop**: SIT-WIFI for the internet, the router by a USB Ethernet adapter (192.168.1.226). The adapter's interface name changes on replug; the firewall allows UDP from 192.168.1.0/24 on any interface. For a phone or TV to open the website: `sudo ufw allow from 192.168.1.0/24 proto tcp to any port 8080`.
- **Power**: the unpowered USB hub drops out under tiles. Tiles on their own power; the Pico on a direct laptop port or a charger.
- **Start the website** (from `Led_Matrix`):
  `.venv/bin/python pc/web.py --pico 192.168.1.154 --layout 3x3 --gif numbered_cells_3x3.gif`
  (`--map` for other layouts, reading order by default; the page is http://127.0.0.1:8080, the scoreboard `/score`). Without hardware: `.venv/bin/python pc/tools/web_demo.py`.
- **Flash tiles one at a time** if the hub is flaky: `bash docs/bench/12nov/tools/flash_tiles.sh [SKIP...]` (skip the tile numbers already done).

## 3. Open items, in order

1. **Pico freezes: watchdog done 19 Nov** (`wall_test.md`, last section): an 8 s hardware watchdog restarts a frozen Pico and the website and tiles recover it untouched (about 23 s in all). The freezes themselves (likely CYW43) are not fixed; `wd_boot=1` in CL_STATS shows when one happened. Pico now on the watchdog build.
2. **Echo grouping by arrival time** (`router.md`): hard slaps split into extra wrong taps on this router. Grouping by the tiles' tap time stamps would fix it.
3. **Tribunal 2, Mon 23 Nov.** Cheston must explain his modules, including what changed: `tile_net` + `game_tile` restart fix (`77e3165`, host test G7), `asset_rx` held upload (4c), and the PC uploader now running every tile at once. Ahsanul's time sync (decision A), Benjamin's coordinator (C31 buffer, whole pictures `cl_tile_picture`, SESSION_CONFIG repeat rule) changed too; draft messages are in `HANDOFF_12nov_late.md` section 7 plus: "Countdown and results are now drawn whole on every tile (cl_tile_picture)."
4. Smaller: animated preview (not asked yet); step 5 whole-wall rotation (stretch); GPIO4 pulse measurement (DS-13).

## 4. Method additions from this session

- **Never `pkill -f` or `pgrep -f | kill` with a pattern that appears in the same shell command** (killed the agent's shell twice today, even with the `[x]` trick, because the restart command in the same line contained the plain name). Find PIDs with `ps -eo pid,args | grep ... | grep -v grep` in one command, kill in another, or by port (`ss -ltnp 'sport = :8091'`).
- A serial log that ends in a boot line means the board restarted under it; the logger then stops recording that port. Check `udevadm` ports again after any reset.
- `TN_GAME` is printed for every game datagram a tile handles, **before** its freshness checks: a tile printing `TN_GAME` can still drop the message. Silence means it never arrived.
- The Pico sends game messages only to tiles in its registry (`g_registry[].addr`, set by REGISTER); STATUS `online` comes from other messages and can show a tile online that gets no game messages.
- `dispatch_max_us` is timed from the pass's `now`, taken just before dispatch: a large value is inside `cl_dispatch_entry`.
- Before testing a website fix on the wall, check the running website has it (restart it; `--layout`, `--gif` restore the wall).
- When the user asks to repeat an action ("flash the tiles connected now"), do exactly that; they declined an automatic watcher.
