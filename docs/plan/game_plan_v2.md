# SE15 whack-a-mole: requirements v2 (frozen 9 Nov 2026)

Status: **requirements frozen 9 Nov 2026.** This file supersedes the scope, rules and roles in `docs/plan/whack_a_mole_replan.md` and `GAME_HANDOFF.md`. The replan stays as history; where the two disagree, this file wins. Design (phase 3) is written after the feasibility checks (phase 2) and goes into section 9 of this file.

Builder: Cheston (B3) builds everything on `feature/whack-a-mole`. Each buddy still defends their own subsystem at Tribunal 2 (section 8).

## 1. Context

| Role | Who | Needs |
|---|---|---|
| Player | One child aged 7 to 10, playing solo | Clear start, readable targets, instant feedback, no punishment, a reward at the end |
| Operator | A team member at the website | Fast setup, level choice, live score, tap tuning on the day |
| Assessor | The professor, Tribunal 2 | Synced tiles, any layout, measured results against targets, each buddy explains their part |

The wall is upright: tiles on a cardboard board, each tile on a pivot so a child can turn it. Up to 9 Waveshare ESP32-S3-Matrix tiles (5 now, 4 coming), one Pico W coordinator, a dedicated router, and the PC website.

## 2. Process (SDLC, V-model)

Every requirement below has a verification method. Host tests cover logic. Bench cards (written in phase 5) cover the hardware. Their results form the report's evaluation section.

| # | Phase | Output | Target date |
|---|---|---|---|
| 1 | Requirements | This file | Mon 9 Nov (done) |
| 2 | Feasibility | Answers to section 7 (F-01 to F-08) | Tue 10 Nov bench |
| 3 | Design | Game design, Pico and tile roles, protocol changes, state machines (section 9) | Wed 11 Nov, reviewed by Cheston |
| 4 | Implementation | Small increments, each passing the host gate | Thu 12 to Sun 15 Nov |
| 5 | Testing | Bench cards traced to every requirement | Sun 15 to Sat 21 Nov |
| 6 | Deployment | Demo setup and runbook | Sat 21 to Sun 22 Nov |
| 7 | Documentation | Evaluation section, defence docs, teammate briefings | Sat 21 to Sun 22 Nov |

Fixed dates that are not agent work: UDE2222 A2 (submit Thu 12 Nov), INF2004 Week 12 quiz and practical skills test (16 to 20 Nov), INF2001 M2 presentation Wed 18 Nov and report Fri 20 Nov, Tribunal 2 Mon 23 Nov. Bench time, Cheston's review and learning time, and briefing teammates for Tribunal 2 cannot be sped up. The 4 extra tiles have no arrival date yet.

## 3. Decisions (9 Nov 2026)

| ID | Decision |
|---|---|
| D-01 | Players are children aged 7 to 10, solo. |
| D-02 | No sound (no buzzer or speaker on the hardware). |
| D-03 | Scoring has no penalty: the score never goes down. Missed targets and wrong actions are counted separately. |
| D-04 | Three levels: Easy, Normal, Hard. |
| D-05 | A round lasts 30 s. |
| D-06 | The child starts the round by tapping a tile. |
| D-07 | Whole-wall rotation is in scope and is never locked, during a round too. It only reorients the wall. It is not a scored action. |
| D-08 | Whole-wall rotation is detected when every active tile turns by the same amount within one window. |
| D-09 | ~~Every tile stores the whole wall's video.~~ **Dropped 11 Nov (DS-14):** each tile stores only its own section, as in the brief and on `master`. |
| D-10 | New target: the rotate sprite. Turning that tile 90 degrees scores +1. If the whole board is turned while it is up, it also scores. |
| D-11 | Golden mole moves to stretch. Uploaded GIF artwork stays stretch. |
| D-12 | No Must or Should feature is cut. |
| D-15 | 10 Nov: **prototype first.** The rotate sprite (G1 to G9) moves to stretch (Cheston consults the professor on a workaround on Thu 12 Nov). Scoring is tap-the-mole only, highest peak wins (DS-03). Whole-wall rotation (E3 to E8) stays a Must but is built after the prototype. |
| D-16 | 10 Nov: **website phase must follow the brief's video path:** the PC decodes the GIF the user selects, divides each frame into sections and sends each section to its panel; tiles never parse GIFs. The bench used synthetic GIFs through `asset_builder`; A13 adds choosing a real GIF on the website. **Conflict to settle then:** D-09 (every tile stores the whole wall) sends each tile more than its section, which departs from the brief's "send each section to the microcontroller responsible". Options: keep D-09 and justify it in the report, or keep per-tile sections and re-split and re-upload after a wall rotation. |
| D-14 | A13 and A14 added 9 Nov: on `12f2f8d` the website cannot pick a GIF or control video, and a round needs the Pico READY, which needs an uploaded video, so the website alone cannot run a game. |
| D-13 | Cheston's 5 Nov colour order (R, G, B), mount offset and 90/270 remap fixes (`b3/showcase-5nov`, `f11e403`, `a7c4b10`) are brought into this branch. They are measured bench fixes, not mockup code. |

## 4. Functional requirements

Priority: **M** = Must (demo needs it), **S** = Should, **X** = Stretch. Verified by: **H** = host test, **B** = bench card, **O** = observed at a demo run. Numbers marked (p) are provisional until the feasibility bench or tuning confirms them.

### 4.1 Operator website (A)

