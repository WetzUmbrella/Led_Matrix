# Requirements (final)

The functional (FR) and non-functional (NFR) requirements of the final
system, as submitted for Tribunal 2 (23 Nov 2026). The tag in brackets after
each row names the rows it came from: `A1` to `G9` and `NR-01` to `NR-14` are
the game requirements in [`plan/game_plan_v2.md`](plan/game_plan_v2.md),
`W6` the Week 6 video-wall design, and `CR1` the Tribunal 1 change
requirement (minimum-bit data, evidence in [`week13/`](week13/)).

How each requirement is tested, and the result:
[`testing/TRACEABILITY.md`](testing/TRACEABILITY.md). To run the tests of one
requirement: `python3 tools/traceability.py FR14`.

Two numbers changed after the submission, on purpose, and the build and the
tests use the new ones:

- **FR14** judges a tap group 150 ms after the first TAP arrives
  (`GAME_COLLECT_MS`), not 20 ms. At 20 ms, 25 of 84 taps on the wall were
  judged on a neighbour's echo; at 150 ms, 5 of 84 (bench 24 Nov).
- **NFR3** is under 300 ms, not under 100 ms. The longer collect wait trades
  latency for accuracy (`docs/bench/25nov/nr03.md`).

## Functional requirements

| ID | Requirement |
|---|---|
| FR1 | The website shall let the operator set a grid of 1 to 9 rows and 1 to 9 columns and place up to 9 tiles in any of its cells. Empty cells stay blank. Any other size is refused. [M; A1, DS-15] |
| FR2 | The website shall place tiles by tap-to-place: it highlights a cell, and the next tile tapped goes into that cell. Placing a tile that is already on the grid moves it instead of duplicating it. [M; A2] |
| FR3 | The operator shall be able to switch a placed tile off and on again, or remove it. An off tile goes blank, keeps its cell and takes no part in video or game. A removed tile frees its cell. [M; A3] |
| FR4 | The system shall switch between video mode and game mode only while stopped. A switch requested during playback or a round is refused. [M; A4] |
| FR5 | The PC shall turn the chosen GIF into one asset per placed tile. It resamples the GIF to 60 fps (at most 1,023 frames), centre-crops it to the grid (cols x 8 by rows x 8 pixels), cuts one 8x8 section per tile, scales every channel with round(v x 8 / 255) and packs two channels per byte. Each tile receives only its own section, with a MANIFEST giving the session, frame count, format version and CRC32. All tiles are uploaded at the same time. [M; A13, DS-14, CR1] |
| FR6 | Only the Pico shall decide readiness. Playback starts only when every placed tile has sent ASSET_READY with the active session and the expected CRC32, and has sent SYNC_READY. [M; PostTri, FR8 (W6)] |
| FR7 | To start or resume playback the Pico shall send START_AT or RESUME_AT with a Pico time 600 ms ahead, 3 times at 100 ms intervals. Each tile shows frame k on the k-th 60 Hz tick after that time, wrapping at the frame count, with ticks taken from its corrected copy of the Pico clock. The website offers play, pause, resume and stop. [M; W6 SC4, A13] |
| FR8 | The Pico shall broadcast a BEACON carrying its 64-bit microsecond time every 100 ms on a fixed schedule. Each tile estimates its clock offset and drift from the least-delayed of its last 16 beacons. A beacon more than 0.5 s off the estimate is dropped; only 3 such beacons in a row restart the estimate. After 200 ms without beacons a playing tile runs on its own clock (holdover); after 1 s it pauses and raises BEACON_LOSS. [M; W6 SC4, DS time sync] |
| FR9 | Each tile shall REGISTER with its board number (1 to 9, kept in NVS) and send a HEARTBEAT every 0.5 s, offset by its Node ID. The Pico marks a tile offline after 2.5 s without a message and takes it back when it rejoins. A tile follows only the first coordinator it locks onto. [M; PostTri, A8] |
| FR10 | A game round shall be armable with no video uploaded: a layout and every placed tile online and clock-synced are enough. [M; A14] |
| FR11 | A round shall go through GAME IDLE, ARMED, COUNTDOWN, ROUND and RESULTS. When armed, a start sprite shows on the active tile nearest the centre; a tap on it at least 400 ms after it appears, or Start now on the website, starts a 3-2-1 countdown of 1 s per digit. Arming lapses after 60 s with no start. The round lasts 30 s. Results (sparkle, score, stars) loop until the next Arm. Abort clears the wall within 500 ms. [M; B1, B2, B5, B7, A6, DS-08] |
| FR12 | The Pico shall show one mole at a time on a random active tile, never on the same tile twice in a row unless only one tile is active. The time a mole stays up shrinks linearly over the round: Easy 2,000 to 1,200 ms, Normal 1,500 to 800 ms, Hard 1,200 to 500 ms, with 300 ms between moles. [M; A5, B3, B4] |
| FR13 | Each tile shall detect taps from its QMI8658 accelerometer, read every 1 ms. A tap is a jump of more than the threshold above a slowly tracked baseline on any axis. The threshold defaults to 300 mg and the website can set it from 50 to 5,000 mg without reflashing. After a tap the detector ignores the tile for 250 ms. The tile sends TAP (peak and tap time on the Pico clock) at once and repeats it every 50 ms until the Pico ACKs it, for at most 1 s. [M; DS-02, DS-07, A12] |
| FR14 | The Pico shall treat TAPs stamped within 30 ms of each other as one physical tap, and judge the group no sooner than 20 ms after the first arrives (150 ms in the build, see above). The tile with the highest peak is the one tapped. If it has the mole, the score goes up by 1 and the tile gets FEEDBACK hit; otherwise a wrong action is counted and it gets FEEDBACK miss. A tap made before the mole hid still counts if it arrives up to 100 ms late. The score never goes down; missed moles and wrong actions are counted separately. [M; C1 to C5, DS-03] |
| FR15 | After a round the website shall show hits, missed moles, wrong actions, accuracy, average reaction time, stars (3 at 80% or more, 2 at 50% or more, otherwise 1) and the best score per level for the session. [S; A9, A10, C6] |
| FR16 | Each tile shall draw its own game visuals with channel values 0 to 8: a mole that rises row by row over 160 ms, a green burst on a hit, a red X on a wrong tap, the mole sinking on a miss, and a faint hole on empty tiles. Hit, wrong and miss differ in shape as well as colour. Every tile shows the countdown, score and stars in full, switching at Pico times that are sent 0.5 s ahead. [M; D1 to D7, A15, DS-09] |
| FR17 | Each tile shall draw its picture upright at 0, 90, 180 or 270 degrees, using gravity from the accelerometer on an upright tile. The orientation changes only after 26 matching readings in a row at 50 Hz (520 ms). [M; E2] |
| FR18 | The website shall show the round state, score, missed moles, wrong actions and time left within 500 ms of the Pico; each tile's status (online, orientation, cell), showing a tile offline within 3 s; a tap test that shows the peak of every tap; a log file of taps; and a /score page for a big screen. [M; A7, A8, A11, A16] |
| FR19 | Each tile shall keep one 32-bit fault word with 9 defined bits (WIFI, CRC_FAIL, QUEUE_OVERFLOW, BEACON_LOSS, IMU, MISSED_DEADLINE, ASSET_FORMAT, DISPLAY, SYNC_STALE) and report it in TELEMETRY. One-off event bits are cleared only after the PC acknowledges the TELEMETRY that carried them. Condition bits stay set until the task that owns them clears them. [M; CR1 VC-4, OD-12] |
| FR20 | If a tile drops out mid-round, the Pico shall skip it and the round continues; the tile rejoins afterwards. A tile that receives an asset with an unknown format version or an impossible length sends a NACK, raises ASSET_FORMAT and never reports ASSET_READY. [M; E9, VC-5] |
| FR21 | The wall shall recover from a frozen or restarted Pico with no operator action. An 8 s hardware watchdog restarts the Pico. When STATUS has been missing for 2 s the website disables its buttons, resends the layout until the Pico answers, publishes the wall again in a new session and restores the mode. Tiles drop the old Pico's game state. [M; 19 Nov watchdog, G7] |

