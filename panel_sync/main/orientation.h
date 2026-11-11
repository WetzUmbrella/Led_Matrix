/**
 * @file    orientation.h
 * @brief   Orientation Task (T-B5-05): reads the QMI8658 accelerometer over
 *          I2C, publishes the debounced orientation (50 Hz) to the Display
 *          Engine through a length-1 mailbox, and detects taps (1 kHz).
 *
 * Owner: Jia Jun (Buddy 5), task T-B5-05. Implemented by Ahsanul (B4) for
 * Jia Jun, written 29 Oct 2026 in Jia Jun's session.
 *
 * Implemented: orientation_start() (priority 5 on core 1, tile_tasks.h, below
 * the Display Engine's 20), a 1 ms esp_timer that wakes the task (the
 * FreeRTOS tick is 100 Hz), qmi_read_accel() over i2c_master on GPIO11 (SDA)
 * and GPIO12 (SCL) at address 0x6A or 0x6B (probed as boardtest.c does),
 * every I2C transaction with a 20 ms timeout, or_classify() through
 * orient_filter on every 20th sample (50 Hz, T-B5-05), and
 * ORIENTATION_CHANGED as xQueueOverwrite() into the mailbox (VA-3.7: the only
 * queue that may overwrite is a length-1 mailbox). On an I2C error the last
 * orientation is kept, the IMU condition bit is set and the task keeps
 * retrying (and re-initialises the IMU once a second while it fails).
 *
 * Tap detection (build step 2, 11 Nov 2026, DS-02): every 1 kHz sample goes
 * through tap_detect (software, see tap_detect.h); an accepted tap is queued
 * for the Network Task, which is woken at once (tile_net_tap_ready()), and a
 * "TAP t_us= peak_mg=" line is printed. The QMI8658 tap engine and its INT
 * pin are no longer used: on the 9 Nov bench they caught about 70% of taps.
 * The range is +/-16 g (slaps reach 15 to 28 g; at +/-8 g a slap and its
 * echo would clip and tie under "highest peak wins").
 *
 * QMI8658 set-up (datasheet register map; the same values as the 9 Nov tap
 * logger, which ran on both tiles): CTRL1 (0x02) = 0x40 (address
 * auto-increment, little-endian), CTRL2 (0x03) = 0x33 (+/-16 g, 1,000 Hz),
 * CTRL7 (0x08) = 0x01 (accelerometer on); WHO_AM_I (0x00) reads 0x05; data
 * AX_L at 0x35.
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