| ID | Requirement | Pass criterion | P | By |
|---|---|---|---|---|
| A1 | Set the grid: rows and cols each 1 to 9 (up to 81 cells), with up to 9 tiles placed in any cells; empty cells stay blank (sparse grid, revised 11 Nov, DS-15) | 1x1 to 9x9 accepted, 10 refused; 9 tiles on a 5x5 diagonal play their cuts, the other cells blank | M | H, B |
| A2 | Tap-to-place: the website highlights any cell of the grid; the tile tapped then is placed there | 9 tiles placed with no wrong cell; a board number already placed is moved, not duplicated | M | H, B |
| A3 | Turn a placed tile on or off; an off tile goes blank, keeps its cell and is left out of video and game | Off removes it within one session update; on restores it in the same cell | M | H, B |
| A4 | Switch between video and game mode, only while stopped | Switching is refused while playing or in a round | M | H, B |
| A5 | Choose the level: Easy, Normal or Hard | The round runs the chosen level's timings (B4) | M | H, B |
| A6 | Arm a round (B7) and abort a round | Abort clears every tile and stops all game messages within 500 ms | M | H, B |
| A7 | Live panel: score, missed targets, wrong actions, time left | The panel matches the Pico's counts within 500 ms during a round | M | H, B |
| A8 | Tile health: online tiles, each tile's orientation and grid position | Unplugging a tile shows it offline within 3 s | M | B |
| A9 | Results after a round: hits, missed targets, wrong actions, accuracy, average reaction time (tap time minus show time, hits only) | The figures match the Pico log of the round | S | H, B |
| A10 | Best score per level for the session | A higher score replaces the best and is marked "new best" | S | H |
| A11 | Tap test mode (while stopped): a tap lights its tile and the website shows that tap's peak | Every tap on every tile shows within 500 ms | S | B |
| A12 | Tap sensitivity set from the website; the PC re-sends it at every session start, tiles keep it in RAM | A new threshold acts on all tiles within 1 s, with no reflash | S | H, B |
| A13 | Video from the website: choose a GIF, then play, pause and stop it. The website alone runs the whole demo, with no terminal dashboard. | Every step from power-on to video playing and to a game round is done on the website | M | H, B |
| A15 | Every wall picture (countdown, score, stars) is readable on every grid: spread across the tiles when they cover enough of the grid, otherwise each tile shows the whole small picture (sparse fallback, DS-15) | Host test renders each picture for 1x1 to 3x3 filled, 1x5, 1x9 and sparse grids (5x5 diagonal); checked on the wall | M | H, B |
| A16 | A big-screen scoreboard page (`/score`): score, time left, stars and the round state in large type, for a TV or projector (DS-16) | Readable from 3 m on a laptop screen; updates within 0.5 s of the Pico | S | B |
| A14 | Game mode needs no video upload: a session with the layout and every mapped tile online is enough to arm a round | A round starts on a wall that has never had a GIF uploaded | M | H, B |

### 4.2 Round flow (B)

| ID | Requirement | Pass criterion | P | By |
|---|---|---|---|---|
| B1 | A 3-2-1 countdown across the whole wall, 1 s per digit, before the first target | The digits read upright on every shape in A1 and after any wall rotation | M | H, B |
| B2 | A round lasts 30 s from the end of the countdown | No target shows after 30 s; the round ends once the last target is judged | M | H |
| B3 | One target at a time, on a random active tile, never the same tile twice in a row (unless one tile is active) | 1,000 simulated targets: no repeat on a 2+ tile wall | M | H |
| B4 | Speed per level: mole up time shrinks linearly over the round. Easy 2000 to 1200 ms, Normal 1500 to 800 ms, Hard 1200 to 500 ms (p). 300 ms gap between targets (p). | Host test pins the up time at the start, middle and end for each level | M | H |
| B5 | Round end: targets stop, a short end animation, then the score and stars (C6) shown in turn, held until the next round is armed | Seen on the wall; score matches the website and the Pico status | M | H, B |
| B6 | Abort clears the wall at once | See A6 | M | B |
| B7 | Child start: arming shows a start sprite on the active tile nearest the wall's centre; tapping it starts the countdown | A tap on the start sprite starts B1; a tap elsewhere does nothing | S | H, B |
| B8 | Last 5 s warning: empty tiles pulse faintly | Seen on the wall | X | O |

### 4.3 Scoring (C)

| ID | Requirement | Pass criterion | P | By |
|---|---|---|---|---|
| C1 | A hit on a mole scores +1 and ends that mole | Host test | M | H |
| C2 | The score never goes down. A tap on a tile with no target, or on the wrong tile, counts one wrong action. | Host test: score unchanged by wrong actions | M | H |
| C3 | A target that times out counts one missed target | Host test | M | H |
| C4 | Taps from several tiles within 50 ms count once; the highest peak wins | Host test; NR-05 on the bench | M | H, B |
| C5 | A tap stamped before the hide time still hits if it arrives up to 100 ms late | Host test | M | H |
| C6 | Stars at the end, from hits over targets shown: 3 stars at 80% or more, 2 at 50% or more, else 1 (p). A child always gets at least 1 star. | Host test | S | H |
| C7 | Golden mole: rare (about 1 in 8), worth +3 | Host test | X | H |

### 4.4 Tile visuals (D)

All drawn by the tile itself, with channel values 0 to 8 (brightness cap, NR-09).

