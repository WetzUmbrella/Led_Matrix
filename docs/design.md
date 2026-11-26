# Design

How the video wall works, and why it is built that way. The requirements it
meets are in [`requirements.md`](requirements.md); every message field is in
[`../protocol/layout_table.md`](../protocol/layout_table.md). Section 11
explains the reference codes (`DS-02`, `T-B3-04`, `VC-4` and so on) found in
source comments.

## 1. The parts

```
 PC (Python)                       Pico W (Pico SDK, no RTOS)              Tiles 1..9 (ESP32-S3, FreeRTOS)
 website, GIF -> assets,   ----->  master clock, sessions, readiness,  --> time sync, video in RAM,
 upload, dashboard                 START_AT / PAUSE / RESUME_AT,           60 Hz display, orientation,
                                   game rules and scoring                  tap detection, game drawing
```

| Part | Code | Owns |
|---|---|---|
| PC | `pc/` (`web.py`, `asset_builder.py`, `uploader.py`, `dashboard.py`) | the operator's website; turning a GIF into one asset per tile; uploading; showing tile health and faults; ACKing TELEMETRY |
| Pico W coordinator | `ledwall_sync/` | the clock every tile follows (BEACON); which tiles are in the session and ready; when playback starts, pauses and resumes; the whole game (moles, taps, score) |
| Tile | `panel_sync/` (same firmware on every tile) | keeping its copy of the Pico clock; storing its video section; drawing frame k on tick k; detecting taps and turns; drawing game sprites and wall pictures |
| Shared codec | `common/proto/` (C), `pc/proto_layout.py` (Python) | packing and unpacking every message from one layout table |

The central idea: **no network traffic is on the per-frame path.** Each tile
holds its own video section in RAM and works out which frame to show from
its corrected copy of the Pico's clock. A lost beacon or a late task cannot
leave a tile permanently behind; it only changes the clock estimate a little.

## 2. Network and protocol

All traffic is UDP on a dedicated 2.4 GHz router (WPA2-AES, fixed channel,
20 MHz, DTIM 1, AP isolation off).

| Port | Listener | Messages |
|---|---|---|
| 4210 | tiles | BEACON (broadcast), START_AT, RESUME_AT, PAUSE, STOP; game messages MOLE_ON, FEEDBACK, GAME_CONTROL, WALL_PICTURE, ACK (unicast to each tile) |
| 4211 | Pico | REGISTER, HEARTBEAT, ASSET_READY, SYNC_READY, NACK, TAP, ACK from tiles |
| 4212 | Pico | SESSION_CONFIG, commands and GAME_CONTROL from the PC |
| 4213 | PC | STATUS (every 250 ms) and ACKs from the Pico |
| 4214 | tiles | MANIFEST, ASSET_CHUNK, TELEMETRY_ACK from the PC |
| 4215 | PC and Pico | TELEMETRY broadcast by every tile once a second; upload replies |

**Binary, minimum bits, one table (Tribunal 1 change requirement).** There
are 24 message types. Every field uses the fewest bits its valid range needs
and is written with explicit shifts and masks, little-endian, never as a C
struct or bit-field (their layout is compiler-defined, and Python has none).
Every message starts with a 29-bit header: type (5 bits), session (8),
sequence number (16). `protocol/layout_table.md` is the single source; C and
Python encode every shared test vector byte for byte the same, which CI
checks.

**Reliable where it matters.** Important messages carry a sequence number,
are ACKed, and are resent every 50 ms until the ACK arrives or their
lifetime ends (a TAP 1 s, FEEDBACK 300 ms, GAME_CONTROL 1 s, a MOLE_ON until
it is due). A receiver ACKs every copy, including repeats and old ones (a
repeat means its ACK was lost), but acts on a message only when its number
is newer than the last one acted on. Timed commands (START_AT, RESUME_AT)
are instead sent 3 times, 100 ms apart, with one number.