## Non-functional requirements

| ID | Requirement |
|---|---|
| NFR1 | Over a 10 min run, the video skew between any two tiles shall stay at or below 5 ms (target) and always below one frame, 16.67 ms (hard limit). [NR-07, W6 NFR2] |
| NFR2 | Each tile shall run at 60 fps, starting the LED transmission of frame n on tick n (a one-shot timer set to origin + n x 1,000,000 / 60 us). Missed ticks are counted, and steady play shall have none. [W6 NFR1, SC5] |
| NFR3 | The hit burst shall appear on the tapped tile less than 100 ms after the tap (300 ms in the build, see above). [NR-03] |
| NFR4 | Out of 50 deliberate taps (fingertip and open palm), at least 95% shall be detected on every tile. A round shall have at most 1 false tap on a neighbouring tile, and each of 50 deliberate hits shall score exactly once. [NR-01, NR-02, NR-05] |
| NFR5 | No LED channel shall exceed 8/255 (about 3%, about 0.12 A per tile at full white). Video assets stay packed at 4 bits per channel: 96 B per frame, so a 600-frame tile asset is 57,600 B in 57 chunks of 1,024 B. [NR-09, CR1] |
| NFR6 | A video for the whole wall shall reach every tile within 60 s. [NR-13] |
| NFR7 | With the tiles already placed, it shall take at most 3 minutes from power-on to the first armed round. [NR-12] |
| NFR8 | 10 rounds in a row shall run with no tile restarting. Unplugging a tile mid-round shall not stop the round. A frozen Pico shall be back to READY within 30 s without anyone touching it. [NR-06, FR21] |
| NFR9 | The Pico loop shall run at most one routine per pass, each finishing within 1 ms, so a BEACON goes out no more than 5 ms after its scheduled time. STATUS goes to the PC every 250 ms. [OD-08, W6 NFR16] |
| NFR10 | The Pico shall notice a lost tile within 3 s: a 2.5 s timeout checked every 100 ms gives 2.6 s at worst. [W6 NFR7, A8] |
| NFR11 | The wall shall run on its own 2.4 GHz router (WPA2-AES, fixed channel, 20 MHz, DTIM 1, AP isolation off) without internet. Game and control messages go to one device at a time with a sequence number and an ACK, never as broadcasts. [W6 NFR10, DS-17] |
| NFR12 | Every WiFi message shall be binary and little-endian, built with explicit shifts and masks from one layout table of 24 message types, and every field shall use the fewest bits its valid range needs. The C and Python code shall encode every shared test vector identically. [CR1] |
| NFR13 | No shared value shall lose an update or be read half-written. The fault word uses atomic operations; time sync and game state are read through sequence-counter snapshots; the Pico handles all traffic from one polling loop; and a tile writes nothing to flash or NVS while armed or playing. [CR1, PostTri] |
| NFR14 | Each tile shall play from a copy of its asset in internal RAM. The 57,600 B asset plus a 32 KB margin (90,368 B) must fit in the largest free internal block once WiFi is running. [A4, OD-04] |
| NFR15 | The wall, the Pico's STATUS and the website shall show the same final score. [NR-11] |
| NFR16 | All C code shall follow Barr-C:2018 with 0 checker findings. The host unit tests (with ASan), pytest and the CI firmware builds shall pass before every commit. [NR-10] |