| ID | Requirement | Pass criterion | P | By |
|---|---|---|---|---|
| D1 | The mole is an 8x8 sprite with a face, not a flat fill | Seen on the wall at every orientation | M | H, O |
| D2 | The mole pops up row by row, about 150 ms | Seen; host test of the frames | M | H, O |
| D3 | A hit shows a green burst on that tile at once; the tile shows it locally, the Pico confirms the score | NR-03 | M | H, B |
| D4 | A wrong tap shows a short red X | Seen | M | H, O |
| D5 | A missed target ducks back down | Seen | M | H, O |
| D6 | Empty tiles show a faint hole ring during a round | Seen | S | H, O |
| D7 | Hit, wrong and missed differ by shape and motion, not only by colour (red-green colour blindness) | Each is recognisable in a greyscale photo | M | O |

### 4.5 Wall and rotation (E)

| ID | Requirement | Pass criterion | P | By |
|---|---|---|---|---|
| E1 | Any layout from A1, in video and game mode | F5 and F6 bench cards on 1x5, 2x2, 3x3 and 1x9 | M | B |
| E2 | Each tile draws its picture upright for its own orientation | All four angles upright and not mirrored on one tile | M | H, B |
| E3 | Whole-wall rotation: when every active tile reports the same turn (+90, -90 or 180) within 1.5 s (p) of the first tile's change, the Pico rotates the grid map. Each tile's final orientation at the window's close decides, so a 180 turn that passes through 90 counts once. | NR-08 | M | H, B |
| E4 | A partial or mixed turn (not every tile, or different amounts) changes nothing for the wall; each turned tile only redraws upright (E2) | NR-08 | M | H, B |
| E5 | The map rotation also turns the shape: 2x3 becomes 3x2, 1x9 becomes 9x1. Tap-to-place is not needed again. | Host test for every shape in A1 | M | H |
| E6 | In video, after a wall rotation the PC re-cuts the GIF for the new shape and re-uploads each tile's section (in parallel); the video resumes upright on every tile together (revised 11 Nov, DS-14) | Video resumes upright and in sync within 10 s of the board settling (p, set in step 4); NR-07 holds | M | B |
| E7 | In a round, a wall rotation does not stop the game; targets stay on their tiles, and wall drawings (B1, B5) redraw upright | Seen in a round; host test | M | H, B |
| E8 | The website shows the new shape and map after a wall rotation | Within 2 s | M | B |
| E9 | A tile that drops out mid-round is skipped; the round continues; the tile rejoins after | Unplug and replug during a round | M | H, B |
| E10 | Video mode keeps working as on master | F5 bench card | M | B |

### 4.6 Rotate sprite (G) - STRETCH from 10 Nov (D-15)

| ID | Requirement | Pass criterion | P | By |
|---|---|---|---|---|
| G1 | Sometimes a rotate sprite (for example a top with a curved arrow) shows instead of a mole, about 1 in 5 targets (p) | Host test of the ratio over 1,000 targets | M | H |
| G2 | Turning that tile 90 degrees scores +1. Easy: either direction. Normal and Hard: the arrow's direction only; the wrong direction counts one wrong action. | Host test; bench | M | H, B |
| G3 | The sprite stays up longer than a mole: Easy 3000 ms, Normal 2500 ms, Hard 2000 ms (p) | Host test | M | H |
| G4 | A tap on the sprite does not score or count as wrong; the sprite wiggles as a hint | Host test; seen | M | H, O |
| G5 | Taps are ignored on a tile while it is being turned | NR-02 | M | B |
| G6 | After the turn, the tile's picture is upright and its grid position is unchanged | Seen | M | B |
| G7 | The tile shows success at once when its own sprite is turned the right way; the Pico confirms the score after the E3 window | NR-04 | M | H, B |
| G8 | Turning a tile with no sprite counts one wrong action once the E3 window has closed and it was not a wall rotation. No red cue (it would come late). | Host test | M | H |
| G9 | A whole-wall rotation while a sprite is up scores the sprite, if the turn matches G2 | Host test | M | H |

### 4.7 Stretch (F)

| ID | Feature |
|---|---|
| F1 | Decoy "don't hit" target; a hit on it counts one wrong action |
| F2 | Two targets at once on Hard |
| F3 | Uploaded GIF artwork for sprites and the hit; a PC-uploaded full-colour graphics pack for the countdown and end screens (DS-09) |
| F4 | Streak celebration: 5 hits in a row sparkles the whole wall |
| F5 | Leaderboard with names on the website |
| C7 | Golden mole (section 4.3) |
| B8 | Last 5 s warning (section 4.2) |

If time runs out, stretch goes first, then the S rows in this order: B8, A10, D6, A9, C6, B7, A12, A11. A11 and A12 go last because they are the tools for tuning taps at the demo.

## 5. Non-functional requirements

| ID | Requirement | Pass criterion | By |
|---|---|---|---|
| NR-01 | Taps detected | At least 95% of 50 deliberate taps, soft fingertip and open palm, on every tile (p, set by F-05) | B |
| NR-02 | False taps | At most 1 false tap on a neighbour per round, including hard slaps; 0 false taps from turning a tile over 20 turns | B |
| NR-03 | Hit feedback latency | Tap to hit burst under 100 ms, measured with GPIO4 event pulses and pico_tap | B |
| NR-04 | Rotate feedback latency | Success shown within 500 ms of the tile settling | B |
| NR-05 | No double counts | Each of 50 deliberate hits scores exactly once | B |
| NR-06 | Reliability | 10 rounds in a row with no reboot; a tile unplugged mid-round does not stop the round | B |
| NR-07 | Video sync | Sync skew under 5 ms, before and after a wall rotation | B |
| NR-08 | Rotation detection | 10 of 10 whole-board turns (90 clockwise, 90 anticlockwise, 180) detected correctly; 0 of 10 single-tile turns read as a wall rotation | B |
| NR-09 | Brightness and packing | Every channel value at most 8 (the 8/255 cap); video assets stay 4-bit packed | H |
| NR-10 | Code standard | Barr-C (`tools/barr_check.py`), every new C file in `tools/barr_scope.txt`, host gate all PASS, CI green | H |
| NR-11 | Score consistency | Wall, Pico status and website show the same final score | B |
| NR-12 | Setup time | Power-on to first armed round in at most 3 minutes, with tiles already placed | B |
| NR-13 | Upload time | A whole-wall video asset reaches every tile within 60 s (p, set by F-04) | B |
| NR-14 | Defensible | Each buddy's defence doc covers their part of the game (section 8) | O |

