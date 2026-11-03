# playback_control: design defence (T-B4-04)

Owner: Ahsanul (Buddy 4). Written for Ahsanul in Ben's session (29 Oct 2026); the atomic state copy and the header update added in Jia Jun's session (30 Oct 2026). For Ahsanul to review, change where they disagree, and defend. Files: `panel_sync/main/playback_control.c/.h`; host tests `panel_sync/test/test_playback.c` ("Playback: Pass = 6 of 6"). Coding standard: Barr-C:2018, deviations D-1 (timeline seqlock and the state copy) and D-2 (the FreeRTOS wrapper).

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

## PROVISIONAL list for Ahsanul to confirm

- Holdover entry at 200 ms.
- FAULT on an invalid frame_count; a new valid session leaves it.
- START_AT accepted in READY and every playing or paused state; RESUME_AT also in READY.
- D-1 for the timeline seqlock and the state copy.

## Questions for Ahsanul before Tribunal 2

1. Show on the statechart which transitions host tests P1 to P6 exercise and which have no test yet.
2. Explain why the state copy uses release and acquire rather than relaxed ordering.
3. Say what the BEACON_LOSS bit looks like on the dashboard during a 10 s Pico power cut.
