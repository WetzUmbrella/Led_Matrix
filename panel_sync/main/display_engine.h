/**
 * @file    display_engine.h
 * @brief   Display Engine Task (T-B5-01): the 60 fps tick as a gptimer
 *          one-shot re-armed every frame at the deadline from timesync.
 *
 * Minimal implementation by Buddy 4 for T-B4-03. Owner: Jia Jun, task
 * T-B5-01. Implemented: display_engine_start(), de_task() on core 1 at
 * priority 20 (tile_tasks.h), de_tick_isr() (IRAM gptimer alarm callback),
 * de_arm_next(), tick n at timesync_deadline_local(origin, n) computed from
 * n every frame and armed as a one-shot, the test pin pulse with the RIG.md
 * tick-identity marker, the missed-tick counter, one timesync_notify_tick()
 * per tick from de_task, and the DE_W10_TEST_PATTERN moving pixel.
 * Not yet implemented: frame buffers, swap, RMT start and preparation
 * (T-B5-02, T-B5-03), orientation, asset playback, the frame-timing
 * histogram (T-B5-07). The prototype render.c/.h is deleted (T-B5-01).
 *
 * PROVISIONAL (test pin, owner Jia Jun): DE_TEST_PIN is GPIO4. The repo does
 * not record which spare GPIO reaches the board header; confirm it before
 * wiring (bench step B1).
 * PROVISIONAL (marker, owner Jia Jun): pulse, not toggle; rising edge at tick
 * n, high 1 ms, or 4 ms when n mod 64 == 0 (RIG.md section 4b request). The
 * falling edge is a second one-shot alarm.
 * PROVISIONAL (origin, T-B4-03 rig build): origin = 0 in Pico time until
 * START_AT exists (T-B4-04).
 * Open interface items with Ahsanul (timesync_DEFENCE.md): the reader
 * parameter, timesync_notify_tick() from de_task, and the rounded-up deadline
 * are used as proposed.
 *
 * DEVIATION D-2 (module-wide): ESP-IDF, FreeRTOS and led_strip names, types
 * and macros.
 */
#ifndef DISPLAY_ENGINE_H
#define DISPLAY_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

#ifndef DE_W10_TEST_PATTERN
/** Week 10 test build: each tick lights only LED (n mod 64) at value 8 on
 *  all three channels. Deleted in T-B5-02. */
#define DE_W10_TEST_PATTERN 1
#endif

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
 * @brief  Ticks shown and ticks missed since start, for logs.
 * @param  p_ticks   Receives the number of ticks with a pulse.
 * @param  p_missed  Receives the number of missed ticks (deadline already
 *                   passed when it was to be armed).
 */
void display_engine_get_counts (uint32_t * p_ticks, uint32_t * p_missed);

#endif /* DISPLAY_ENGINE_H */

/*** end of file ***/