## 6. Constraints

- The wall is upright: rotation is sensed from gravity, so a flat wall cannot detect turns.
- Brightness cap 8/255 and 4-bit packing stay (defended at Tribunal 1).
- Game and control messages are UDP unicast with seq and ACK, never broadcast (2.8% broadcast loss on the router).
- Node identity is the board number 1 to 9 in NVS.
- Protocol changes go into `protocol/layout_table.md`, both codecs (`common/proto`, `pc/proto_layout.py`) and the vectors, together.
- No new dependencies. Never commit `wifi_secrets.h`. Do not change `.github/`.
- Out of scope: sound, multiplayer, adaptive difficulty, lives mode.

## 7. Feasibility checks (phase 2, before design)

| ID | Question | Why it matters | How |
|---|---|---|---|
| F-01 | Do the branch's tile and Pico firmware compile? | Nothing on this branch has been built on ESP-IDF or the Pico SDK | Card F0 |
| F-02 | Is the QMI8658 interrupt pin wired to a GPIO? | Tap design: interrupt or polling | Card H5 (schematic, continuity) |
| F-03 | Is PSRAM enabled, and how much is free for a whole-wall asset? | D-09: a 3x3, 600-frame asset is 518 KB per tile | Read `sdkconfig`, boot log heap report |
| F-04 | How fast does an upload run to one tile and to all tiles? | NR-13 and the frame cap for D-09 | Time an upload of the current asset |
| F-05 | Tap peaks: child-strength taps against neighbour and board noise | NR-01 and NR-02 thresholds | Card H6, with soft and palm taps |
| F-06 | What does twisting a tile on a pivot look like to the IMU? | G5: turns must not read as taps | H6 logger while turning a tile |
| F-07 | How quickly does orientation settle after a turn, and after a whole-board turn? | E3 window and NR-04 | Orientation log while turning |
| F-08 | Can a child turn a tile on the chosen pivot without pulling the cable? | G1 is physical | Build one pivot, try 20 turns each way |

## 8. Tribunal 2 defence map

Cheston builds all of it; each buddy must be able to explain their part. Briefings and defence docs are part of phase 7.

| Buddy | Subsystem | Game parts to defend |
|---|---|---|
| B1 Jolyn | Website, dashboard, asset builder | A1 to A12, whole-wall asset build (D-09) |
| B2 Benjamin | Pico coordinator, game engine, layout table | B1 to B7, C1 to C6, G1 to G3, G8, G9, E3 to E5 decision, protocol changes |
| B3 Cheston | Tile comms and storage | TAP, ROTATE and game message handling, ACKs, whole-wall asset storage (D-09) |
| B4 Ahsanul | Timing and mode switching | Countdown and target timing on the corrected clock, mode switch, video sync after rotation (E6, NR-07) |
| B5 Jia Jun | Tap detection, orientation, display | D1 to D7, E2, G5 to G7, NR-01 to NR-04 |

## 9. Design

Phase 3, decided one by one with Cheston from 9 Nov 2026. Evidence: `docs/bench/9nov/feasibility.md`.

### DS-01 Roles (decided 9 Nov)

- **Pico decides:** round states and level timings, target choice (tile, kind, up time), scoring and counts (hits, missed targets, wrong actions, reaction times), tap arbitration (DS-03), taps during turns (DS-04), whole-wall rotation and the map (DS-06), wall pictures (DS-07); plus the corrected clock, beacons and video sync as today; reports to the PC in STATUS.
- **Tiles sense and draw, never score:** tap detection (DS-02), turns as ROTATE (DS-05), their own tap blanking while turning (DS-04); every sprite and animation drawn procedurally; wall-picture slices; video frames.
- **Fast feedback, Pico-picked (revised 9 Nov after F-10):** a tile cannot tell its own tap from a neighbour's echo (both several g on any mounting tried), so the tile never decides a hit alone. The Pico picks the tapped tile (DS-03) and tells only the winner; the winner flashes if its own spin check (DS-02) also says "tap". Both run in parallel, so the flash comes about 60 to 80 ms after the touch. The rotate success is still shown locally (only the turned tile turns). The first version of this point (the tile flashes alone, safe because "the target tile wins") was dropped: F-10's rule replay showed "target wins" gives a free hit on every slap next to the mole.
- **PC website sets up and reports:** layout, tap-to-place, slots, level, arm, abort, video control (A13), tap test and sensitivity, live panel, results; no part in round timing.
- MOLE_ON becomes a target message with a kind field (mole, rotate, start; later golden, decoy).

### DS-02 Tap detection (decided 9 Nov)

