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
| D-09 | Every tile stores the whole wall's video (option A), so a rotation or a layout change needs no re-upload. Subject to F-03 and F-04. |
| D-10 | New target: the rotate sprite. Turning that tile 90 degrees scores +1. If the whole board is turned while it is up, it also scores. |
| D-11 | Golden mole moves to stretch. Uploaded GIF artwork stays stretch. |
| D-12 | No Must or Should feature is cut. |
| D-14 | A13 and A14 added 9 Nov: on `12f2f8d` the website cannot pick a GIF or control video, and a round needs the Pico READY, which needs an uploaded video, so the website alone cannot run a game. |
| D-13 | Cheston's 5 Nov colour order (R, G, B), mount offset and 90/270 remap fixes (`b3/showcase-5nov`, `f11e403`, `a7c4b10`) are brought into this branch. They are measured bench fixes, not mockup code. |

## 4. Functional requirements

Priority: **M** = Must (demo needs it), **S** = Should, **X** = Stretch. Verified by: **H** = host test, **B** = bench card, **O** = observed at a demo run. Numbers marked (p) are provisional until the feasibility bench or tuning confirms them.

### 4.1 Operator website (A)

| ID | Requirement | Pass criterion | P | By |
|---|---|---|---|---|
| A1 | Set the grid shape, rows x cols, with rows x cols at most 9 | Every shape from 1x1 to 3x3, 1x9 and 9x1 is accepted; a shape above 9 tiles is refused | M | H, B |
| A2 | Tap-to-place: the website highlights a slot, the tile tapped there fills it | 9 tiles placed with no wrong slot; a duplicate board number is refused | M | H, B |
| A3 | Turn a slot on or off | Turning a slot off removes it from video and game within one session update | M | H, B |
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
| E6 | In video, after a wall rotation each tile plays its new slice from the next frame boundary, with no re-upload and no stop | Upright video within 2 s of the board settling (p); NR-07 holds | M | B |
| E7 | In a round, a wall rotation does not stop the game; targets stay on their tiles, and wall drawings (B1, B5) redraw upright | Seen in a round; host test | M | H, B |
| E8 | The website shows the new shape and map after a wall rotation | Within 2 s | M | B |
| E9 | A tile that drops out mid-round is skipped; the round continues; the tile rejoins after | Unplug and replug during a round | M | H, B |
| E10 | Video mode keeps working as on master | F5 bench card | M | B |

### 4.6 Rotate sprite (G)

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
| F3 | Uploaded GIF artwork for sprites and the hit |
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

To be written in phase 3, after section 7 is answered.

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
