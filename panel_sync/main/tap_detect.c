/**
 * @file    tap_detect.c
 * @brief   Software tap detection (see tap_detect.h for the rules).
 *
 * Owner: Jia Jun (Buddy 5). Rewritten 11 Nov 2026 (build step 2, DS-02).
 *
 * No ESP-IDF includes: this module is host-testable. Integer arithmetic
 * only; the baseline is kept in LSB x 65,536 so the slow average does not
 * lose its fraction.
 */
#include "tap_detect.h"

#include <stddef.h>
#include <stdint.h>

/** Axes in a sample. */
#define TD_AXES 3u

/** The baseline's fixed-point scale: 16 fraction bits. */
#define TD_Q_SHIFT 16

/** mg per g. */
#define TD_MG_PER_G 1000

static uint32_t td_deviation_mg (tap_detect_t const * p_detect,
                                 int16_t const * p_raw);
static bool     td_baseline (tap_detect_t * p_detect, int16_t const * p_raw,
                             uint32_t dev_mg);
static bool     td_phase (tap_detect_t * p_detect, int64_t t_us,
                          uint32_t dev_mg, tap_detect_event_t * p_event);
static uint32_t td_isqrt (uint64_t value);
static int64_t  td_q (int16_t raw);

/**
 * @brief  Reset the detector: no baseline, armed, default threshold.
 * @param  p_detect  The detector.
 */
void tap_detect_init (tap_detect_t * p_detect)
{
    for (uint32_t axis = 0u; axis < TD_AXES; axis++)
    {
        p_detect->base_q[axis] = 0;
    }

    p_detect->b_seeded     = false;
    p_detect->held         = 0u;
    p_detect->threshold_mg = TAP_DETECT_THRESHOLD_MG;
    p_detect->phase        = TAP_DETECT_ARMED;
    p_detect->t_first_us   = 0;
    p_detect->peak_mg      = 0u;
    p_detect->b_quiet      = false;
    p_detect->t_quiet_us   = 0;
    p_detect->last_mg      = 0u;
    p_detect->count        = 0u;
}

/**
 * @brief  Set the threshold, clamped to its limits.
 * @param  p_detect      The detector.
 * @param  threshold_mg  Threshold, mg.
 */
void tap_detect_set_threshold (tap_detect_t * p_detect, uint32_t threshold_mg)
{
    uint32_t value = threshold_mg;

    if (value < TAP_DETECT_THRESHOLD_MIN_MG)
    {
        value = TAP_DETECT_THRESHOLD_MIN_MG;
    }
    else if (value > TAP_DETECT_THRESHOLD_MAX_MG)
    {
        value = TAP_DETECT_THRESHOLD_MAX_MG;
    }
    else
    {
        /* Inside the limits: unchanged. */
    }

    p_detect->threshold_mg = value;
}

/**
 * @brief  One accelerometer sample.
 * @param  p_detect  The detector.
 * @param  t_us      Sample time, microseconds.
 * @param  accel_x   Raw X.
 * @param  accel_y   Raw Y.
 * @param  accel_z   Raw Z.
 * @param  p_event   Receives the tap when the return is true.
 * @return true when a tap's peak window ended with this sample.
 */
bool tap_detect_sample (tap_detect_t * p_detect, int64_t t_us, int16_t accel_x,
                        int16_t accel_y, int16_t accel_z,
                        tap_detect_event_t * p_event)
{
    int16_t const raw[TD_AXES] = {accel_x, accel_y, accel_z};
    uint32_t      dev_mg       = 0u;
    bool          b_tap        = false;

    if (!p_detect->b_seeded)
    {
        /* The first sample is the baseline: deviation 0. */
        for (uint32_t axis = 0u; axis < TD_AXES; axis++)
        {
            p_detect->base_q[axis] = td_q(raw[axis]);
        }

        p_detect->b_seeded = true;
    }
    else
    {
        dev_mg = td_deviation_mg(p_detect, raw);

        /* A reseed makes this sample the baseline: deviation 0. */
        if (td_baseline(p_detect, raw, dev_mg))
        {
            dev_mg = 0u;
        }

        b_tap = td_phase(p_detect, t_us, dev_mg, p_event);
    }

    p_detect->last_mg = dev_mg;

    return b_tap;
}

/**
 * @brief  Deviation of the last sample, mg.
 * @param  p_detect  The detector.
 * @return mg.
 */
uint32_t tap_detect_last_mg (tap_detect_t const * p_detect)
{
    return p_detect->last_mg;
}

/**
 * @brief  |sample - baseline| over the three axes, mg.
 * @param  p_detect  The detector (seeded).
 * @param  p_raw     X, Y and Z.
 * @return mg.
 */
static uint32_t td_deviation_mg (tap_detect_t const * p_detect,
                                 int16_t const * p_raw)
{
    uint64_t sum_sq = 0u;

    for (uint32_t axis = 0u; axis < TD_AXES; axis++)
    {
        /* Q16 difference back to LSB; |diff| is below 2^17, so its square
         * fits. */
        int64_t const diff = (td_q(p_raw[axis]) - p_detect->base_q[axis]) /
                             (INT64_C(1) << TD_Q_SHIFT);

        sum_sq += (uint64_t)(diff * diff); /* A square: non-negative. */
    }

    /* At most sqrt(3) x 65,536 LSB, so the mg value fits uint32_t. */
    return (td_isqrt(sum_sq) * (uint32_t)TD_MG_PER_G) /
           (uint32_t)TAP_DETECT_LSB_PER_G;
}

