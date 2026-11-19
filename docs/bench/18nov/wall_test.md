# Wall test of the website (step 4e), 18 Nov 2026 evening

Branch `feature/whack-a-mole`. The website (`pc/web.py`) ran against the real wall: Pico `…E66430A64B595138` at 192.168.1.154, nine tiles on the `MATRIX` router (fixed channel 11, 20 MHz, DTIM 1), laptop on the router by USB Ethernet. Operator: Cheston at the wall; agent on the logs. Logs in this folder: `wall*_*.log` and `repro*_*.log` are serial logs named by USB serial number (`E66430A64B595138` the Pico; `9C139EF13070` tile 1, `9C139EF12EB0` tile 4, `9C139EF12C70` tile 7), `wall*_web.log` the website's console.

## Result

**The demo runs from the website alone (A13)** for grid, tap-to-place, tiles on and off, GIF upload, play, pause, resume, stop, game mode, arm, a full round, results and `/score`: the operator reported every step working. Parallel uploads (DS-14): every tile at once, **about 2.5 to 3.2 s for all nine** (was about 22 s one after another), 57 of 57 chunks, resent 0.

Seven bugs found and fixed on the way, all committed with tests:

| # | Seen on the wall | Cause | Fix (commit) |
|---|---|---|---|
| 1 | After choosing a GIF, every tile uploaded but none became ready; `CRC_MISMATCH` for all nine in the Pico log; Play stayed disabled | The Pico ignores a SESSION_CONFIG repeating its active session; the website sent the grid, then the GIF, in one session | Every SESSION_CONFIG after the first goes out in a fresh session (`06b78a0`) |
| 2 | After the Pico restarted, the page still said READY | The Pico sends STATUS only to where the PC last wrote from; the page kept the last STATUS | NO STATUS after 2 s of silence, actions disabled; the last SESSION_CONFIG resent every second until STATUS returns (`bdc2920`) |
| 3 | Only tiles 1, 4, 7 showed the hole in game mode; rounds ran on those three only; earlier, tiles 6, 8, 9 kept round 39's results after a mode switch | **Tile firmware:** on a new or restarted Pico, `tile_net` forgot only `g_game.b_seq_known`; `game_tile` also keeps `ctl_seq` and `clear_seq`, so ARM, START and every picture of the restarted Pico (counting from 0) were dropped as old. Tiles that had just rebooted worked, which hid it | `game_tile_new_pico()` forgets the whole game state; host test G7 (`77e3165`). **All nine tiles flashed.** Proved on the wall: Pico power-cycled with six tiles running, all six joined the next round |
| 4 | A tile could be skipped by an upload | Session ids are 8 bits and repeat; an ACK from an earlier use of the id counted | Upload ACKs forgotten on a new session (`0ecb14e`) |
| 5 | After the Pico came back, WAITING FOR TILE READINESS forever | Tiles report ASSET_READY only after an upload; the restored session had none | When the Pico answers again after silence, publish once more in a fresh session, and send the mode again (a restarted Pico starts in video mode) (`132e456`, `e68cb66`) |
| 6 | A website restart failed every upload at once | The address check came before the wait for the tile's first TELEMETRY | Wait first (`f6c48c6`); a Pico that never answered is also woken (`f6c48c6`) |
| 7 | With tiles unplugged, every layout change was refused for about 27 s | Uploads to a silent tile retried 20 rounds | A tile silent for 5 s is not uploaded to (`6e3c526`) |

Also changed at the operator's request: **the countdown, score and stars show whole on every tile** instead of one picture cut across the grid (the tiles stand apart; replaces DS-15 decision 3; `bdc2920`, Pico flashed). And `--layout`, `--map`, `--gif` set up the wall when the website starts (`87792ff`).

## Open

