# display_engine: design defence (T-B5-01, T-B5-02, T-B5-03, T-B5-07 test builds)

Subsystem: tile LED display (ESP32-S3 tile). Files: `panel_sync/main/display_engine.c/.h`, `panel_sync/main/display_frame.c/.h`; host tests `panel_sync/test/test_display.c` D1 to D6 ("Display: Pass = 9 of 9" with the orientation tests). Coding standard: Barr-C:2018, deviation D-2. Updated 25 Nov 2026 against `a6d4902`: section 3 is corrected (RMT now uses DMA, since 12 Nov), and section 9 covers the game and the bench fixes. Host tests: "Display: Pass = 9 of 9" on 25 Nov.

**Names.** Public: `display_engine_start()`. The plan's `de_task()`, `de_tick_isr()`, `de_arm_next()`, `de_swap()`, `de_start_rmt()`, `de_prepare_frame()` keep their names as statics. The plan's `de_state` is `g_state` (7.1.j). The unpack, remap and frame-index rules are a separate pure module, `display_frame` (`display_frame_unpack()`, `display_frame_source_led()`, `display_frame_index()`), so T-B5-02's host test runs without a tile. The plan's `de_dump_back_buffer()` (T-X-01) is the `DE_DUMP_FRAMES` test build.

## 1. Each tick re-armed from its own deadline

- Source: SE15 (1).txt bullet 2 "Schedule each tick at timeline_origin + n/60 s in corrected Pico time, re-armed every frame"; VA-4.5; T-B5-01.
- Choice: the deadline of tick n is computed from n every frame with `timesync_deadline_local()` (never by adding 16,667 us), armed as a gptimer one-shot; the IRAM ISR raises the test pin and wakes `de_task`. A deadline closer than 200 us (plan choice) counts as missed and the next tick after now is taken.
- Examiner: "Why can two free-running 60 Hz timers be a whole frame apart even when the clocks agree, and how does your design stop that?"

## 2. Transmit the prepared buffer on the tick, then prepare the next

- Source: SE15 (1).txt bullet 3 "Transmit a prepared buffer immediately on the tick, then prepare the next. Pick one back-buffer owner"; W6D-3a, W6D-3b, LDC-7.
- Choice: two 192 B buffers, both written only in `display_engine.c` (the owner). On tick n: if RMT is still sending, count a missed deadline and raise MISSED_DEADLINE; otherwise swap front and back in a critical section, start RMT on the front, then prepare n + 1 into the back. Preparation time never moves the presentation time.
- Examiner: "What does a slow preparation do to the tick that follows it? Show it with the T-B5-07 (b) build."

## 3. RMT with DMA, one refill per frame (OD-16, changed 12 Nov 2026)

- Source: T-B5-03 "RMT transfer uses DMA or an IRAM-safe ISR per OD-16"; bench 12 Nov 2026, step 3 rounds 3 to 5.
- Choice: an RMT TX channel with a bytes encoder at 10 MHz and the WS2812B timings 0.3/0.9 us, created inside `de_task` on core 1. `rmt_transmit()` returns at once, and `rmt_tx_wait_all_done(..., 0)` is the idle check. The `led_strip` component is gone because its refresh call blocks until the frame is out.
- **DMA since 12 Nov.** The October build had no DMA: the channel held 64 symbols and needed a refill every 32 symbols (about 38 us). In game mode the WiFi traffic delayed those refills, and the operator saw random pixels and wrong colours for an instant (round 3). The first DMA attempt asked for 4,096 symbols, which ESP-IDF 5.3 refuses ("mem_block_symbols can't exceed 2046"), so no channel was made and the LEDs froze (round 4). Now `DE_RMT_SYMBOLS` = 2,046, the driver's maximum, checked at compile time. A frame (192 B, 1,536 symbols) needs one refill, due about 1.3 ms in. Round 5: no flicker, tick-to-RMT lateness unchanged (about 99% under 100 us), 1 missed tick in about 23,000.
- Examiner: "How long does one frame take on the wire, and how much of the 16.67 ms does that leave?" (64 x 24 bits x 1.25 us = 1.92 ms.)

## 4. Unpack straight to LED values, no scaling on the tile

- Source: VC-6 "each LED byte equals the nibble value 0 to 8, with no scaling and no floating point"; OD-05 option (a); CB-5.
- Choice: nibble k of the frame is written unchanged into the wire buffer in R, G, B order (the Waveshare panel is RGB; bench 5 Nov 2026). The brightness cap lives only on the PC. `grep -rn -e "dim(" -e BRIGHT panel_sync/main` returns 0 lines. Host test D1: the T-B1-01 `frame_pattern.bin` vector gives 192 of 192 bytes equal to the PC reference and every byte 0 to 8.
- Examiner: "Where would double scaling show up, and which test catches it?" (T-X-01 round trip.)

## 5. Frame index by tick count

- Source: W6D-1a; playback_control timeline.
- Choice: frame k is shown on the k-th tick at or after the timeline origin, modulo frame_count; the first tick comes from `timesync_next_tick(origin - 1)`. Counting ticks avoids the rounding of 10^6 / 60 at the grid points. Host test D3.
- Examiner: "The origin falls between two ticks. Which tick shows frame 0?"

## 6. Rotation remap from the stable orientation

