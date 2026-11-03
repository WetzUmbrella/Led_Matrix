# Two-tile skew rig bench log (T-B4-03)

## Dry run (Part B0), 29 Oct 2026, Jia Jun

Not evidence. Single tile + Pico W only (no second tile, no logic analyser yet).

| Item | Value |
|---|---|
| Branch / hash flashed | `b4/phase1` `7a98781` (tile app version `cr1-before-75-g7a98781`), both tile and Pico |
| Host | macOS (not WSL), ESP-IDF v5.3, Pico SDK 2.3.1 |
| WiFi | router SSID changed to `Matrix` (2.4 GHz, channel 6); both `wifi_secrets.h` updated |
| Pico | `/dev/cu.usbmodem114201`; joined Matrix, IP 192.168.1.57; `BEACON seq=` rising by 1, `late_us` about 50; first `LATE_HIST,100,0,...` clean |
| Tile A | ESP-4, MAC 9c:13:9e:f1:2e:b0, `/dev/cu.usbmodem101`, IP 192.168.1.6; `TN_RX_HOOK 1`; `display_engine: test pin GPIO4, first tick 16` |
| Tile B | not connected |
| Time from tile boot to joined | 2.4 s (`joined Matrix` at 2398 ms) |
| `missed` | `DE ticks=600 missed=1` on the first DE line (known first-tick start-up limit); later lines not yet checked |
| `dropped` | `TN_STATS dropped=0` |
| GPIO4 on header | NOT YET CONFIRMED (B1, Jia Jun) |
| Analyser capture / UNPAIRED | not run (no analyser, one tile) |
| C1 (T-B5-04) | PASS 3 of 3, see `docs/week10/tile_tasks_boot.log` |

### Problems found

1. **Two coordinators on one network; the tile mixes their beacons.** Tile A received about 17 beacons/s instead of 10. Grouping `BCN,<seq>,<t_p>,<t_l>` lines by `t_p - seq x 100 ms` gives two separate Pico clocks, 171 beacons each in 20 s (examples: `BCN,1536,162981900,...` from this bench's Pico and `BCN,529,58970285,...` from a second Pico that booted later). Both send session_id 0 to UDP 4210. Cause in code: `tile_net.c:281` `(void)p_addr; /* The sender is not checked yet (T-B2-04). */`, so `timesync` takes (t_p, t_l) pairs from both clocks, the offset estimate is meaningless, and `TN_STATS lost=` reads in the millions (seq jumps between the two streams). Effect on the rig: any skew run while a second Pico is beaconing on the same SSID is invalid. Bench rule until T-B2-04 lands: only one Pico powered on the router. Defect for Benjamin (T-B2-04 sender/session check) and Cheston (T-B3-03, filter beacons by coordinator address or session_id), noted to Ahsanul (T-B4-02).

2. **Tile A always reboots into download mode on a software reset.** Every reset from the Mac (esptool `--after hard_reset`, `idf.py monitor`, RTS/DTR pulse) gave `rst:0x15 (USB_UART_CHIP_RESET),boot:0x0 (DOWNLOAD(USB/UART0))` / `waiting for download`. Flashing works; the app only runs after unplugging and replugging USB (then `boot:0x8 (SPI_FAST_FLASH_BOOT)`). Steps that say "press RESET" (B0.8, B0.9, the Pico-first start order) must use a USB replug on this tile. Not yet checked whether the onboard RESET button or a Windows/WSL host behaves the same.

3. **Checklist steps that don't match a macOS host.** B0.1 (`usbipd`) and the `\\wsl$\...\ahsanul` path in B0.4 don't apply; ports are `/dev/cu.usbmodem*`. The Pico was put into BOOTSEL without the button by opening its port at 1200 baud.

Note added on the handoff build (`b4/handover` + patches 01 to 09): the sender is still not checked. `tile_net.c:281` moved into `tn_handle_record()`, which now re-learns the Pico address whenever the beacon sender changes, so with two Picos the tile would also re-send REGISTER on every flip. Raw logs and the two-clock grouping are in the handoff folder `bench_logs_29oct/` (kept out of the repo, as Part B0 says for dry-run logs).
