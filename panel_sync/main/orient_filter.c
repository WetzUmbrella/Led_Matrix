/**
 * @file    orient_filter.c
 * @brief   Orientation classification and debounce (T-B5-05). See
 *          orient_filter.h.
 *
 * Owner: Jia Jun (Buddy 5). Implemented by Ahsanul (B4) for Jia Jun, task
 * T-B5-05.
 *
 * No ESP-IDF includes: this module is host-testable.
 */
#include "orient_filter.h"

#include "fault_word.h"

/** Orientation codes (layout_table.md section 5.7). */
#define OF_ORIENT_0   0u
#define OF_ORIENT_90  1u
#define OF_ORIENT_180 2u
#define OF_ORIENT_270 3u

static int32_t of_abs (int16_t value);

/**
 * @brief  Stable orientation 0, no candidate.
 * @param  p_filter  The filter.
 */
void orient_filter_init (orient_filter_t * p_filter)
{
    p_filter->stable      = OF_ORIENT_0;
    p_filter->candidate   = ORIENT_FILTER_NONE;
    p_filter->count       = 0u;
    p_filter->b_imu_fault = false;
    p_filter->state       = ORIENT_FILTER_INITIALISING;
}

/**
 * @brief  Quadrant of one sample.
 * @param  accel_x  Raw X.
 * @param  accel_y  Raw Y.
 * @return 0 to 3, or ORIENT_FILTER_NONE.
 */
uint8_t orient_filter_classify (int16_t accel_x, int16_t accel_y)
{
    int32_t const mag_x  = of_abs(accel_x);
    int32_t const mag_y  = of_abs(accel_y);
    uint8_t       result = ORIENT_FILTER_NONE;

    if ((mag_y > mag_x) && (mag_y >= ORIENT_FILTER_MIN_LSB))
    {
        result = (accel_y > 0) ? OF_ORIENT_0 : OF_ORIENT_180;
    }
    else if ((mag_x > mag_y) && (mag_x >= ORIENT_FILTER_MIN_LSB))
    {
        result = (accel_x > 0) ? OF_ORIENT_90 : OF_ORIENT_270;
    }
    else
    {
        /* Weak or diagonal gravity: no candidate. */
    }

    return result;
}

/**
 * @brief  One sample: count equal candidates, publish after 26, keep the
 *         last orientation on an IMU error.
 * @param  p_filter   The filter.
 * @param  b_read_ok  false on an I2C error or timeout.
 * @param  accel_x    Raw X.
 * @param  accel_y    Raw Y.
 * @return true when the stable orientation changed.
 */
bool orient_filter_step (orient_filter_t * p_filter, bool b_read_ok,
                         int16_t accel_x, int16_t accel_y)
{
    bool b_changed = false;

    if (!b_read_ok)
    {
        if (!p_filter->b_imu_fault)
        {
            p_filter->b_imu_fault = true;
            fault_word_set(FAULT_BIT_IMU);
        }

        p_filter->candidate = ORIENT_FILTER_NONE;
        p_filter->count     = 0u;
        p_filter->state     = ORIENT_FILTER_FAULT;
    }
    else
    {
        if (p_filter->b_imu_fault)
        {
            p_filter->b_imu_fault = false;
            fault_word_clear_condition(FAULT_BIT_IMU); /* Owner. */
        }

        uint8_t const sample = orient_filter_classify(accel_x, accel_y);

        if ((ORIENT_FILTER_NONE == sample) || (sample == p_filter->stable))
        {
            p_filter->candidate = ORIENT_FILTER_NONE;
            p_filter->count     = 0u;
            p_filter->state     = (ORIENT_FILTER_NONE == sample)
                                      ? ORIENT_FILTER_MOVEMENT
                                      : ORIENT_FILTER_MONITORING;
        }
        else if (sample != p_filter->candidate)
        {
            p_filter->candidate = sample;
            p_filter->count     = 1u;
            p_filter->state     = ORIENT_FILTER_MOVEMENT;
        }
        else
        {
            p_filter->count++;
            p_filter->state = ORIENT_FILTER_VALIDATING;

            if (p_filter->count >= ORIENT_FILTER_STABLE_SAMPLES)
            {
                p_filter->stable    = sample;
                p_filter->candidate = ORIENT_FILTER_NONE;
                p_filter->count     = 0u;
                p_filter->state     = ORIENT_FILTER_UPDATED;
                b_changed           = true;
            }
        }
    }

    return b_changed;
}

/**
 * @brief  Absolute value without overflow (-32,768 becomes 32,768).
 * @param  value  16-bit reading.
 * @return |value|.
 */
static int32_t of_abs (int16_t value)
{
    int32_t const wide = value; /* Widened, unchanged. */

    return (wide < 0) ? (-wide) : wide;
}

/*** end of file ***/
