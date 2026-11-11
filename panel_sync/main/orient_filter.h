/**
 * @file    orient_filter.h
 * @brief   Orientation classification and debounce (T-B5-05): quadrant from
 *          gravity in X and Y, published only after 26 equal samples, last
 *          orientation kept on an IMU error. Pure C (fault_word only), so the
 *          T-B5-05 host tests run on a PC.
 *
 * Owner: Jia Jun (Buddy 5), task T-B5-05. Implemented by Ahsanul (B4) for
 * Jia Jun, written 29 Oct 2026 in Jia Jun's session.
 *
 * - Accelerometer only (VA-5.4): an upright tile within 20 degrees of
 *   vertical (proposed, stated in the README) has gravity mostly in X and
 *   Y, so the quadrant needs no gyro integration (SE15 (1).txt bullet 9).
 * - Quadrant (PROVISIONAL, owner Jia Jun, confirm in T-B5-06): +Y dominant
 *   = 0 degrees, +X = 90, -Y = 180, -X = 270. The dominant axis must carry
 *   at least half of 1 g, otherwise there is no candidate (tile flat or
 *   moving) and the count restarts.
 * - Debounce (IFM-6b): a candidate different from the stable orientation
 *   is published after ORIENT_FILTER_STABLE_SAMPLES equal samples in a row
 *   (26 = 0.5 s at 50 Hz, plus the first).
 * - IMU error (SRM-6E): the stable orientation is kept, the IMU condition
 *   bit is set once, and it is cleared by this module (its owner) on the
 *   next good sample.
 */
#ifndef ORIENT_FILTER_H
#define ORIENT_FILTER_H

#include <stdbool.h>
#include <stdint.h>

/** Equal samples needed to publish a new orientation (T-B5-05 row). */
#define ORIENT_FILTER_STABLE_SAMPLES 26u

/** Least dominant-axis reading for a candidate: 0.5 g at 2,048 LSB/g
 *  (QMI8658 at +/-16 g, the range tap detection needs from 11 Nov 2026,
 *  tap_detect.h), plan choice. */
#define ORIENT_FILTER_MIN_LSB 1024

/** No candidate (weak or ambiguous gravity in X and Y). */
#define ORIENT_FILTER_NONE 4u

/** Orientation Task states (W6 statechart, T-B5-05 row). */
typedef enum
{
    ORIENT_FILTER_INITIALISING = 0, /**< INITIALISING IMU. */
    ORIENT_FILTER_MONITORING,       /**< MONITORING ORIENTATION. */
    ORIENT_FILTER_MOVEMENT,         /**< MOVEMENT DETECTED. */
    ORIENT_FILTER_VALIDATING,       /**< VALIDATING ORIENTATION. */
    ORIENT_FILTER_UPDATED,          /**< ORIENTATION UPDATED. */
    ORIENT_FILTER_FAULT             /**< ORIENTATION FAULT. */
} orient_filter_state_t;

/** Filter state, owned by the Orientation Task. */
typedef struct
{
    uint8_t  stable;      /**< Published orientation, 0 to 3. */
    uint8_t  candidate;   /**< Orientation being counted, or NONE. */
    uint32_t count;       /**< Equal candidate samples in a row. */
    bool     b_imu_fault; /**< IMU condition bit set by this filter. */
    uint32_t state;       /**< orient_filter_state_t. */
} orient_filter_t;

/**
 * @brief  Stable orientation 0, no candidate, INITIALISING IMU.
 * @param  p_filter  The filter.
 */
void orient_filter_init (orient_filter_t * p_filter);

/**
 * @brief  Quadrant of one accelerometer sample.
 * @param  accel_x  Raw X, 16-bit.
 * @param  accel_y  Raw Y, 16-bit.
 * @return 0 to 3, or ORIENT_FILTER_NONE.
 */
uint8_t orient_filter_classify (int16_t accel_x, int16_t accel_y);

/**
 * @brief  One 50 Hz sample.
 * @param  p_filter  The filter.
 * @param  b_read_ok false when the I2C read failed or timed out.
 * @param  accel_x   Raw X (ignored when b_read_ok is false).
 * @param  accel_y   Raw Y (ignored when b_read_ok is false).
 * @return true when the stable orientation changed (ORIENTATION_CHANGED).
 */
bool orient_filter_step (orient_filter_t * p_filter, bool b_read_ok,
                         int16_t accel_x, int16_t accel_y);

#endif /* ORIENT_FILTER_H */

/*** end of file ***/