- **Sampling:** the tile polls the QMI8658 accelerometer and gyroscope at about 900 samples/s (6-axis output rate) in the orientation task, replacing the 100 Hz reads and the QMI8658 tap engine. The orientation filter uses a decimated copy. The rate is confirmed by a CPU check during implementation (F-05, F-09: 878 to 978 samples/s, 0 overruns, in the logger).
- **Detector:** baseline per axis (EMA, tau 500 ms, held during a spike, reseeded after a 200 ms hold); a spike is a deviation over the threshold, default 200 mg, set from the website (A12). The detector re-arms when the deviation stays under half the threshold for 30 ms, at most 600 ms after the spike (a slap rings up to 600 ms).
- **Tap or turn:** 50 ms after the spike, the tile sums the in-plane rotation (gyroscope Z). Under 8 degrees: a tap. 8 degrees or more: a turn starts; taps are blocked until 200 ms after the orientation settles, then ROTATE is sent. F-09: all 40 taps and slaps under 2.4 degrees at 50 ms; 13 of 20 turns over 8 degrees at 50 ms, 19 of 20 at 80 ms.
- **Turns that slip through:** if a ROTATE whose motion started within 600 ms of a TAP arrives from the same tile, the Pico drops that TAP's wrong-action count. A hit is never taken back: **any touch on a mole whacks it** (option A, 9 Nov), so twisting the mole tile can score.
- **Feedback:** only the hit burst is local (DS-01, option A). The red X for a wrong tap comes from the Pico after arbitration (DS-03), because a slap shakes the neighbouring tiles.
- **Tap time:** the first threshold crossing, in the Pico's corrected clock; TAP carries the peak. **TAP is sent at the spike** (revised with DS-03), before the 50 ms spin check ends; the spin verdict follows (DS-04).

### DS-03 Which tile was tapped (decided 9 Nov)

- **Every tile sits on a soft mount** (sponge or foam) between it and the board: new constraint. F-10: on sponge the tapped tile had the highest peak in 40 of 40 taps and slaps, against 34 of 40 slaps on the bare box.
- **Pico rule: highest peak wins.** TAPs whose times are within about 30 ms of the first form one group; the Pico judges the group once their TAPs have had time to arrive. The highest peak is the tapped tile; the other TAPs of the group are echoes and are dropped.
  - Winner has the target, inside its show time: a hit; the Pico sends the winner FEEDBACK hit at once, and the winner flashes if its spin check says tap (DS-01).
  - Winner has no target: one wrong action; the Pico sends the winner FEEDBACK miss (red X).
- Rejected: "target wins" (a free hit on every slap next to the mole, F-05 and F-10 replay), and the peak-ratio rules (free hits on 1 to 9 of 20 slaps on sponge).
- To re-check on the real board with more tiles (F-10 used 2 tiles, 10 taps of each kind).

### DS-07 Prototype tap flow (decided 11 Nov)

For the prototype (D-15), no spin check: the tile sends TAP (peak, time in the Pico clock) at the spike, resent every 50 ms until ACKed. The Pico groups TAPs by tap time, not arrival; a group closes **30 ms** after its first tap (v1 used 50 ms; bench echoes came 2 to 3 ms after the tap, on tile clocks). Highest peak wins; only the winner gets FEEDBACK (hit: green burst and the mole ends; miss: red flash, red X from step 3). A late TAP inside a closed group's window is dropped as a duplicate (v1). Every message is final: nothing is corrected or taken back. Expected tap to flash: about 40 to 50 ms. Accepted weak spots: a lost TAP from the tapped tile lets the echo win (about 1 in 100 at 1% loss; counted in NR-05); the echo gap is to be confirmed on the Pico clock in the first run; no protection against board turns until E3. v1's -1 for a wrong tap stays for the first run and becomes "no penalty" in step 3.

**Parked with E3 (after the prototype):** DS-04 and DS-05 (turns), DS-06 (whole-wall rotation), the DS-02 spin check.

### DS-08 Round states on the Pico (decided 11 Nov)

GAME IDLE -> ARMED -> COUNTDOWN -> ROUND -> RESULTS, all decided on the Pico.

| State | Wall | Taps | Leaves when |
|---|---|---|---|
| GAME IDLE | Faint holes or dark | Ignored | Website Arm (with a level) -> ARMED. Switch to video mode only here or in RESULTS. |
| ARMED | Start sprite on the active tile nearest the wall's centre | Only a tap on the start sprite counts | Start sprite tapped -> COUNTDOWN; website Start now -> COUNTDOWN; **60 s with no tap -> GAME IDLE** |
| COUNTDOWN | 3, 2, 1 across the wall, 1 s each (DS-09) | Ignored | After 3 s -> ROUND |
| ROUND | Moles, hit bursts, red X | Scored (DS-07) | 30 s up and the last mole judged -> RESULTS |
| RESULTS | End animation, then score and stars in turn (DS-09) | Ignored | Next Arm -> ARMED |

- Abort (website) from ARMED, COUNTDOWN or ROUND clears the wall and returns to GAME IDLE.
- A tile lost mid-round is skipped; the round continues (v1, E9).
- **A14:** in game mode a tile is ready when online and clock-synced; no uploaded video is needed. Video mode keeps today's checks.
- STATUS gains the round state, so the website can show it (protocol list, DS-10).
- Build order: step 1 runs v1 (game start goes straight to ROUND, score at the end, video upload still needed); step 3 adds ARMED, COUNTDOWN, RESULTS and Start now; step 4 adds the website buttons and A14.

### DS-09 Wall pictures (decided 11 Nov)

