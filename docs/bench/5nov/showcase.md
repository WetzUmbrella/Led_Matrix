# Showcase runbook: two-tile video wall (5 Nov 2026)

Branch `b3/showcase-5nov`, made from `master` at `8af7b15`. The showcase runs the real pipeline (dashboard, asset upload, Pico readiness, START_AT, synchronised playback). The branch adds a showcase GIF generator (`pc/tools/make_showcase_gif.py`, with pytest), this file, and one firmware fix found on the day: the 90 and 270 degree rotation remap (`panel_sync/main/display_frame.c`, section 5).

Ran on 5 Nov at Cheston's bench: Pico plus two tiles (nodes 3 and 5) on the top row, then a 2x2 wall of nodes 3, 5, 4 and 2, all built on this laptop from `master` plus this branch (ESP-IDF v5.3, Pico SDK 2.3.1, 0 warnings).

**Use a new session number for every run** (`--session 2`, 3, ...). A tile that already holds the clip for the same session and CRC answers only the Pico, so the uploader waits about 70 s and the dashboard stops (section 5).

For the 2x2 wall: `python pc/tools/make_showcase_gif.py --rows 16 --out showcase_2x2.gif`, then `config showcase_2x2.gif 2x2 3,5,0,4,2,0,0,0,0 continue 8`; readiness for nodes 2 to 5 is `0x01e`.

## 1. Set-up (10 min)

| Item | Setting |
|---|---|
| Network | Net A ("Matrix"), 2.4 GHz, AP isolation off |
| Pico | Pico A, `pico_plain_A`; power it first. Its log prints `COORDINATOR up: ip=<Pico IP>` |
| Tiles | Tile 1 (node 1) on the **left**, tile 2 (node 2) on the **right**, both `tile_plain_A`, upright, side by side. Each tile log must show `BCN,` lines and the Pico log `JOIN node=1` and `JOIN node=2` |
| PC | On Net A; WiFi power save off; firewall allows UDP 4210 to 4215 inbound; nothing else bound to UDP 4215 (close `telemetry_ack.py` if it is running) |

Make the GIF, from the repo root:

```
python pc/tools/make_showcase_gif.py --out showcase.gif
```

8 s loop: a white bar sweeps across both tiles and back (4 s), then rainbow bands scroll (4 s). Both cross the seam between the tiles, so a skew or a wrong partition shows at once.

## 2. Run it (5 min)

```
python pc/dashboard.py --pico <Pico IP> --session 1
(dashboard) config showcase.gif 2x2 1,2,0,0,0,0,0,0,0 continue 8
(dashboard) build
(dashboard) status          # repeat until both "tile 1" and "tile 2" rows appear
(dashboard) upload          # expect "node 1: 45 of 45 chunks ACKed" and the same for node 2
(dashboard) status          # repeat until readiness=0x003 and "start enabled"
(dashboard) start
```

Map `1,2,0,0,0,0,0,0,0` puts nodes 1 and 2 on the top row of the 2x2 layout; the generator only draws that row, so nothing is lost. `continue` is the failure policy: if a tile is unplugged, the other keeps playing (FR18).

Then show: `pause` (both tiles hold the same frame), `resume` (they continue together from the held frame, RESUME_AT), and unplug tile 2 for a few seconds (tile 1 keeps playing; `status` names node 2 as missing; after it reconnects it rejoins at a frame boundary).

## 3. If a step fails

| Symptom | Check |
|---|---|
| `status` shows no tile rows | PC not on Net A, firewall, or another program holds UDP 4215 |
| `node n: no TELEMETRY yet` on `upload` | Run `status` again first; telemetry arrives once a second |
| Upload stops short of all chunks | Run `upload` again (the tile re-announces a complete asset; known single-datagram gap R1-8) |
| `readiness` stays below 0x003 after a good upload | ASSET_READY lost (R1-8): run `upload` again. If a tile shows offline after a Pico reset, replug that tile (restart-detection gap in `tile_net.c` `tn_pico_restarted()`) |
| `start disabled` with readiness 0x003 | The tiles are not SYNC_READY yet: wait about 2 s after the beacons settle |
| Image on one tile rotated or mirrored | The tile is not upright, or the LED-numbering assumption is wrong for that board (open item from review round 0). Rotate the board and wait 0.5 s for the orientation filter |
| LEDs very dim | Expected: the brightness cap is 8/255 (Week 6 power feedback); 4 bits per channel rest on it |

## 4. Talking points for the lecturer

### What the wall shows
One GIF is decoded and partitioned on the PC, uploaded once to each tile, and played by each tile from its own RAM. Each tile picks its frame from the Pico's corrected clock, so the two halves change frame together, with no network traffic per frame (FR22).

### Each subsystem today

