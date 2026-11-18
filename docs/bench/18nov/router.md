# Bench, 18 Nov 2026: new demo router, 9 tiles

Branch `feature/whack-a-mole` @ `52b367f`. Pico `…E66430A64B595138`, nine tiles in a 3x3 wall (map `1,2,3,4,5,6,7,8,9`). The laptop is on SIT-WIFI and reaches the router through a USB Ethernet adapter (192.168.1.226; the adapter's name changes on every replug, so the firewall now allows UDP from 192.168.1.0/24 on any interface).

## New router

The user replaced the Matrix router. Same password, same 192.168.1.x addresses, but the 2.4 GHz SSID is `MATRIX` (it was `Matrix`; SSIDs are case-sensitive). Both `wifi_secrets.h` files and the backup in `~/.config/se15/` now name `MATRIX`; the Pico and all nine tiles were rebuilt and reflashed (builds exit 0, every tile `DE ticks` with `missed=0` after the flash).

| Check | Result | Notes |
|---|---|---|
| Join | PASS | Pico 192.168.1.154 and every tile at its old address; Pico `online=0x1ff sync=0x1ff`. |
| Video V3 (session 35), router on auto channel | **FAIL** | Uploads and playback worked, but every tile ended with `QUEUE_OVERFLOW`; tiles 1, 4, 7 logged about 24,000 missed display ticks in bursts and 79 lost beacons. Beacons arrived in bunches of three about every 300 ms; unicast ping to a tile 3 to 322 ms (mean 46 ms). The router was on channel 6 (15 networks) after channel 10 an hour before, with a phone also joined. |
| Router settings | changed | Fixed channel 11, 20 MHz, DTIM 1, nothing else joined. |
| Video V3b (session 36) | **PASS** | Ping mean 10 to 15 ms (worst 30 to 98 ms); 98.5% of beacons 50 to 150 ms apart (worst 211 ms); 1 missed tick per tile in 100 s; 0 queue drops; `faults=none` on all nine before and after; uploads 57 of 57, resent 0, 2.0 to 2.2 s each. |
| Round 37 (session 37) | **FAIL** | The Pico stopped for about 100 s from 9 s into game mode, during the tap test: no beacons, no log, no ACKs (`skipped=1001`, `rx_dropped=21` afterwards); the tap test off and ARM were not acknowledged, so no start sprite. It then recovered by itself. Earlier the same day the same Pico printed `[CYW43] do_ioctl(2, 134, 4): timeout` and needed a restart (`picotool reboot -f`). |
| Round 38 (session 38), Pico on a direct laptop port, DHCP reservations | **PASS** | No stall (beacon watchdog quiet; `skipped` unchanged). 26 of 27 moles, 3 stars, 78 wrong: the operator tapped freely. Of 116 physical taps, 53 were felt by more than one tile and 24 of those reached the Pico more than 20 ms apart, so they were judged as separate taps. Tap latency means 27 to 37 ms. |
| Round 39 (session 39), one tap per mole | **PASS** | 25 of 26 moles, 0 wrong, 780 ms, 3 stars; 1 of 25 taps felt by two tiles; latency median 29 ms, 95% under 52 ms, worst 72 ms; `faults=none` on all nine. |

## Open

- **The Pico stall of round 37 is not explained.** It has not come back since the Pico moved to a direct laptop port (two rounds). Power the Pico from a laptop port or a good charger at the demo, and keep a beacon watchdog on rounds.
- **Echo grouping by arrival time.** The Pico groups a tap's reports within 20 ms of the first arrival; on this router the reports of one hard tap can arrive up to about 170 ms apart and are then judged separately. Grouping by the tiles' tap time stamps would remove this. Matters only for hard slaps (round 39: one echo in 25 taps).
- **The demo room's channel.** Channel 11 shares the air with about 22 SIT networks at this bench; if the wall misbehaves in the demo room, first try another fixed channel.
