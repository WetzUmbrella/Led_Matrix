/**
 * @file    orientation.h
 * @brief   Orientation Task (T-B5-05): reads the QMI8658 accelerometer at
 *          50 Hz over I2C and publishes the debounced orientation to the
 *          Display Engine through a length-1 mailbox.
 *
 * Owner: Jia Jun (Buddy 5), task T-B5-05. Implemented by Ahsanul (B4) for
 * Jia Jun, written 29 Oct 2026 in Jia Jun's session.
 *
 * Implemented: orientation_start() (priority 5 on core 1, tile_tasks.h),
 * orientation_task() with vTaskDelayUntil every 20 ms, qmi_read_accel()
 * over i2c_master on GPIO11 (SDA) and GPIO12 (SCL) at address 0x6A or 0x6B
 * (probed as boardtest.c does), every I2C transaction with a 20 ms timeout
 * (one sample period), or_classify() through orient_filter, and
 * ORIENTATION_CHANGED as xQueueOverwrite() into the mailbox (VA-3.7: the
 * only queue that may overwrite is a length-1 mailbox). On an I2C error the
 * last orientation is kept, the IMU condition bit is set and the task keeps
 * retrying (and re-initialises the IMU once a second while it fails).
 *
 * PROVISIONAL (owner Jia Jun, check on the board): QMI8658 set-up from its
 * datasheet register map: CTRL1 (0x02) = 0x40 (address auto-increment,
 * little-endian), CTRL2 (0x03) = 0x06 (+/-2 g, about 125 Hz), CTRL7 (0x08)
 * = 0x01 (accelerometer on); WHO_AM_I (0x00) reads 0x05; data AX_L at 0x35.
 *
 * DEVIATION D-2 (module-wide): ESP-IDF and FreeRTOS names and types.
 */
#ifndef ORIENTATION_H
#define ORIENTATION_H

#include <stdbool.h>
#include <stdint.h>

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

#endif /* ORIENTATION_H */

/*** end of file ***/
