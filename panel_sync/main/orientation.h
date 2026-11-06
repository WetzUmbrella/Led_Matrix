/**
 * @file    orientation.h
 * @brief   Orientation Task (T-B5-05): reads the QMI8658 accelerometer over
 *          I2C, publishes the debounced orientation (50 Hz) to the Display
 *          Engine through a length-1 mailbox, and detects taps.
 *
 * Owner: Jia Jun (Buddy 5), task T-B5-05. Implemented by Ahsanul (B4) for
 * Jia Jun, written 29 Oct 2026 in Jia Jun's session.
 *
 * Implemented: orientation_start() (priority 5 on core 1, tile_tasks.h),
 * orientation_task() with vTaskDelayUntil every 10 ms, qmi_read_accel()
 * over i2c_master on GPIO11 (SDA) and GPIO12 (SCL) at address 0x6A or 0x6B
 * (probed as boardtest.c does), every I2C transaction with a 20 ms timeout
 * (one sample period), or_classify() through orient_filter, and
 * ORIENTATION_CHANGED as xQueueOverwrite() into the mailbox (VA-3.7: the
 * only queue that may overwrite is a length-1 mailbox). On an I2C error the
 * last orientation is kept, the IMU condition bit is set and the task keeps
 * retrying (and re-initialises the IMU once a second while it fails).
 *
 * Tap detection (re-plan build step 2): the task now samples X, Y and Z at
 * 100 Hz for tap_detect and passes every second sample to orient_filter, so
 * orientation stays at 50 Hz. The IMU tap engine raises INT1 or INT2, picked
 * by the one constant OR_TAP_INT_LINE in orientation.c; the interrupt
 * records the time and the task does the I2C. +/-8 g replaces +/-2 g because
 * a tap would clip at 2 g and the peak could not rank tiles. Each interrupt
 * prints a "TAP result=" line, the data for tuning thresholds on the board.
 *
 * PROVISIONAL (owner Jia Jun, check on the board): QMI8658 set-up from its
 * datasheet register map: CTRL1 (0x02) = 0x48 with INT1 or 0x50 with INT2
 * (address auto-increment, little-endian, that INT on), CTRL2 (0x03) = 0x24
 * (+/-8 g, 500 Hz), CTRL7 (0x08) = 0x01 (accelerometer on), CTRL8 (0x09) =
 * 0xC1 with INT1 or 0x81 with INT2 (CTRL9 done in STATUSINT, motion events
 * on that INT, tap on); CTRL9 (0x0A) command 0x0C; WHO_AM_I (0x00) reads
 * 0x05; data AX_L at 0x35; STATUS1 (0x2F) bit 1; TAP_STATUS at 0x59. The tap
 * values were written from memory of the datasheet, with no copy to hand:
 * check them against it before tuning. The full list with meanings is in
 * docs/plan/overnight_log/SUMMARY_laptop.md.
 *
 * PROVISIONAL (owner Jia Jun, check on the schematic): INT1 on GPIO10 (the
 * default) and INT2 on GPIO13 are unconfirmed. If the chosen line is not
 * wired there, no TAP lines print.
 *
 * DEVIATION D-2 (module-wide): ESP-IDF and FreeRTOS names and types.
 */
#ifndef ORIENTATION_H
#define ORIENTATION_H

#include <stdbool.h>
#include <stdint.h>

#include "tap_detect.h"

/**
 * @brief  Create the mailbox and the Orientation Task. Call once from
 *         app_main.
 * @return true when both were created.
 */
bool orientation_start (void);

/**
 * @brief  The stable orientation (any task; peeks the mailbox, never
 *         blocks).
 * @return 0 to 3 (0, 90, 180, 270 degrees); 0 before the first change.
 */
uint8_t orientation_current (void);

/**
 * @brief  Take the oldest accepted tap (any one consumer task; never blocks).
 *         Up to four taps are held; the oldest is dropped when a fifth comes.
 *         t_us is local time, not yet the Pico's corrected clock.
 * @param  p_event  Receives the tap.
 * @return true when a tap was taken.
 */
bool orientation_tap_take (tap_detect_event_t * p_event);

#endif /* ORIENTATION_H */

/*** end of file ***/
