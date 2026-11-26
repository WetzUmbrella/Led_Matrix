# playback_control: design defence (T-B4-04)

Subsystem: tile playback control (ESP32-S3 tile). Files: `panel_sync/main/playback_control.c/.h`; host tests `panel_sync/test/test_playback.c` ("Playback: Pass = 7 of 7", 25 Nov). Coding standard: Barr-C:2018, deviations D-1 (timeline seqlock and the state copy) and D-2 (the FreeRTOS wrapper). Updated 25 Nov 2026 against `a6d4902`: sections 1 to 7 are unchanged in the code; section 8 covers what changed for the game and the 9-tile wall.

**Names.** The plan's Playback Control Task is `playback_task()` (static), started by `playback_control_start()`. The pure core is public so the host tests drive it: `playback_control_init()`, `playback_control_step()`, `playback_control_on_command()`, `playback_control_frame_at()`, `playback_control_state()`, `playback_control_read_timeline()`, `playback_control_rejected()`. State names are those of W6 statechart 4.

## 1. Nine states of W6 statechart 4

- Source: T-B4-04 row; W6 statechart 4.
- Choice: INITIALISING, WAITING FOR READINESS, SYNCHRONISING, READY, ARMED, PLAYING, PAUSED, CLOCK HOLDOVER, FAULT. Inputs are read once per step (WiFi, asset, SYNC_READY, corrected time, beacon age), so a step never sees half-changed inputs.
- Examiner: "Which input moves the tile from SYNCHRONISING to READY, and who owns it?"

## 2. A command acts once per (session_id, seq)

- Source: T-B2-05 "3 copies, one seq"; layout_table.md section 10 item 13 (seq scope).
- Choice: a copy with a seq not newer than the last accepted one is counted in `rejected` and ignored; a wrong session_id is ignored. Host test P1: three identical START_AT give 1 transition; P2: wrong session gives 0.
- Examiner: "The second copy of START_AT arrives 50 ms after the first. What changes?"

## 3. Frame index from the timeline origin

- Source: W6D-1a "frame = floor((t - origin) x FPS) mod frame_count with FPS = 60, t in corrected Pico time".
- Choice: `playback_control_frame_at()` uses 64-bit integers and splits whole seconds from the remainder so nothing overflows; host test P3 checks frame k at origin + k x 10^6 / 60 for k 0 to 1199. The Display Engine counts the same thing in ticks (`display_frame_index()`), which avoids rounding at the grid points.
- Examiner: "Why does the Display Engine not just call your frame_at() each tick?"

## 4. Holdover and safe pause

- Source: W6D-2b holdover 1 s (proposed), VA-4.3; T-B4-04.
- Choice: CLOCK HOLDOVER when the newest beacon is older than 200 ms (two beacon periods, plan choice); still playing on the last clock; after `TIMESYNC_HOLDOVER_US` (1 s) PAUSED with the BEACON_LOSS condition bit, which this module clears when beacons return. Host test P6: 0.9 s keeps playing, 1.1 s pauses, 10 of 10.
- Examiner: "Why 200 ms to enter holdover, and what does the wall look like at 0.5 s and at 1.5 s without beacons?"
- Review round 1 (R1-13): `timesync_newest_age_us()` also returns `INT64_MAX` when three seqlock reads in a row see a write in progress. The task now keeps the last readable age in that case, so one unlucky read no longer enters CLOCK HOLDOVER (or, already in holdover, pauses with BEACON_LOSS). Real silence still reads as a growing age on the next pass. Not host-tested (it is in the FreeRTOS task loop); the decision logic it feeds is covered by P6.

## 5. RESUME_AT shows the saved frame at t_resume

- Source: T-B2-05, OD-14 option (a).
- Choice: on RESUME_AT the origin is moved so the saved frame lands on t_resume; RESUME_AT is also accepted in READY so a rejoining tile can join mid-show. Host test P5.
- Examiner: "A tile rejoins 4 s into a 10 s loop. Which frame does it show first, and when?"

## 6. Timeline seqlock for the Display Engine

- Source: VC-1 pattern (same as timesync); RAR-3.
- Choice: the timeline (running or held, origin, frame_count, hold frame) is published in four atomic words under a sequence counter; the reader on core 1 retries up to 3 times and keeps its last good copy otherwise.
- Examiner: "What can the Display Engine see if it reads while you are writing a new origin?"

## 7. State read by other tasks

- Source: coding standard interpretation 1 (cross-task variables are C11 atomics).
- Choice (30 Oct 2026): `playback_control_state()` returns an atomic copy written with release wherever the state changes, because asset_store (the BUSY gate) and tile_net (the comms statechart) call it from their own tasks.
- Examiner: "asset_store reads READY while you move to ARMED. What is the worst case?"

## 8. What changed for the game and the 9-tile wall (12 to 25 Nov 2026)

- **A new or restarted Pico stops playback** (`playback_control_pico_restart()`, 24 Nov; host test P7). The timeline belongs to the Pico that sent START_AT. After a restart its origin means nothing, and a playing tile answers BUSY to every upload, so the website could not publish the wall again. `tile_net` calls this whenever it sees a new Pico (`tn_game_new_pico()`), together with `timesync_pico_restart()`.
- **Game mode does not use this module.** In game mode the Pico never sends START_AT, and the tile's display draws the game state (`game_tile.c`) instead of the video. The countdown, moles and results are timed by Pico times in each message (MOLE_ON 200 ms ahead, WALL_PICTURE 500 ms ahead), on the same corrected clock.
- **Continue policy on the wall** (25 Nov). A tile pulled for 3 s and for 10 s during playback rejoined in step with its neighbours (by eye, no re-upload) through RESUME_AT in READY (section 5). The wall never stopped. Under the pause policy the Pico restarts after a 5 s loss (a coordinator bug, `coordinator_loop_DEFENCE.md` section 6), so the demo runs `--policy continue`.
- **Video skew measured** (NR-07, 24 Nov). With the drift limit in time sync (`cbe7763`), every pair filmed on tiles 4 to 9 at 240 fps stepped together within 5 ms (medians -0.38 to +0.31 ms). See `timesync_DEFENCE.md`.

## Settled values (25 Nov 2026)

- Holdover entry at 200 ms without a beacon; pause with BEACON_LOSS at 1 s.
- FAULT on an invalid frame_count; a new valid session leaves it.
- START_AT accepted in READY and every playing or paused state; RESUME_AT also in READY (a rejoining tile).
- D-1 for the timeline seqlock and the state copy.

## Questions to practise before the demo

Answer each in your own words, without reading the code.

1. A tile is unplugged for 10 s during a video and plugged back in. Walk through how it lands on the same frame as its neighbours.
2. Why must a restarted Pico stop playback on every tile? What broke on 24 Nov before it did?
3. What does the wall look like at 0.5 s and at 1.5 s without beacons, and which bit shows on the website?
4. Why does the state copy use release and acquire rather than relaxed ordering?