- **Pico freezes (three today).** 16:13 (`[CYW43] do_ioctl(2, 134, 4): timeout`, restarted), round 37 (about 100 s, recovered), 18:42 after a round's results (did not recover; power-cycled), and one at 19:09 caught in the counters: **`dispatch_max_us=143928567`**, one datagram's handling took 144 s (`skipped=1438`). Every case came with bursts of traffic. Likely the CYW43 WiFi chip stops answering and each send blocks in the driver. Proposed, not decided: a hardware watchdog (reboot after about 5 s without a loop pass; the router join must not block past it), now that the website and the tiles recover a restarted Pico by themselves.
- **USB power.** An unpowered hub with three tiles kept disconnecting (device numbers past 126), resetting tiles 1, 4, 7 (their unexplained reboots) and once the Pico. The operator suspects the laptop cannot power the Pico and tiles; tiles 1, 4, 7 ran from another laptop at the end. For the demo: tiles on their own power, the Pico on a direct port or a good charger.
- **Opening a tile's serial port can reset it** (boot lines at the start of `repro_9C139EF12EB0.log` and `repro_9C139EF13070.log`), and some freshly flashed tiles print nothing on USB for 15 s. Judge a tile by its TELEMETRY on the website, not its serial log.
- **The preview is the first frame only** (operator asked; animated preview not built).
- clang-format 18.1.8 (CI's pinned version) was not available offline; changed C lines were formatted by hand to `.clang-format`. CI will tell.

## 19 Nov 2026: Pico watchdog (decided with Cheston)

Another freeze before any traffic: the Pico was silent on USB and WiFi while waiting for a session (14:30). Built and flashed the hardware watchdog (`coordinator_loop.c`, normal build in `ledwall_sync/build`):

- The loop feeds an **8 s** watchdog every pass (a pass takes microseconds to milliseconds; the RP2040 maximum is 8.3 s). The router join no longer blocks in `cyw43_arch_wifi_connect_timeout_ms()`: the same loop, with the watchdog fed every 100 ms.
- `CL_STATS` ends with `wd_boot=1` after a boot the watchdog caused, so a freeze leaves a trace.
- The loop feeds the watchdog only while `scratch[4]` is what `watchdog_enable()` left: `picotool reboot` arms the watchdog with a short delay and rewrites it, and feeding then cancelled the reboot (found on the bench: the Pico never restarted).

| Check | Result |
|---|---|
| Test build `cmake -DCL_TEST_HANG_S=30` (`ledwall_sync/build_hang`, gitignored) | **PASS**: plain restart showed `wd_boot=0`; the loop stopped at 30 s (`CL_TEST_HANG_S, the loop stops now`); USB disconnected exactly 5 s later (then 5 s limit); the Pico rejoined with `wd_boot=1` and did not stop again |
| Normal build, `picotool reboot -f` with the website and nine tiles running | **PASS**: NO STATUS 2 s after, Pico back 17 s after (its first join timed out at 10 s and the retry joined, as on every boot today), the website published again and the wall was READY with all nine tiles 23 s after the restart, untouched |
| Gate | C host tests and ASan, barr_check 0 findings; Pico builds with no warnings |

Open: one unexplained restart 14 s after a flash with the 5 s limit (hence 8 s); the first router join after a boot timing out at 10 s on this router.

## 19 Nov 2026: breadcrumbs and the router's 2.4 GHz extras

Breadcrumbs (`5690403`, `CL_STATS wd_step`) showed every caught hang inside the WiFi driver: once inside a UDP send from the outbox routine during a round (`0x80000005`), once in the router join right after a restart (`0x0a`). The Pico still froze several times during rounds on a phone charger, so laptop power was not the cause. The freezes began with the new router (none on the old Matrix router on 12 Nov with the same Pico).

**Router changed (Asus RT-AC87U, 2.4 GHz):** Wireless → General: WPA2-Personal, AES only, PMF disabled if offered, group key rotation 86400 s; Wireless → Professional: **Turbo QAM off**, explicit and universal beamforming off, TX bursting off, airtime fairness off, roaming assistant off, DTIM 1. Channel 11, 20 MHz unchanged.

**Result: three rounds with no freeze** (before: about one per one or two rounds). Early evidence, not proof: keep counting rounds; if a freeze comes back, read `wd_step` with the Pico on the laptop's USB.
