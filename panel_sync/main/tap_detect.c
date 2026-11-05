/**
 * @file    tap_detect.c
 * @brief   QMI8658 tap detection: turns the tap interrupt into a validated
 *          event with a peak size. Pure C, host-testable.
 *
 * Owner: Jia Jun (Buddy 5). Implemented by Ahsanul (B4) for Jia Jun.
 *
 * No ESP-IDF includes: this module is host-testable.
 */
#include "tap_detect.h"

#include <stdint.h>

/** 16x the slow running average (shift constant for the baseline). */
#define TD_BASE_SCALE 16

/** Tap status: bits 1..0 hold the tap count. */
#define TAP_STATUS_COUNT_MASK 0x03u
/** Tap status: bits 5..4 hold the axis. */
#define TAP_STATUS_AXIS_MASK 0x03u
/** Tap status: bit 7 holds the polarity (0 positive, 1 negative). */
#define TAP_STATUS_POLARITY_MASK 0x01u
/** Tap status: axis field shift. */
#define TAP_STATUS_AXIS_SHIFT 4u
/** Tap status: polarity field shift. */
#define TAP_STATUS_POLARITY_SHIFT 7u

/* --- Page 1 (CAL4_H = 1): peak window, priority, tap window, double-tap --- */

/** Page 1 CAL1_L: peak window, 10 samples = 20 ms at 500 Hz. PROVISIONAL. */
#define TAP_P1_CAL1_L 10u
/** Page 1 CAL1_H: priority 0. PROVISIONAL. */
#define TAP_P1_CAL1_H 0u
/** Page 1 CAL2_L: tap window, 15 samples. PROVISIONAL. */
#define TAP_P1_CAL2_L 15u
/** Page 1 CAL2_H: tap window high byte, 0. PROVISIONAL. */
#define TAP_P1_CAL2_H 0u
/** Page 1 CAL3_L: double-tap window, 10 samples. PROVISIONAL. */
#define TAP_P1_CAL3_L 10u
/** Page 1 CAL3_H: double-tap window high byte, 0. PROVISIONAL. */
#define TAP_P1_CAL3_H 0u
/** Page 1 CAL4_L: 0. PROVISIONAL. */
#define TAP_P1_CAL4_L 0u
/** Page 1 CAL4_H: page select 1. PROVISIONAL. */
#define TAP_P1_CAL4_H 1u

/* --- Page 2 (CAL4_H = 2): alpha, gamma, thresholds --- */

/** Page 2 CAL1_L: alpha 8 = 0.0625 in u0.7. PROVISIONAL. */
#define TAP_P2_CAL1_L 8u
/** Page 2 CAL1_H: gamma 32 = 0.25 in u0.7. PROVISIONAL. */
#define TAP_P2_CAL1_H 32u
/** Page 2 CAL2_L: peak magnitude threshold low byte (1638 = 0x0666).
 *  PROVISIONAL. */
#define TAP_P2_CAL2_L 0x66u
/** Page 2 CAL2_H: peak magnitude threshold high byte. PROVISIONAL. */
#define TAP_P2_CAL2_H 0x06u
/** Page 2 CAL3_L: under-damped threshold low byte (819 = 0x0333).
 *  PROVISIONAL. */
#define TAP_P2_CAL3_L 0x33u
/** Page 2 CAL3_H: under-damped threshold high byte. PROVISIONAL. */
#define TAP_P2_CAL3_H 0x03u
/** Page 2 CAL4_L: 0. PROVISIONAL. */
#define TAP_P2_CAL4_L 0u
/** Page 2 CAL4_H: page select 2. PROVISIONAL. */
#define TAP_P2_CAL4_H 2u

static int32_t tap_detect_base_step (int32_t * p_base, int16_t sample);

/**
 * @brief  One axis of the slow baseline: deviation from the average, then
 *         update the baseline.
 * @param  p_base  The baseline, holds 16x the average.
 * @param  sample  The raw 16-bit sample.
 * @return  |sample - (base / TD_BASE_SCALE)|.
 */
static int32_t tap_detect_base_step (int32_t * p_base, int16_t sample)
{
    int32_t const avg  = *p_base / TD_BASE_SCALE; /* Rounded toward zero. */
    int32_t const diff = sample - avg;            /* At most 65,535 in size. */

    *p_base += diff; /* Slow-moving average. */

    return (diff < 0) ? (-diff) : diff;
}

/**
 * @brief  Zero the detector state.
 * @param  p_detect  The detector.
 */
void tap_detect_init (tap_detect_t * p_detect)
{
    for (uint32_t idx = 0u; idx < TAP_DETECT_WINDOW_SAMPLES; idx++)
    {
        p_detect->window[idx] = 0u;
    }

    p_detect->next       = 0u;
    p_detect->base_x     = 0;
    p_detect->base_y     = 0;
    p_detect->base_z     = 0;
    p_detect->b_seeded   = false;
    p_detect->b_has_last = false;
    p_detect->last_us    = 0;
    p_detect->accepted   = 0u;
    p_detect->rejected   = 0u;
}

/**
 * @brief  One 100 Hz accelerometer sample: update the baselines and the
 *         sliding window.
 * @param  p_detect  The detector.
 * @param  accel_x   Raw X, 16-bit.
 * @param  accel_y   Raw Y, 16-bit.
 * @param  accel_z   Raw Z, 16-bit.
 */