| Buddy | Subsystem | Shown or measured today |
|---|---|---|
| 1 Jolyn | PC dashboard and media | GIF to 60 fps, 4-bit packing, per-tile CRC, upload with retry; F1 asset check 14 of 14 (`docs/week13/check_assets.log`); the dashboard drives this demo |
| 2 Benjamin | Pico W coordinator | Poll-mode loop, beacon every 100 ms on an absolute schedule. D1: 7,901 beacons, all under 1 ms late; D2: `exc_max=0` (no callback in interrupt context, settles OD-01); D7: one JOIN per tile (`docs/week10/pico_lateness_10min.log`) |
| 3 Cheston | Tile network and storage | Beacon receive on both hooks: 7,400 and 7,300 received, 0 dropped by the tile; the 2.8% loss is the router dropping broadcasts, the same seq on both tiles (D4). Upload, flash, CRC check and RAM copy run live in this demo |
| 4 Ahsanul | Sync and playback | Both tiles SYNC_READY (`sync=0x003`); START_AT, PAUSE and RESUME_AT shown live; found and fixed a task-starvation bug on the day (`6be4096`) |
| 5 Jia Jun | Display and orientation | 60 Hz tick from corrected Pico time on core 1, RMT output, 4-bit unpack after the send; orientation from the IMU |

### How Tribunal 1 (minimum-bit data) was carried out

| Decision at Tribunal 1 | Where it is now |
|---|---|
| 4 bits per colour channel (115,200 B to 57,600 B per 10 s tile asset) | `pc/asset_builder.py`, unpacked in `panel_sync/main/display_frame.c`; this demo's 8 s asset is 46,080 B per tile |
| Binary wire format, explicit shifts and masks, one shared table | `protocol/layout_table.md` (18 messages), `common/proto` (C) and `pc/proto_layout.py` (Python), 57 shared test vectors, 0 mismatches |
| Fault word: atomic OR to set, clear only acknowledged event bits | `panel_sync/main/fault_word.c`, `telemetry_book.c`; TELEMETRY carries the word, the dashboard ACKs it |
| Pico readiness bitmap without a race | Poll mode, confirmed on hardware today (D2 `exc_max=0`) |
| 64-bit clock offset read without tearing | Sequence counter in `panel_sync/main/timesync.c`, covering offset, drift and reference |
| Field widths from the valid range (frame index 10 bits, slew 8 bits signed, time 64 bits with the reason) | `docs/week13/data_representation.md`; the PC refuses GIFs over 1,023 frames |
| Evidence for Week 13 | `docs/week13/before_after.md` and `message_sizes.md` (before sizes from tag `cr1-before`; RAM and on-air cells still to measure) |

### What is not done (say it plainly)
- Inter-tile skew is not measured yet: no logic analyser today. The two-tile capture (target 5 ms) is the next session's first test; `pico_tap` on a spare Pico is the backup tool.
- Not yet run on hardware: upload fault cases, 3-tile and 5-tile runs, the 30 min soak, orientation trials, display timing under load.
- Only five tiles exist, so the 3x3 target runs as five tiles with four blank slots.
- Known open items: R1-4, R1-7, R1-8, and the Pico-restart detection in `tile_net.c`.

### Next steps
1. Borrow or buy a logic analyser; run the two-tile skew test (B), then three tiles (G6).
2. Run the asset and fault tests (F2 to F5, G1, G7, E1 to E4) and the stress builds (G2, G5).
3. Fix the open robustness items, then a second review round.
4. Five-tile soak and router run; fill the Week 13 before/after evidence; Tribunal 2 on 23 Nov (to confirm).

## 5. Found on the bench (5 Nov 2026)

| # | Finding | Evidence | State |
|---|---|---|---|
| 1 | **Re-upload of a clip the tile already holds hangs the PC.** `as_on_manifest()` answers a MANIFEST that matches the published asset with ASSET_READY to the Pico only, then ignores every chunk without a CHUNK_ACK; the uploader retries about 70 s and the dashboard stops at that node, so later nodes are never uploaded. Hit after tiles rebooted and reloaded their session-1 clip from flash. | Tile 2: `ignored=932 erases=0`; a forced upload of a different clip: 45 of 45 ACKed in 4.3 s, no resend | Workaround: new session per run. Fix to do: the tile also answers the PC on that path and the uploader takes it as done (close to R1-8) |
| 2 | **90 and 270 degree remaps were swapped.** A tile turned 90 degrees in its slot showed its piece upside down; 180 was right. | Bench observation; `ORIENT changed` codes 0, 1, 2 for clockwise turns | Fixed in `display_frame.c`, test pins LED 0 to source 7 at 90 and 56 at 270; flashed on all four tiles |
| 3 | Turning the **whole wall** keeps each piece upright but leaves the pieces in the wrong slots. Slot position comes from the Node ID map (FR3, NFR11); position detection is out of scope (W6 1.4). | All four tiles changed orientation together | By design; whole-wall rotation would need the PC to rotate the map and re-upload (future work) |
| 4 | Tile `lost=` beacon counter jumps by about 62,000 after a Pico restart: the beacon seq restarts at 0 and the wrap-aware gap counts it as loss. | `TN_STATS lost=61905` | Cosmetic; same root as the `tn_pico_restarted()` issue |
| 5 | Tile 5 (`9c:13:9e:f1:2f:70`) had no board number; set to 5. Tile `b8:f8:62:d8:05:ec` is board 3. | NVS read and write with esptool | Done |
| 6 | Rejoin works on hardware: all four tiles reflashed one at a time during playback came back to `online=0x01e ready=0x01e sync=0x01e`, state COORDINATING_PLAYBACK, from their cached clips. | Pico `CL_STATS` | First hardware evidence for FR19; timing not yet measured (H3) |