**One coordinator per tile.** A tile locks onto the first Pico it hears and
ignores beacons and commands from any other address; it moves only after 2 s
of silence (twice the 1 s holdover), so it follows its own Pico through a
restart but never mixes in another team's clock (bench, 29 Oct).

## 3. The video path

1. **GIF to assets (PC, `asset_builder.py`).** The GIF is resampled to 60 fps
   by cumulative time (at most 1,023 frames, the 10-bit frame index),
   centre-cropped to the grid (columns x 8 by rows x 8 pixels) and cut into
   one 8x8 section per tile (DS-14: each tile stores only its own section).
   Every channel is scaled once, on the PC, with round(v x 8 / 255) and
   packed at 4 bits per channel: 96 B per frame, 57,600 B for 600 frames.
2. **Upload (PC to each tile, all tiles at once).** A MANIFEST (session,
   frame count, format version, CRC32), then 1 KB chunks in windows of 4.
   The tile ACKs every chunk by index; only missing chunks are resent.
3. **Verify and copy to RAM (tile, `asset_rx.c`, `asset_store.c`).** An
   unknown format version or an impossible length gets a NACK and the
   ASSET_FORMAT fault, and never ASSET_READY. When all chunks are in, the
   tile checks the CRC32, copies the whole asset into internal RAM (it must
   fit with a 32 KB margin: 90,368 B), and only then sends ASSET_READY. The
   display never reads flash, because flash access stalls code that is not
   in IRAM (Tribunal 1 feedback).
4. **Readiness (Pico).** Only the Pico decides. Start is allowed when every
   placed tile has sent ASSET_READY with the active session and the expected
   CRC32, and SYNC_READY.
5. **Start.** The Pico sends START_AT naming a Pico time 600 ms ahead,
   3 times. Each tile shows frame k on the k-th 60 Hz tick after it, wrapping
   at the frame count. PAUSE freezes on a frame; RESUME_AT names the frame
   and the time to continue from.

A tile that already holds the right asset (same session and CRC32, reported
in REGISTER) is counted ready without a new upload.

## 4. Time sync (tile, `timesync.c`)

- **Input.** The Pico broadcasts its 64-bit microsecond clock every 100 ms on
  a fixed schedule. The tile stamps its local time as soon as the beacon
  arrives (in the lwIP receive callback) and queues the pair.
- **Offset.** From the last 16 beacons, the one with the smallest delay
  (the least time between the Pico's stamp and the tile's) gives the offset;
  delay only ever adds lateness, so the fastest beacon is the truest.
- **Drift.** From the minima of the two halves of the window, limited to
  +-50 ppm, the most a crystal can drift. Without the limit, router delay
  read as clock rate and put tiles up to 10 ms apart (ST-01, ST-07).
- **Slew, never step.** The published clock moves at most 100 us per 60 Hz
  tick toward the estimate, so the video never jumps. A new estimate is
  seeded only when the Pico restarts (or, as a fallback, after 3 beacons more
  than 5 s off); late beacons from a WiFi stall never move it (ST-07).
- **Safe reads.** Offset, drift and reference are published together under
  one sequence counter (a seqlock): the writer makes it odd, writes, makes it
  even; a reader retries if it changed. No reader ever sees half an update,
  and the display tick never waits on a lock.
- **Holdover.** After 200 ms without beacons a playing tile runs on its own
  clock; after 1 s it pauses and raises BEACON_LOSS.

## 5. Display (tile, `display_engine.c`)

- **Tick n is re-armed from its own deadline,** origin + n x 1,000,000 / 60 us
  in corrected Pico time, as a one-shot timer, so ticks never accumulate
  error and every tile's tick n lands at the same moment (Tribunal 1
  feedback: free-running 60 Hz timers can be a frame apart).
- **Transmit first, then prepare.** On the tick the already-prepared buffer
  is sent to the LEDs (RMT with DMA, one refill per frame), then the next
  frame is unpacked from the 4-bit asset. Preparation time never adds to
  presentation time.