/**
 * @brief  Update the baseline: held during a tap or a loud sample, reseeded
 *         after TAP_DETECT_HOLD_MAX_SAMPLES held samples, otherwise a slow
 *         average.
 * @param  p_detect  The detector.
 * @param  p_raw     X, Y and Z.
 * @param  dev_mg    This sample's deviation.
 * @return true when the baseline was reseeded from this sample.
 */
static bool td_baseline (tap_detect_t * p_detect, int16_t const * p_raw,
                         uint32_t dev_mg)
{
    bool const b_hold = (TAP_DETECT_ARMED != p_detect->phase) ||
                        (dev_mg > TAP_DETECT_HOLD_MG);
    bool       b_reseed = false;

    if (b_hold)
    {
        p_detect->held++;

        if (p_detect->held >= TAP_DETECT_HOLD_MAX_SAMPLES)
        {
            for (uint32_t axis = 0u; axis < TD_AXES; axis++)
            {
                p_detect->base_q[axis] = td_q(p_raw[axis]);
            }

            p_detect->held = 0u;
            b_reseed       = true;
        }
    }
    else
    {
        for (uint32_t axis = 0u; axis < TD_AXES; axis++)
        {
            int64_t const x_q = td_q(p_raw[axis]);

            p_detect->base_q[axis] +=
                (x_q - p_detect->base_q[axis]) / TAP_DETECT_EMA_SAMPLES;
        }

        p_detect->held = 0u; /* Not held: a later reseed counts from here. */
    }

    return b_reseed;
}

/**
 * @brief  Advance the phase with one sample's deviation.
 * @param  p_detect  The detector.
 * @param  t_us      Sample time.
 * @param  dev_mg    Deviation, mg.
 * @param  p_event   Receives the tap when the return is true.
 * @return true when a tap's peak window ended.
 */
static bool td_phase (tap_detect_t * p_detect, int64_t t_us, uint32_t dev_mg,
                      tap_detect_event_t * p_event)
{
    bool b_tap = false;

    if (TAP_DETECT_ARMED == p_detect->phase)
    {
        if (dev_mg >= p_detect->threshold_mg)
        {
            p_detect->phase      = TAP_DETECT_PEAK;
            p_detect->t_first_us = t_us;
            p_detect->peak_mg    = dev_mg;
        }
    }
    else if (TAP_DETECT_PEAK == p_detect->phase)
    {
        if (dev_mg > p_detect->peak_mg)
        {
            p_detect->peak_mg = dev_mg;
        }

        if ((t_us - p_detect->t_first_us) >= TAP_DETECT_PEAK_WINDOW_US)
        {
            p_detect->count++;
            p_event->t_us  = p_detect->t_first_us;
            p_event->count = p_detect->count;
            /* The TAP peak field is 16 bits: clamp (65 g is never reached). */
            p_event->peak  = (p_detect->peak_mg > UINT16_MAX)
                                 ? UINT16_MAX
                                 : (uint16_t)p_detect->peak_mg;
            p_detect->phase   = TAP_DETECT_SETTLING;
            p_detect->b_quiet = false;
            b_tap             = true;
        }
    }
    else
    {
        bool const b_low = (dev_mg < (p_detect->threshold_mg / 2u));

        if (!b_low)
        {
            p_detect->b_quiet = false;
        }
        else if (!p_detect->b_quiet)
        {
            p_detect->b_quiet    = true;
            p_detect->t_quiet_us = t_us;
        }
        else
        {
            /* The quiet run continues. */
        }

        if (((p_detect->b_quiet) &&
             ((t_us - p_detect->t_quiet_us) >= TAP_DETECT_QUIET_US)) ||
            ((t_us - p_detect->t_first_us) >= TAP_DETECT_REARM_MAX_US))
        {
            p_detect->phase = TAP_DETECT_ARMED;
        }
    }

    return b_tap;
}

/**
 * @brief  Integer square root, rounded down.
 * @param  value  0 to 2^63.
 * @return floor(sqrt(value)).
 */
static uint32_t td_isqrt (uint64_t value)
{
    uint64_t root = 0u;
    uint64_t bit  = UINT64_C(1) << 62;

    while (bit > value)
    {
        bit >>= 2;
    }

    while (0u != bit)
    {
        if (value >= (root + bit))
        {
            value -= root + bit;
            root = (root >> 1) + bit;
        }
        else
        {
            root >>= 1;
        }

        bit >>= 2;
    }

    return (uint32_t)root; /* sqrt of a value below 2^64 fits 32 bits. */
}

/**
 * @brief  A raw reading in the baseline's fixed point (LSB x 65,536).
 * @param  raw  Raw reading.
 * @return raw x 65,536.
 */
static int64_t td_q (int16_t raw)
{
    return (int64_t)raw * (INT64_C(1) << TD_Q_SHIFT); /* Widened first. */
}

/*** end of file ***/