- **The Pico draws** the countdown, end animation, score and stars: it knows the score at once, holds the map, and needs no PC during a round (DS-01). PC-drawn pictures would go over the upload path (about 1 s per tile, one tile at a time: about 9 s on 3x3) and fail if the PC stops.
- **A timed list per tile:** the Pico renders each picture for the whole wall, cuts one 8x8 piece per tile and sends each tile a list of up to 4 pieces, each with a colour, a start time on the corrected clock and a duration, plus a repeat flag. Every tile switches at the listed time, so pictures change together however late the message arrived; the list goes out about 0.5 s ahead.
- **WALL_PICTURE replaces SCORE_MASK** (type 22; the layout table is still provisional). Fields in DS-10. The sparkle effect is drawn by the tile (no mask).
- **8 colours** within the 0 to 8 channel values: white, yellow, green, red, blue, orange, purple, cyan.
- **Scales to every shape** (new requirement A15 below): the picture is drawn at the wall's size (cols x 8 by rows x 8 pixels) with the largest whole-number scale of the font that fits: 1x1 shows a 3x5 digit and two-digit scores (7 px wide; no minus sign now that the score never goes below 0); 2x2 scales 3x for the countdown; 3x3 scales 4x; strips keep the small font. Stars: 1 to 3 blinks of one star on 1x1, a row of stars elsewhere. Centred on the active tiles; pixels on blank slots are not shown. Redrawn for the new shape after a wall rotation.
- **Stretch (under F3):** the PC uploads a full-colour graphics pack for the current layout while the wall is in GAME IDLE; the Pico then only says "show image n at time T".

### DS-10 Protocol changes, one batch (decided 11 Nov)

Step 1 runs v1 with no wire change. Before step 3, one batch changes `protocol/layout_table.md`, both codecs (`common/proto`, `pc/proto_layout.py`) and the vectors together (Benjamin defends the table at Tribunal 2). No new type codes: three rows extended, one replaced.

| Row | Change | For |
|---|---|---|
| MOLE_ON becomes TARGET (19) | Adds kind (3 bits: mole, start; rotate, golden, decoy reserved for stretch). up_ms widened 11 to 12 bits (to 4,095 ms) now, so the stretch rotate sprite needs no later change. A start sprite sends up_ms 0: up until tapped or cleared. | DS-08, stretch room |
| GAME_CONTROL (21) | New commands ARM, START_NOW, TAP_TEST_ON, TAP_TEST_OFF; a level field (2 bits: Easy, Normal, Hard) used with ARM; the tap threshold (from SESSION_CONFIG) carried to each tile in ARM. END stays the abort. | DS-08, levels, A11, A12 |
| SCORE_MASK becomes WALL_PICTURE (22) | Node ID, list slot (2 bits), effect (2 bits: picture, sparkle, clear), colour (3 bits), repeat (1 bit), start time (64 bits, Pico clock), duration (13 bits, ms), mask (64 bits). About 23 bytes. | DS-09 |
| STATUS (15) | Adds round state, level, missed moles, wrong taps, moles shown, time left, average reaction time (about 13 bytes in all). The website computes stars and accuracy from the same counts. | A7, A9 |
| SESSION_CONFIG (2) | Adds the tap threshold; the Pico forwards it in ARM (PC to Pico to tiles, no new tile port). | A12 |
| TAP, FEEDBACK, ACK | Unchanged (DS-07). | |
| MOTION | Not now: reserved for E3 (DS-04 to DS-06). | |

**As built (11 Nov 2026, step 3):** MOLE_ON keeps its name in the code (the table calls it the target row); the tap threshold travels in GAME_CONTROL ARM (20 mg steps) instead of SESSION_CONFIG, so the layout message is unchanged in step 3; the sparse-grid positions (DS-15) move to step 4 with the website and `asset_builder`. The Pico's acked outbox grew to 48 slots of 24 bytes (WALL_PICTURE is 23 bytes, and the countdown sends 27 at once), with a compile-time size check.

Tap test (A11): in GAME IDLE with tap test on, the Pico answers every TAP with FEEDBACK hit (the tile lights) and forwards the peak to the PC over the path that tap-to-place already uses. Exact widths are fixed when the batch is written, by the table's width rule.

### DS-11 Visuals (decided 11 Nov)

Drawn procedurally by the tile on 8x8, every channel 0 to 8, frames computed from the elapsed time on the shared clock (60 fps, no blocking in the draw path). Sprites as proposed 11 Nov, tuned on the real tiles in step 3:

- **Mole:** brown body, white eyes, pink nose (8x8). **Appears** by rising out of the hole, 2 rows every 40 ms (160 ms); **missed**: sinks back (160 ms, reversed).
- **Hole** (D6): dim purple arc on the bottom two rows of empty tiles during a round.
- **Hit:** the mole squashes (2 frames), then a green star burst with a yellow centre expands (3 frames), 250 ms.
- **Wrong tap:** red X, blinks twice, 300 ms.
- **Start sprite:** green play triangle, pulsing between half and full brightness once a second.
- **Wall pictures** (DS-09): yellow countdown digits, white score, yellow stars.
- Hit and wrong differ by shape (burst against X), not only colour (D7).