- **No scaling on the tile.** The unpacked nibble (0 to 8) is the LED value,
  so every channel is at most 8/255 and the brightness cap cannot be applied
  twice.
- **Orientation.** Each LED takes its source pixel through the tile's
  orientation, so the picture is upright at 0, 90, 180 or 270 degrees.

## 6. Orientation and tap detection (tile, `orientation.c`, `tap_detect.c`)

The Orientation Task reads the QMI8658 accelerometer every 1 ms at +-16 g.

- **Orientation** is gravity on an upright tile (no gyro integration
  needed). It changes only after 26 equal readings in a row at 50 Hz
  (520 ms), so a tap or a bump never turns the picture.
- **Taps are detected in software (DS-02),** because the IMU's own tap engine
  caught only about 70% of taps at 100 Hz on the bench. A tap is a jump of
  more than the threshold (300 mg by default, set from the website from 50 to
  5,000 mg without reflashing) above a slowly tracked baseline of gravity.
  The peak over the next 10 ms is reported with the tap time. After a tap the
  tile ignores itself for 250 ms, so a slap that rings counts once.

## 7. The game (Pico `game_rules.c`, tile `game_tile.c`)

**Round states (DS-08).** GAME IDLE, ARMED (holes on every tile and a start
sprite on the tile nearest the centre), COUNTDOWN (3-2-1, 1 s per digit),
ROUND (30 s), RESULTS (sparkle, score and stars on a loop until the next
Arm). Tapping the start sprite, or Start now on the website, starts the
countdown.

**Moles.** One at a time on a random active tile, never the same tile twice
in a row. The up time shrinks linearly over the round (Easy 2,000 to
1,200 ms, Normal 1,500 to 800 ms, Hard 1,200 to 500 ms), with 300 ms between
moles. MOLE_ON is sent 200 ms before the mole shows, with its show and hide
times on the Pico clock, so every tile draws it on time.

**One tap, one verdict (DS-03).** A hard tap shakes the neighbouring tiles,
which report echo TAPs of their own. The tile stamps the tap on the Pico
clock and sends TAP with its peak at once. The Pico treats TAPs stamped
within 30 ms of each other as one physical tap, waits 150 ms after the first
arrives for the rest (WiFi delivery is uneven), and the tile with the
**highest peak** is the one tapped: on the bench the tapped tile had the
highest peak in 40 of 40 taps on sponge mounts. A hit scores +1; a tap
elsewhere counts one wrong action; the score never goes down. A tap made
before the mole hid still counts if it arrives up to 100 ms late.

**Feedback and pictures.** The Pico sends FEEDBACK (hit or miss) to the
tapped tile, which shows a green burst or a red X at once. The countdown,
score and stars are WALL_PICTUREs with a Pico time 0.5 s ahead, so every
tile switches on the same frame. Hit, wrong and miss differ in shape as well
as colour.

**On the tile,** every game message for this tile is ACKed and applied once.
Separate sequence baselines for MOLE_ON/FEEDBACK, GAME_CONTROL and pictures
let a lost ARM that is resent after the start sprite still arm the round.
The Network Task publishes the game state to the Display Engine through a
seqlock.

## 8. Tile tasks and shared data

| Task | Core | Priority | Job |
|---|---|---|---|
| Display Engine | 1 | 20 | the 60 Hz tick: send, then prepare |
| Orientation | 1 | 5 | IMU every 1 ms: orientation and taps |
| Time Sync | 0 | 16 | the clock filter and slew |
| Playback Control | 0 | 15 | the playback states and timeline |
| Network (`tile_net`) | 0 | 12 | every socket; REGISTER, heartbeats, TELEMETRY, game messages |
| Asset Storage | 0 | 8 | receiving, verifying and copying the asset |

WiFi and lwIP stay on core 0 (WiFi at 23, tcpip pinned to core 0), so the
display tick on core 1 is never delayed by network work. Only the Network
Task owns sockets; other parts talk to it through queues. Shared data has
one writer and a stated protection: the fault word is one atomic 32-bit word
(fetch-or to set, fetch-and to clear); time sync, the playback timeline and
the game state are seqlocks; the asset is published once and constant while
it can be read; a tile writes nothing to flash while armed or playing.