void tap_detect_sample (tap_detect_t * p_detect, int16_t accel_x,
                        int16_t accel_y, int16_t accel_z)
{
    int32_t dev_x   = 0;
    int32_t dev_y   = 0;
    int32_t dev_z   = 0;
    int32_t largest = 0;

    if (!p_detect->b_seeded)
    {
        p_detect->base_x   = accel_x * TD_BASE_SCALE; /* 16x the average. */
        p_detect->base_y   = accel_y * TD_BASE_SCALE;
        p_detect->base_z   = accel_z * TD_BASE_SCALE;
        p_detect->b_seeded = true;
        /* First sample: its deviation is 0. */
    }
    else
    {
        dev_x = tap_detect_base_step(&p_detect->base_x, accel_x);
        dev_y = tap_detect_base_step(&p_detect->base_y, accel_y);
        dev_z = tap_detect_base_step(&p_detect->base_z, accel_z);
    }

    largest = dev_x;

    if (dev_y > largest)
    {
        largest = dev_y;
    }

    if (dev_z > largest)
    {
        largest = dev_z;
    }

    /* Fits uint16_t: max deviation is 65,535. */
    p_detect->window[p_detect->next] = (uint16_t)largest;
    p_detect->next = (p_detect->next + 1u) % TAP_DETECT_WINDOW_SAMPLES;
}

/**
 * @brief  Largest value in the sliding window.
 * @param  p_detect  The detector.
 * @return 0 to 65,535.
 */
uint16_t tap_detect_peak (tap_detect_t const * p_detect)
{
    uint16_t peak = 0u;

    for (uint32_t idx = 0u; idx < TAP_DETECT_WINDOW_SAMPLES; idx++)
    {
        if (p_detect->window[idx] > peak)
        {
            peak = p_detect->window[idx];
        }
    }

    return peak;
}

/**
 * @brief  Handle a tap interrupt: validate status, refractory and peak.
 * @param  p_detect   The detector.
 * @param  t_us       Interrupt time, microseconds.
 * @param  tap_status QMI8658 tap status byte.
 * @param  p_event    Filled only on TAP_DETECT_ACCEPT.
 * @return TAP_DETECT_ACCEPT or a TAP_DETECT_REJECT_* reason.
 */
tap_detect_result_t tap_detect_on_irq (tap_detect_t * p_detect, int64_t t_us,
                                       uint8_t tap_status,
                                       tap_detect_event_t * p_event)
{
    tap_detect_result_t result = TAP_DETECT_ACCEPT;

    /* Masked to two bits, so it fits uint8_t. */
    uint8_t const count = (uint8_t)(tap_status & TAP_STATUS_COUNT_MASK);

    /* Masked to two bits, so it fits uint8_t. */
    uint8_t const axis = (uint8_t)((tap_status >> TAP_STATUS_AXIS_SHIFT) &
                                   TAP_STATUS_AXIS_MASK);

    /* Masked to one bit, so it fits uint8_t. */
    uint8_t const polarity =
        (uint8_t)((tap_status >> TAP_STATUS_POLARITY_SHIFT) &
                  TAP_STATUS_POLARITY_MASK);

    if ((0u == count) || (0u == axis))
    {
        result = TAP_DETECT_REJECT_STATUS;
        p_detect->rejected++;
    }
    else if ((p_detect->b_has_last) &&
             ((t_us - p_detect->last_us) < TAP_DETECT_REFRACTORY_US))
    {
        result = TAP_DETECT_REJECT_REFRACTORY;
        p_detect->rejected++;
    }
    else
    {
        uint16_t const peak = tap_detect_peak(p_detect);

        if (peak < TAP_DETECT_MIN_PEAK_LSB)
        {
            result = TAP_DETECT_REJECT_WEAK;
            p_detect->rejected++;
        }
        else
        {
            p_detect->accepted++;
            p_detect->b_has_last = true;
            p_detect->last_us    = t_us;
            p_event->t_us        = t_us;
            p_event->count       = p_detect->accepted;
            p_event->peak        = peak;
            p_event->axis        = axis;
            p_event->b_positive  = (0u == polarity);

            /* Zero the window so the same spike cannot count twice. */
            for (uint32_t idx = 0u; idx < TAP_DETECT_WINDOW_SAMPLES; idx++)
            {
                p_detect->window[idx] = 0u;
            }

            result = TAP_DETECT_ACCEPT;
        }
    }

    return result;
}

/**
 * @brief  Write the two QMI8658 tap-config pages (register order,
 *         little-endian 16-bit fields).
 * @param  p_first   Buffer of TAP_DETECT_CONFIG_BYTES (page 1).
 * @param  p_second  Buffer of TAP_DETECT_CONFIG_BYTES (page 2).
 */
void tap_detect_encode_config (uint8_t * p_first, uint8_t * p_second)
{
    uint8_t const page1[TAP_DETECT_CONFIG_BYTES] = {
        TAP_P1_CAL1_L, TAP_P1_CAL1_H, TAP_P1_CAL2_L, TAP_P1_CAL2_H,
        TAP_P1_CAL3_L, TAP_P1_CAL3_H, TAP_P1_CAL4_L, TAP_P1_CAL4_H};
    uint8_t const page2[TAP_DETECT_CONFIG_BYTES] = {
        TAP_P2_CAL1_L, TAP_P2_CAL1_H, TAP_P2_CAL2_L, TAP_P2_CAL2_H,
        TAP_P2_CAL3_L, TAP_P2_CAL3_H, TAP_P2_CAL4_L, TAP_P2_CAL4_H};

    for (uint32_t idx = 0u; idx < TAP_DETECT_CONFIG_BYTES; idx++)
    {
        p_first[idx]  = page1[idx];
        p_second[idx] = page2[idx];
    }
}

/*** end of file ***/