**As built (11 Nov 2026, step 3, host-tested; wall test pending):**
- Results list per tile: sparkle 1.5 s, then the white score and the yellow stars 2.5 s each, looping until the next ARM or START. Countdown: 3, 2, 1 in yellow, 1 s each, 0.5 s after the start tap. Pictures scale by the largest whole number that fits the active tiles' box; stars are 5x5 (or 3x3 on 1x1 and 1x2) in a row or a pyramid, whichever is drawn larger; a wall under half full shows the whole small picture on each tile (score_font.c, host test C19b).
- Tile ordering: MOLE_ON and FEEDBACK act only when newer than every message accepted; GAME_CONTROL when newer than the last GAME_CONTROL, and it clears only an older target or picture entry (a lost ARM resent after its start sprite still arms and keeps the sprite); WALL_PICTURE entries may arrive in any order (newer than their entry and than the last ARM or START).
- The sprites live in `panel_sync/main/game_sprites.c` (Jia Jun's display subsystem); the tap pulse comes from the Orientation Task through an esp_timer one-shot, the mole and hit pulses from the frame where they start.
- STATUS keeps the final score in RESULTS; the PC reads the round's end from RESULTS, not from the coordinator state.

### DS-12 Upload fixes (decided 11 Nov; built in step 4)

Three failures seen on the 9 Nov bench (F-04). No wire change; the tile's silent drop of an upload for another session (T-X-08) stays.

| Problem | Fix | Where |
|---|---|---|
| A. Session race: after SESSION_CONFIG the first tile gets its MANIFEST before a beacon carries the new session, drops it, and the uploader resends chunks but never the MANIFEST (90 s, then 0 of 57) | A1: the uploader waits until the tile's TELEMETRY header shows the new session before its MANIFEST (adds up to about 1 s). A2: if a tile ACKs nothing in one round, the uploader resends the MANIFEST before more chunks (also covers a lost MANIFEST). | `pc/uploader.py`, `pc/dashboard.py` |
| B. A tile already holding the same asset (session and CRC) answers only the Pico, so the PC waits about 70 s (5 Nov) | The reuse path also answers the PC: all chunks ACKed | `asset_rx` / `asset_store` (Cheston's module) |
| C. Last CHUNK_ACKs lost: "55 of 57" while the Pico already counts the tile ready (R1-8) | When retries run out, the uploader checks the Pico's STATUS readiness and counts a ready tile as uploaded | `pc/uploader.py`, `pc/dashboard.py` |

Step 1 keeps the "upload twice in the same session" workaround.

### DS-13 Measuring the targets (decided 11 Nov)

| Target | Method |
|---|---|
| NR-03 tap to hit flash | Game-mode event pulses on each tile's GPIO4, recorded by pico_tap; cross-checked by phone slow-motion video (finger to flash) |
| NR-01 taps detected | Tap test mode with the prompter's fixed schedule (`docs/bench/9nov/prompter.py`) |
| NR-02, NR-05 echoes and double counts | Pico event log: each group, its winner, the dropped echoes |
| NR-07 video sync | Tick pulses in video mode (card H4), unchanged |
| Mole shown together on every tile | The "mole shown" pulse on each tile |

- **GPIO4 in game mode** carries one pulse per event instead of the tick pulses: tap detected 2 ms, hit burst first frame 6 ms, mole first frame 10 ms. Video mode keeps the 1 ms tick and 4 ms marker. `pc/tools/skew_analyse.py` already classifies pulses by width. Detection lags the touch by about 1 to 2 ms (900 samples/s), stated in the report; the phone video covers finger to flash.
- **Hardware:** Pico `…3B6838` becomes the pico_tap recorder (no WiFi needed, so its join hangs do not matter); Pico `…595138` is the coordinator. Each measured tile needs GPIO4 and GND wired to pico_tap (no header pins fitted: solder or press-fit). Calibrate pico_tap first (card H3).

### DS-14 Video storage (decided 11 Nov): per-tile sections, D-09 dropped

- **Each tile stores only its own section**, as the brief asks ("divide each frame into the appropriate sections, and send each section to the microcontroller responsible") and as `master` and the 5 Nov showcase already do (`partition_tiles()`, one `node_<n>.bin` per tile).
- **D-09 (every tile stores the whole wall) is dropped.** It would only make rotation instant for square walls and 180 degree turns; a 90 degree turn of a non-square wall (1x5 to 5x1, 2x3 to 3x2) needs a new crop of the GIF anyway, so a re-cut and re-upload is needed regardless. It also cost 9 times the upload and PSRAM.
- **After a whole-wall rotation in video mode** the PC re-cuts the GIF for the new shape and re-uploads; the video pauses and resumes upright on every tile together (existing pause and resume at a frame boundary). Game mode is instant: the Pico redraws (DS-09).
- **Uploads go to all tiles at the same time** (today one tile after another: 600 frames take 5.2 s per tile, about 47 s for 9 tiles; parallel, about 6 to 10 s, to measure in step 4). Each tile still receives only its own section.

### DS-15 Sparse grid (decided 11 Nov, professor's suggestion)

- **The grid can have more cells than tiles:** rows and cols each 1 to 9 (the existing 4-bit fields), up to 9 tiles placed in any cells (a 5x5 diagonal, an L, a ring). Replaces the "rows x cols at most 9" rule.
- **Video:** the PC crops the GIF to the whole grid's shape (cols x 8 by rows x 8 pixels) and cuts a section only for each placed tile; empty cells are simply not shown. Each tile still receives only its own section (DS-14).
- **Layout message:** SESSION_CONFIG's 9-slot row-major node_map becomes a position per board number: row and col (4 bits each) for Node ID 1 to 9, plus a placed bit (joins the DS-10 batch).
- **Website:** grid size up to 9x9; click any cell, then tap the tile for it; each placed tile switchable on or off (off: blank, keeps its cell, out of video and game).
- **Game:** moles only on placed tiles that are on. **Whole-wall rotation** rotates every tile's (row, col) within the grid.
- **Wall pictures on a sparse grid** (A15): when the placed tiles cover less than half of the grid's cells, the Pico sends every tile the whole small picture (the 1x1 rendering) instead of one big picture cut across the grid.

### DS-16 Website layout (decided 11 Nov)

- **One page, three tabs** (Setup, Video, Game) under a status bar that is always visible: Pico state, tiles online, mode, and any problem (tile offline, upload failed).
- **Setup:** grid size (up to 9x9); the grid editor (click an empty cell, then tap its tile; click a tile to switch it on or off); per-tile health (online, on or off, orientation); tap test and the sensitivity slider (A11, A12).
- **Video:** choose a GIF from the browser; it is sent to the PC and kept in a library; a preview shows exactly how it is cut for the current grid (empty cells greyed); Upload to wall with progress per tile; Play, Pause, Stop (A13).
- **Game:** level (Easy, Normal, Hard); Arm, Start now, Abort; round state ("ARMED, waiting for the tap"); live score, missed, wrong, time left; results (hits, missed, wrong, accuracy, average reaction time, stars, best today) (A5 to A10).
- Buttons are enabled only when the action is allowed (as today); the page polls the PC 4 times a second (no WebSockets).
- **Scoreboard page `/score`** (A16): score, timer, stars and "Get ready!" in large type for a TV or projector, from the same data.

### DS-17 Demo network (decided 11 Nov)

- **The Matrix router, dedicated to this wall:** plain WPA2 on 2.4 GHz, no internet, AP isolation off; the Pico `…595138` keeps its reserved IP 192.168.1.154. Cheston can get it back from the teammate at any time.
- **One Pico per network:** two coordinators make each tile lock onto whichever it hears first (`coord_lock`, 9 Nov).
- Rejected: a phone hotspot (client cap: the demo needs 11 devices; battery) and the Pico soft AP (about 3 tiles). The Pico W cannot join WPA2/WPA3 mixed networks (home router, 9 Nov).
- **Switching back:** both `wifi_secrets.h` files to Matrix (the tiles' copy is backed up at `~/.config/se15/panel_wifi_secrets_matrix.h`; the Pico's must be typed in again). The laptop joins Matrix (the website runs locally). For a phone or TV to open the website: `sudo ufw allow in on wlp0s20f3 from 192.168.1.0/24 proto tcp to any port 8080`.
- Development continues on the hotspot with 2 tiles; the 9-tile tests move to Matrix.

### Build order (11 Nov)

| Step | Content | Decisions |
|---|---|---|
| 1. First run of v1 | Bring in the 5 Nov colour order, mount offset and 90/270 fixes (D-13; update the D4 to D6 rotation tests); group window 30 ms; run a round on nodes 4 and 5 from the dashboard (upload twice in one session) | DS-07 |
| 2. Tap detection | Software detector at about 900 samples/s, 200 mg, re-arm on quiet; sponge mounts | DS-02, DS-03 |
| 3. Child-friendly game | Protocol batch; ARMED, COUNTDOWN, RESULTS, Start now; no penalty; levels; wall pictures and sparse fallback; sprites and animations; game-mode GPIO4 pulses | DS-08 to DS-11, DS-13, DS-15 |
| 4. Website runs everything | Tabs, GIF library and preview, video control, game controls, results, `/score`; rounds without video (A14); upload fixes; parallel uploads; sparse grid in `asset_builder` | DS-12, DS-14 to DS-16 |
| 5. Whole-wall rotation | Spin check, turn messages, rotation on the Pico, video re-cut after rotation | DS-04 to DS-06 |
| Then | 9 tiles on Matrix (F8), measurements (F7), report and defence docs; stretch goals | DS-13, DS-17 |

### DS-04 Taps around turns (decided 9 Nov)

- **The turned tile corrects itself:** it sends TAP at the grab; if its 50 ms spin check finds a turn, it sends TURN_START (DS-05), ignores its own spikes until 200 ms after it settles, then sends ROTATE. No follow-up means the TAP was a tap, so a normal tap costs no extra message.
- **A hit that turns out to be a turn is never shown:** the winner flashes only if its spin check says tap (DS-01); the Pico cancels the hit on TURN_START, so nothing visible is taken back.
- **The group is a turn when its winner turns:** if the highest-peak TAP of a group comes from a tile that sends TURN_START, the whole group is dropped: no wrong action for any tile (F-06: the turned tile peaks at 0.9 to 2.2 g, its neighbour at about 1 g at most).
- **During a turn, all taps are ignored:** from a TURN_START until 200 ms after that tile settles (its ROTATE), the Pico drops taps from every tile. Safe with one target at a time; for stretch F2 (two targets) narrow it to the turned tile's neighbours.
- **Grab, pause, then twist** (missed by the spin check): a ROTATE whose motion began within 600 ms of a TAP from the same tile removes that TAP's wrong action; a hit on a mole stands (DS-02).
- **Whole-board turn:** every tile sends TURN_START, so no tap counts; the rotation itself is DS-06.

## 10. Changes from the 5 Nov replan

| Replan | v2 |
|---|---|
| Wrong tap -1 | No penalty; wrong actions counted (D-03) |
| Mole 1200 to 500 ms | Three levels (D-04); 1200 to 500 ms is Hard |
| Solid one-colour mole | Sprite with pop-up, burst, X and duck (section 4.4) |
| PC starts the round | Child taps a start sprite (D-06) |
| Whole-wall rotation out of scope | In scope, never locked (D-07, D-08) |
| Each tile stores its own slice | Each tile stores the whole wall (D-09) |
| Moles only | Rotate sprite added (D-10) |
| Stretch: several moles, decoy | Plus golden mole, GIF artwork, streak, leaderboard, last 5 s warning |
| Buddy build roles | Cheston builds all; buddies defend their part (section 8) |
| Never copy from `b3/showcase-5nov` | The measured colour and rotation fixes are brought in (D-13) |
