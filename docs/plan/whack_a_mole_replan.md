# SE15 re-plan after prof review (5 Nov 2026)

Prof asked for:
- An interactive whack-a-mole game, where hitting a tile counts as an action.
- Tiles that work in any position and orientation, including a single row.
- 9 ESP32 tiles (3x3).
- A website that decides which grid positions are on or off.

Ahsanul builds everything. The report still credits each buddy's subsystem.

Baseline is `master` of github.com/WetzUmbrella/Led_Matrix. The `b3/showcase-5nov` branch is a mockup used for inspiration only. Nothing from it is merged, copied or counted as project work.

## Locked decisions

**Physical**
- Upright wall: a cardboard board with tiles stuck on with Blu Tack. Looks and mounting are not graded.
- 9 Waveshare ESP32-S3-Matrix tiles at the demo (5 now, 4 coming).
- Brightness stays at master's existing 8/255 cap (about 3.1%, VC-6 / OD-02). This keeps the 4-bit packing defended at Tribunal 1.
- Power from one multi-port USB charger.

**Network**
- A dedicated router is the access point, because the Pico W soft AP only takes 3 ESP32s. Router settings: AP isolation off, no internet uplink, and bring it to the demo.
- The Pico gets a DHCP-reserved fixed IP. UDP broadcast announce is the fallback.
- All new game and control messages are UDP, unicast, with seq and an ack. Never use broadcast for them: the bench saw about 2.8% broadcast loss on the router.

**Tiles**
- A press is a QMI8658 IMU tap. An external GPIO button is the fallback only if false triggers can't be tuned out.
- Orientation comes from gravity via the existing orient_filter, with about 300 ms of stability required. Rotation is applied on the tile.
- The 90/270 degree remap in `display_frame.c` is verified on master from first principles: a host test pins the expected LED mapping for each rotation, then one physical tile is checked at each angle. Fix only if the test or the tile shows it's wrong.
- Identity stays the existing board number 1 to 9, stored in NVS. No MAC IDs.

**Layout and position**
- SESSION_CONFIG's 2-bit `layout` field becomes two fields: `rows` (4 bits, 1 to 9) and `cols` (4 bits, 1 to 9). Any shape where rows × cols > 9 is rejected. `node_map[0..8]` is row-major over rows × cols, and 0 means blank.
- This change touches:
  - `protocol/layout_table.md` (still PROVISIONAL; Benjamin's OD-07 freeze must include it)
  - `common/proto`
  - `pc/proto_layout.py`
  - the vectors
  - `asset_builder` splitting
- Tap-to-place: the website highlights a slot, you tap the tile there, and its TAP message carries its board number into `node_map`.
- Whole-wall rotation is out of scope. Re-run tap-to-place instead (about 30 s).

**Website**
- Hosted on the PC: a small Python `http.server` front end over the existing `DashboardModel`. The Pico firmware does not change for the website.
- Slot on/off writes a 0 or a board number into `node_map`, then rebuilds and re-uploads.
- Screens:
  - grid size (rows × cols)
  - tap-to-place wizard
  - slot on/off
  - mode switch (video or game; only while stopped)
  - game start
  - live score
- The page polls the dashboard about every 250 ms. No WebSockets.
- This remains Jolyn's subsystem (dashboard and media).

**Video on any layout**
- GIFs are centre-cropped to the layout's aspect ratio in `asset_builder`, and rebuilt when the layout changes. Include a strip-friendly GIF for the 1x9 demo.

**Game**
- New Pico state COORDINATING_GAME. It can only be entered from stopped (IDLE or READY) and is exclusive with COORDINATING_PLAYBACK. The dashboard gets matching GAME states.
- The Pico runs the game. Tiles only display and report taps.
- MOLE_ON and FEEDBACK are sent unicast to one tile. The tile draws the mole itself as a solid fill, with no uploaded asset.
- MOLE_ON carries show and hide times in the Pico's corrected clock (existing timesync).
- Tap dedupe: when several tiles tap within 50 ms, only the highest peak acceleration counts.
- v1 rules:
  - One mole at a time on a random active slot.
  - Each mole stays up for 1200 ms at first, shrinking to 500 ms over a 30 s round.
  - Hit scores +1. Tapping the wrong tile scores -1.
  - Visuals: mole in one colour, green flash on a hit, red flash on a miss.
  - Score updates live on the website. The final score is drawn across the wall at the end.
- Stretch goals, only if v1 is solid: several moles at once, and a decoy colour.

**Test and evaluation**
- Use the existing `tools/pico_tap` on a spare Pico (5 channels, tile GPIO4, output read by `skew_analyse.py`). Calibrate it first as its README says. gusmanb LogicAnalyzer is the fallback only if calibration fails.
- Tap latency is measured on the same GPIO4 pin, with a different pulse width for each event (tap interrupt vs hit flash). Each probed tile gets its own ground wire. Probe 3 or 4 tiles at a time.
- Targets:
  - sync skew under 5 ms
  - tap to hit flash under 100 ms
  - zero double-counted taps over 50 deliberate hits

## Build order (by dependency, no fixed dates)

1. **Base.**
   - Set up the router and reserve the Pico's IP.
   - Connect all 5 tiles on master.
   - Calibrate pico_tap.
   - Add the rotation-mapping host test and verify it on a tile.
2. **Tap detection.** Set up QMI8658 tap on an interrupt, then tune thresholds on the cardboard board. Everything after this depends on it.
3. **Protocol.**
   - rows/cols layout fields
   - new messages: TAP (board number, seq, peak, timestamp), MOLE_ON, FEEDBACK, game control, ACK
   - layout table, C and Python codecs, vectors and tests
4. **Layout on PC and Pico.**
   - rows × cols `node_map`
   - tap-to-place flow
   - asset_builder splitting and centre crop for any shape
5. **Website.**
   - `http.server` front end over `DashboardModel`.
   - Build the grid editor and tap-to-place first, then slot on/off and the mode switch.
6. **Video on any layout.** Confirm sync holds on 1x5, 2x2 and 5-in-3x3.
7. **Game.**
   - COORDINATING_GAME state on the Pico and the dashboard
   - unicast MOLE_ON/FEEDBACK with ack
   - dedupe
   - v1 rules
   - end score across the wall
8. **Scale to 9 tiles** once the boards arrive. Test 3x3 and 1x9 in both modes.
9. **Measure and write up.** Take pico_tap captures against the targets, and update layout_table, the statecharts and the evaluation section.

## Report allocation (mapped onto existing buddies)

- Jia Jun (B5): tap detection and on-tile rotation (display and orientation)
- Cheston (B3): TAP and game message handling on the tile (tile comms)
- Benjamin (B2): Pico COORDINATING_GAME, game engine, layout table freeze
- Jolyn (B1): website, layout and slot UI, asset_builder for any shape
- Ahsanul (B4): mode switching, game timing on corrected Pico clock, GIF sync on any layout