## 9. The Pico coordinator (`coordinator_loop.c`)

- **Poll mode.** The Pico runs one loop with no RTOS; lwIP callbacks run only
  inside `cyw43_arch_poll()`, so no callback can interrupt the loop
  mid-update (the RP2040 has no atomic read-modify-write).
- **One bounded routine per pass,** each within 1 ms, so a beacon goes out no
  more than 5 ms after its slot. Beacons, the heartbeat scan (100 ms) and
  STATUS (250 ms) run on absolute schedules; a stall skips slots instead of
  bursting.
- **Registry.** 9 Node IDs; a tile is offline after 2.5 s without a message
  (2.6 s at worst with the 100 ms scan).
- **Failure policy** (chosen per session). Continue: a lost tile is skipped
  and the others keep playing; it rejoins with a RESUME_AT. Pause: the wall
  pauses and waits up to 5 s (this path has an open bug, ST-12, so the demo
  uses continue).
- **Watchdog.** An 8 s hardware watchdog restarts a frozen Pico; the website
  republishes the wall and the tiles drop the old Pico's state.

## 10. Faults and recovery

- **Fault word (tile).** 9 bits: WIFI, CRC_FAIL, QUEUE_OVERFLOW, BEACON_LOSS,
  IMU, MISSED_DEADLINE, ASSET_FORMAT, DISPLAY, SYNC_STALE. TELEMETRY carries
  the whole word every second. **Event** bits (one-off problems) are cleared
  only when the PC ACKs the TELEMETRY that carried them, so a lost report is
  simply repeated a second later. **Condition** bits (ongoing problems) are
  cleared only by the part that owns them, when the problem ends.
- **Pico restart.** A tile notices a new Pico (a beacon from a new address,
  or the session falling to 0, or the sequence number jumping back) and
  stops playback, restarts time sync, clears the game and resets every
  sequence baseline, so the new Pico's messages are not dropped as old.
- **Tile restart.** The tile registers again, reporting the asset it still
  holds; the Pico counts it ready if it matches, otherwise the PC uploads it
  again.

## 11. Reference codes in the source

Comments cite where a rule came from. The families are:

| Code | Meaning |
|---|---|
| `FR1` to `FR21`, `NFR1` to `NFR16` | the final requirements ([`requirements.md`](requirements.md)) |
| `A1` to `G9`, `NR-01` to `NR-14`, `D-01` to `D-17` | the game requirements and decisions frozen on 9 Nov 2026, merged into the FRs and NFRs (each FR names them) |
| `DS-01` to `DS-17` | game design decisions of 9 to 11 Nov 2026, described in sections 3 to 7 here (DS-02 tap detection, DS-03 highest peak, DS-08 round states, DS-14 per-tile video sections, DS-15 sparse grid, DS-17 demo network) |
| `F-01` to `F-10` | the feasibility checks on the bench, 9 Nov 2026 |
| `W6`, `W6D-` | the Week 6 architecture design |
| `VA-`, `VB-`, `VC-`, `PostTri` | rows of the Tribunal 1 document: Version A, B and C ([`tribunal1/`](tribunal1/)) |
| `CR1` | the Tribunal 1 change requirement (minimum-bit data) |
| `T-B1-01` to `T-B5-08`, `T-X-01` to `T-X-08` | tasks of the October implementation plan, by subsystem (B1 PC, B2 Pico, B3 tile comms and storage, B4 time sync and playback, B5 display and orientation; X shared) |
| `OD-01` to `OD-22` | open decisions of that plan, each settled as recorded in the code |
| `R1-1` to `R1-11` | findings of the first code review, each fixed |
| `ST-01` to `ST-14` | system tests on the wall ([`testing/evidence/`](testing/evidence/)) |
| `PROVISIONAL` | a value chosen in the plan and kept, not measured as optimal |