- Source: T-B5-03 "unpack and rotation remap from the 2-bit stable orientation"; IFM-5a.
- Choice: `display_frame_source_led()` maps each physical LED to its source LED for 0, 90, 180, 270 degrees; the orientation is read from the Orientation Task's mailbox once per preparation. Host test D2: four 90 degree turns are the identity, 180 degrees is LED i to 63 - i, every orientation is a permutation. D2 passes for either direction, so host tests D4 to D6 pin it: D4 and D6 against hand-worked LED tables, D5 from gravity through `orient_filter_classify()` to the picture for all 64 LEDs. Code 90 is a tile turned 90 degrees clockwise: the 5 Nov bench showed the first mapping (X right, Y up) turned a 90 degree tile upside down, and on 9 Nov the code rose with clockwise turns. D4 to D6 pin the corrected remap; the board's IMU sits a quarter turn from the LED grid, so `DISPLAY_FRAME_MOUNT_TURNS` (3, measured 5 Nov) is added before the remap. The whole chain is to be confirmed at all four angles on a tile (card F4, T-B5-06).
- Examiner: "The tile is turned 90 degrees clockwise. Which source LED does physical LED 0 show?"

## 7. States reported and the timing log

- Source: W6 statechart 5; VC-8 "a lateness histogram, not per-frame prints".
- Choice: state changes (INITIALISING, IDLE, WAITING FOR NEXT TICK, PAUSED, FAULT) go to TELEMETRY; PREPARING FRAME and TRANSMITTING PIXELS are phases inside every tick and are not reported, to keep telemetry to changes. Every 600 ticks one `DE` line (ticks, missed, rmt_busy, worst preparation time, orientation) and one `DE_HIST` line (tick-to-RMT-start lateness in 100 us bins). An RMT error sets the DISPLAY condition bit, cleared here on the next good start.
- Examiner: "Your log prints once every 10 s. How do you prove no tick was late?"

## 8. Test builds

- Source: T-X-01, T-X-03, T-B5-07.
- Choice: `DE_DUMP_FRAMES` prints each prepared frame once (T-X-01); `DE_PROFILE` counts CPU cycles around preparation (unsigned 32-bit deltas within one frame, correct across one wrap), keeps a histogram and toggles GPIO5 (T-X-03); `DE_TEST_SLOW_PREP` adds 13 ms to every preparation (T-B5-07 (b)); `DE_TEST_FLASH_ERASE` erases a spare sector every second while PLAYING and logs the stall (T-B5-07 (c)). None is in the shipped build.
- Examiner: "Why is a 32-bit cycle delta safe when the counter wraps every 26.8 s?"

## 9. What the game and the bench added (11 to 24 Nov 2026)

- **The game draws through the same tick.** On every tick the engine reads the game state through its seqlock (`game_tile_read()`). When the game owns the tile and no video is running, `game_tile_fill_wire()` draws the frame for that tick's Pico time instead of a video frame. The game uses the same buffers, the same transmit-then-prepare order and the same rotation (`display_frame_mounted()`). See `game_tile_DEFENCE.md` section 6.
- **Clock-jump guard** (`DE_MAX_LEAD_US` = 100 ms, bench 12 Nov 2026). When the Pico restarts, time sync re-seeds and the corrected clock steps back. The next deadline is then far in the future, and the gptimer waited out the Pico's old uptime with the LEDs frozen. Now a lead over 100 ms (six ticks) is treated as a clock jump: the tick is dropped, the next tick after now is shown, and `jumps=` counts it, with no fault raised. A lead under 200 us is still a missed tick and raises MISSED_DEADLINE.
- **Game-mode GPIO4 pulses** (DS-13): 10 ms in a mole's first frame, 6 ms in a hit burst's first frame, and 2 ms on a detected tap (`display_engine_tap_pulse()`, called by the Orientation Task, dropped by a one-shot esp_timer). Each event has its own width so one pin carries all three. They were never captured on the bench: NR-03 was measured from the Pico's log and a 240 fps video.
- **Mount offset and rotation direction** (5 and 9 Nov). The IMU sits a quarter turn from the LED grid, so `DISPLAY_FRAME_MOUNT_TURNS` = 3 is added before the remap. Orientation codes rise with clockwise turns. Host tests D4 to D6 pin both, and whole-wall rotation (NR-08: 10 of 10) relies on them.

## Settled values (25 Nov 2026)

- Test pin GPIO4, profiling pin GPIO5 (test builds only).
- Marker: 1 ms pulse, 4 ms every 64th tick (video); event pulses 10, 6 and 2 ms (game).
- OD-16: RMT with DMA, 2,046 symbols.
- LED numbering row by row from the top left; codes rise with clockwise turns; mount offset 3 quarter turns.
- A deadline under 200 us away is a missed tick; one over 100 ms away is a clock jump.

## Questions to practise before the demo

Answer each in your own words, without reading the code.

1. Why can two free-running 60 Hz timers be a whole frame apart even when the clocks agree, and how does re-arming from n stop that?
2. What caused the game-mode flicker on 12 Nov, why did the first DMA fix freeze the LEDs, and what proved the final fix?
3. The Pico restarts during a video. Why did the LEDs freeze before 12 Nov, and what does the engine do now?
4. Why may only `display_engine.c` touch the two pixel buffers?
