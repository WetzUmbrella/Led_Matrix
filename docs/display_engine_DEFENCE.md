# display_engine: design defence (T-B5-01, T-B5-02, T-B5-03, T-B5-07 test builds)

Owner: Jia Jun (Buddy 5). T-B5-01 minimal version by Ahsanul (B4) for the T-B4-03 rig; the frame path (T-B5-02, T-B5-03) and the test builds written in Jia Jun's session (29 to 30 Oct 2026), for Jia Jun to review, change where needed, and defend. Files: `panel_sync/main/display_engine.c/.h`, `panel_sync/main/display_frame.c/.h`; host tests `panel_sync/test/test_display.c` D1 to D3 ("Display: Pass = 6 of 6" with the orientation tests). Coding standard: Barr-C:2018, deviation D-2.

**Names.** Public: `display_engine_start()`. The plan's `de_task()`, `de_tick_isr()`, `de_arm_next()`, `de_swap()`, `de_start_rmt()`, `de_prepare_frame()` keep their names as statics. The plan's `de_state` is `g_state` (7.1.j). The unpack, remap and frame-index rules are a separate pure module, `display_frame` (`display_frame_unpack()`, `display_frame_source_led()`, `display_frame_index()`), so T-B5-02's host test runs without a tile. The plan's `de_dump_back_buffer()` (T-X-01) is the `DE_DUMP_FRAMES` test build.

## 1. Each tick re-armed from its own deadline

- Source: SE15 (1).txt bullet 2 "Schedule each tick at timeline_origin + n/60 s in corrected Pico time, re-armed every frame"; VA-4.5; T-B5-01.
- Choice: the deadline of tick n is computed from n every frame with `timesync_deadline_local()` (never by adding 16,667 us), armed as a gptimer one-shot; the IRAM ISR raises the test pin and wakes `de_task`. A deadline closer than 200 us (plan choice) counts as missed and the next tick after now is taken.
- Examiner: "Why can two free-running 60 Hz timers be a whole frame apart even when the clocks agree, and how does your design stop that?"

## 2. Transmit the prepared buffer on the tick, then prepare the next

- Source: SE15 (1).txt bullet 3 "Transmit a prepared buffer immediately on the tick, then prepare the next. Pick one back-buffer owner"; W6D-3a, W6D-3b, LDC-7.
- Choice: two 192 B buffers, both written only in `display_engine.c` (the owner). On tick n: if RMT is still sending, count a missed deadline and raise MISSED_DEADLINE; otherwise swap front and back in a critical section, start RMT on the front, then prepare n + 1 into the back. Preparation time never moves the presentation time.
- Examiner: "What does a slow preparation do to the tick that follows it? Show it with the T-B5-07 (b) build."

## 3. RMT driven directly, no DMA (OD-16)

- Source: T-B5-03 "RMT transfer uses DMA or an IRAM-safe ISR per OD-16".
- Choice: an RMT TX channel with a bytes encoder at 10 MHz and the WS2812B timings 0.3/0.9 us, created inside `de_task` on core 1; `rmt_transmit()` returns at once and `rmt_tx_wait_all_done(…, 0)` is the idle check. No DMA: the refill interrupt is IRAM-safe (`CONFIG_RMT_ISR_IRAM_SAFE`). The `led_strip` component is gone because its refresh call blocks until the frame is out. IRAM is unchanged (16,383 of 16,384 B).
- Examiner: "How long does one frame take on the wire, and how much of the 16.67 ms does that leave?" (64 x 24 bits x 1.25 us = 1.92 ms.)

## 4. Unpack straight to LED values, no scaling on the tile

- Source: VC-6 "each LED byte equals the nibble value 0 to 8, with no scaling and no floating point"; OD-05 option (a); CB-5.
- Choice: nibble k of the frame is written unchanged into the wire buffer in G, R, B order. The brightness cap lives only on the PC. `grep -rn -e "dim(" -e BRIGHT panel_sync/main` returns 0 lines. Host test D1: the T-B1-01 `frame_pattern.bin` vector gives 192 of 192 bytes equal to the PC reference and every byte 0 to 8.
- Examiner: "Where would double scaling show up, and which test catches it?" (T-X-01 round trip.)

## 5. Frame index by tick count

- Source: W6D-1a; playback_control timeline.
- Choice: frame k is shown on the k-th tick at or after the timeline origin, modulo frame_count; the first tick comes from `timesync_next_tick(origin - 1)`. Counting ticks avoids the rounding of 10^6 / 60 at the grid points. Host test D3.
- Examiner: "The origin falls between two ticks. Which tick shows frame 0?"

## 6. Rotation remap from the stable orientation

- Source: T-B5-03 "unpack and rotation remap from the 2-bit stable orientation"; IFM-5a.
- Choice: `display_frame_source_led()` maps each physical LED to its source LED for 0, 90, 180, 270 degrees; the orientation is read from the Orientation Task's mailbox once per preparation. Host test D2: four 90 degree turns are the identity, 180 degrees is LED i to 63 - i, every orientation is a permutation. The direction is PROVISIONAL until the T-B5-06 trials.
- Examiner: "The tile is turned 90 degrees clockwise. Which source LED does physical LED 0 show?"

## 7. States reported and the timing log

- Source: W6 statechart 5; VC-8 "a lateness histogram, not per-frame prints".
- Choice: state changes (INITIALISING, IDLE, WAITING FOR NEXT TICK, PAUSED, FAULT) go to TELEMETRY; PREPARING FRAME and TRANSMITTING PIXELS are phases inside every tick and are not reported, to keep telemetry to changes. Every 600 ticks one `DE` line (ticks, missed, rmt_busy, worst preparation time, orientation) and one `DE_HIST` line (tick-to-RMT-start lateness in 100 us bins). An RMT error sets the DISPLAY condition bit, cleared here on the next good start.
- Examiner: "Your log prints once every 10 s. How do you prove no tick was late?"

## 8. Test builds

- Source: T-X-01, T-X-03, T-B5-07.
- Choice: `DE_DUMP_FRAMES` prints each prepared frame once (T-X-01); `DE_PROFILE` counts CPU cycles around preparation (unsigned 32-bit deltas within one frame, correct across one wrap), keeps a histogram and toggles GPIO5 (T-X-03); `DE_TEST_SLOW_PREP` adds 13 ms to every preparation (T-B5-07 (b)); `DE_TEST_FLASH_ERASE` erases a spare sector every second while PLAYING and logs the stall (T-B5-07 (c)). None is in the shipped build.
- Examiner: "Why is a 32-bit cycle delta safe when the counter wraps every 26.8 s?"

## PROVISIONAL list for Jia Jun to confirm

- Test pin GPIO4 (check it is on the header); profiling pin GPIO5.
- Marker: 1 ms pulse, 4 ms every 64th tick.
- OD-16: RMT without DMA.
- LED numbering row by row from the top left, and the rotation direction.
- Status pattern: LED 0 blue at 2 when synced with no asset.
- PREPARING FRAME and TRANSMITTING PIXELS not reported.

## Questions for Jia Jun before Tribunal 2

1. Explain, using the tick-to-RMT-start histogram, what the worst case latency between the tick pulse and the first LED bit is on your bench tile, and what causes it.
2. Explain why only `display_engine.c` may touch the two pixel buffers, and what could go wrong if `asset_store` wrote into the back buffer directly.
3. Say what the wall shows during an upload, during a PAUSE, and before the first frame's tick after START_AT, and point to the code for each.
