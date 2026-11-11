/**
 * @file    display_engine.h
 * @brief   Display Engine Task (T-B5-01, T-B5-02, T-B5-03): the 60 fps tick
 *          as a gptimer one-shot re-armed every frame at the deadline from
 *          timesync, and the double-buffered frame path to the WS2812B.
 *
 * Owner: Jia Jun (Buddy 5), tasks T-B5-01 to T-B5-03. T-B5-01 minimal
 * version by Ahsanul (B4) for T-B4-03; T-B5-02 and T-B5-03 implemented by
 * Ahsanul (B4) for Jia Jun, written 29 Oct 2026 in Jia Jun's session.
 *
 * Implemented:
 * - T-B5-01: de_task() on core 1 at priority 20 (tile_tasks.h),
 *   de_tick_isr() (IRAM gptimer alarm callback), de_arm_next(), tick n at
 *   timesync_deadline_local(origin, n) computed from n every frame and armed
 *   as a one-shot, the test pin pulse with the RIG.md tick-identity marker,
 *   the missed-tick counter, one timesync_notify_tick() per tick, and no
 *   first tick before timesync_sync_ready().
 * - T-B5-02: de_prepare_frame() reads frame n of the RAM asset
 *   (asset_store_get()) and unpacks its 192 nibbles straight into the
 *   192 B back buffer (display_frame.c): each LED byte is the nibble, 0 to
 *   8, no scaling or floating point (VC-6). The frame comes from the
 *   timeline Playback Control publishes (playback_control_read_timeline():
 *   running, or held while PAUSED). With no timeline or no asset a status
 *   pattern (values at most 8) is shown. The DE_PROFILE build also copies
 *   8-bit frames (OD-15); the shipped build has no 8-bit path, and
 *   asset_store NACKs format_version 1 (VC-5).
 * - T-B5-03: two 192 B buffers written only here. On tick n: (1) if RMT is
 *   still sending, count a missed deadline and raise MISSED_DEADLINE;
 *   (2) swap front and back in a critical section; (3) start RMT on the
 *   front buffer; (4) prepare n + 1 into the back buffer with the stable
 *   orientation (orientation_current()). The RMT channel and its WS2812B
 *   bytes encoder are created inside de_task on core 1. An RMT error sets
 *   the DISPLAY condition bit (cleared here on the next good start). The
 *   frame-timing log is a tick-to-RMT-start lateness histogram in 100 us
 *   bins plus the worst preparation time, every 600 ticks (VC-8).
 * - Display states (layout_table.md 5.8) reported to telemetry on change:
 *   INITIALISING, IDLE, WAITING FOR NEXT TICK (running), PAUSED, FAULT.
 *   PREPARING FRAME and TRANSMITTING PIXELS are phases inside each tick and
 *   are not reported, to keep telemetry to state changes.
 * The prototype render.c, frames.c, gif_frames.h, the DE_W10_TEST_PATTERN
 * moving pixel and the led_strip dependency are deleted.
 *
 * PROVISIONAL (test pin, owner Jia Jun): DE_TEST_PIN is GPIO4. Confirm it
 * reaches the board header before wiring (bench step B1).
 * PROVISIONAL (marker, owner Jia Jun): pulse, not toggle; rising edge at tick
 * n, high 1 ms, or 4 ms when n mod 64 == 0 (RIG.md section 4b request). The
 * falling edge is a second one-shot alarm.
 * PROVISIONAL (OD-16, owner Jia Jun): RMT without DMA; the refill interrupt
 * is IRAM-safe (CONFIG_RMT_ISR_IRAM_SAFE).
 * PROVISIONAL (owner Jia Jun): the tick grid origin is 0 in Pico time; the
 * playback origin (START_AT) selects the first frame's tick on that grid.
 * Open interface items with Ahsanul (timesync_DEFENCE.md): the reader
 * parameter, timesync_notify_tick() from de_task, and the rounded-up deadline
 * are used as proposed.
 *
 * DEVIATION D-2 (module-wide): ESP-IDF and FreeRTOS names, types and macros.
 */
#ifndef DISPLAY_ENGINE_H
#define DISPLAY_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

/** Frames per second (T-B5-01, W6D-1a). */
#define DE_FPS 60u

/** Spare GPIO driven at every tick (PROVISIONAL, see above). */
#define DE_TEST_PIN 4u

/**
 * @brief  Create the Display Engine Task (priority 20, core 1). The task
 *         creates the gptimer, the test pin and the LED driver on core 1.
 * @return true when the task was created.
 */
bool display_engine_start (void);

/**
 * @brief  The game-mode tap pulse on the test pin (DS-13): high now, low
 *         GAME_TILE_PULSE_TAP_US later (an esp_timer one-shot), only while
 *         the last frame drawn was the game's. Video mode keeps its tick
 *         pulses untouched. Called by the Orientation Task when a tap is
 *         detected; never blocks.
 */
void display_engine_tap_pulse (void);

#endif /* DISPLAY_ENGINE_H */

/*** end of file ***/
