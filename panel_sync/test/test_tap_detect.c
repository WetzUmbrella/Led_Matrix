/**
 * @file    test_tap_detect.c
 * @brief   Host tests of tap_detect (tap events with peak size), run by
 *          test_main.c.
 *
 * Implemented by Ahsanul (B4) for Jia Jun. Build and run:
 * make -C panel_sync/test.
 */
#include "test_tap_detect.h"

#include <stddef.h>
#include <stdint.h>

#include "test_main.h"
#include "../main/tap_detect.h"

/** 1 g at 4,096 LSB/g (+/- 8 g range). */
#define TEST_GRAVITY_LSB 4096
/** A clear spike above the baseline, LSB. */
#define TEST_SPIKE_ABOVE 3000
/** The spike sample (gravity plus spike). */
#define TEST_SPIKE_LSB (TEST_GRAVITY_LSB + TEST_SPIKE_ABOVE)
/** Minimum accepted peak, LSB. */
#define TEST_MIN_PEAK 2048u
/** Just below the minimum peak. */
#define TEST_BELOW_PEAK 2047
/** First interrupt time, microseconds. */
#define TEST_T0 1000000
/** One microsecond inside the refractory period. */
#define TEST_REFRACTORY_MINUS_1 149999
/** Exactly one refractory period, microseconds. */
#define TEST_REFRACTORY 150000
/** Constant-gravity samples per direction. */
#define TEST_SAMPLE_COUNT 100
/** Gravity directions tested (+X, -X, +Y, -Y, +Z). */
#define TEST_DIRECTION_COUNT 5
/** INT16_MAX (extreme seed). */
#define TEST_INT16_MAX 32767
/** INT16_MIN (extreme seed). */
#define TEST_INT16_MIN (-32768)
/** UINT16_MAX (maximum deviation). */
#define TEST_UINT16_MAX 65535
/** Quiet samples after a spike while it is still the peak. */
#define TEST_QUIET_KEEP 19

/* Status bytes: bits 1..0 = tap count, bits 5..4 = axis, bit 7 = polarity. */
/** count 0, axis Z: invalid count. */
#define TEST_STATUS_COUNT0 0x30u
/** count 1, axis 0: invalid axis. */
#define TEST_STATUS_AXIS0 0x01u
/** count 1, axis Z, positive. */
#define TEST_STATUS_Z_POS 0x31u
/** count 1, axis Z, negative. */
#define TEST_STATUS_Z_NEG 0xB1u
/** count 1, axis X, positive. */
#define TEST_STATUS_X_POS 0x11u
/** count 1, axis Y, positive. */
#define TEST_STATUS_Y_POS 0x21u
/** count 2 (double tap), axis Z, positive. */
#define TEST_STATUS_DOUBLE 0x32u

/* Page 1: {10, 0, 15, 0, 10, 0, 0, 1}. */
/** Page 1 CAL1_L: peak window 10 samples. */
#define TEST_P1_B0 10u
/** Page 1 CAL1_H: priority 0. */
#define TEST_P1_B1 0u
/** Page 1 CAL2_L: tap window 15 samples. */
#define TEST_P1_B2 15u
/** Page 1 CAL2_H: tap window high byte 0. */
#define TEST_P1_B3 0u
/** Page 1 CAL3_L: double-tap window 10 samples. */
#define TEST_P1_B4 10u
/** Page 1 CAL3_H: double-tap window high byte 0. */
#define TEST_P1_B5 0u
/** Page 1 CAL4_L: 0. */
#define TEST_P1_B6 0u
/** Page 1 CAL4_H: page select 1. */
#define TEST_P1_B7 1u

/* Page 2: {8, 32, 0x66, 0x06, 0x33, 0x03, 0, 2}. */
/** Page 2 CAL1_L: alpha 8 (0.0625 in u0.7). */
#define TEST_P2_B0 8u
/** Page 2 CAL1_H: gamma 32 (0.25 in u0.7). */
#define TEST_P2_B1 32u
/** Page 2 CAL2_L: peak magnitude threshold low byte (1638 = 0x0666). */
#define TEST_P2_B2 0x66u
/** Page 2 CAL2_H: peak magnitude threshold high byte. */
#define TEST_P2_B3 0x06u
/** Page 2 CAL3_L: under-damped threshold low byte (819 = 0x0333). */
#define TEST_P2_B4 0x33u
/** Page 2 CAL3_H: under-damped threshold high byte. */
#define TEST_P2_B5 0x03u
/** Page 2 CAL4_L: 0. */
#define TEST_P2_B6 0u
/** Page 2 CAL4_H: page select 2. */
#define TEST_P2_B7 2u

/**
 * @brief  Initialise the detector, seed with one sample, feed one spike.
 * @param  p_detect  The detector.
 * @param  seed_x    Seed X.
 * @param  seed_y    Seed Y.
 * @param  seed_z    Seed Z.
 * @param  spike_x   Spike X.
 * @param  spike_y   Spike Y.
 * @param  spike_z   Spike Z.
 */
static void seed_and_spike (tap_detect_t * p_detect, int16_t seed_x,
                            int16_t seed_y, int16_t seed_z, int16_t spike_x,
                            int16_t spike_y, int16_t spike_z)
{
    tap_detect_init(p_detect);
    tap_detect_sample(p_detect, seed_x, seed_y, seed_z);
    tap_detect_sample(p_detect, spike_x, spike_y, spike_z);
}

/**
 * @brief  Test K1: a peak just below the threshold is REJECT_WEAK, exactly
 *         the threshold is ACCEPTed with peak, count 1, axis Z, positive.
 * @return true when every check passed.
 */
bool test_tap_detect_threshold (void)
{
    bool                b_ok = true;
    tap_detect_t        detect;
    tap_detect_event_t  event = {0};
    tap_detect_result_t got;

    /* Seed at (0, 0, 1 g): a Z sample 2047 above baseline is weak. */
    tap_detect_init(&detect);
    tap_detect_sample(&detect, 0, 0, TEST_GRAVITY_LSB);
    tap_detect_sample(&detect, 0, 0, TEST_GRAVITY_LSB + TEST_BELOW_PEAK);
    got = tap_detect_on_irq(&detect, TEST_T0, TEST_STATUS_Z_POS, &event);
    b_ok = (test_main_check(TAP_DETECT_REJECT_WEAK == got, __LINE__)) && b_ok;

    /* Fresh detector: exactly 2048 above baseline is accepted. */
    tap_detect_init(&detect);
    tap_detect_sample(&detect, 0, 0, TEST_GRAVITY_LSB);
    tap_detect_sample(&detect, 0, 0, TEST_GRAVITY_LSB + TEST_MIN_PEAK);
    got = tap_detect_on_irq(&detect, TEST_T0, TEST_STATUS_Z_POS, &event);
    b_ok = (test_main_check(TAP_DETECT_ACCEPT == got, __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == event.count, __LINE__)) && b_ok;
    b_ok = (test_main_check(TEST_MIN_PEAK == event.peak, __LINE__)) && b_ok;
    b_ok = (test_main_check(TAP_DETECT_AXIS_Z == event.axis, __LINE__)) && b_ok;
    b_ok = (test_main_check(true == event.b_positive, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test K2: a new spike inside the refractory period is
 *         REJECT_REFRACTORY; the same spike exactly one period later is
 *         ACCEPTed and count becomes 2.
 * @return true when every check passed.
 */
bool test_tap_detect_refractory (void)
{
    bool                b_ok = true;
    tap_detect_t        detect;
    tap_detect_event_t  event = {0};
    tap_detect_result_t got;

    seed_and_spike(&detect, 0, 0, TEST_GRAVITY_LSB, 0, 0, TEST_SPIKE_LSB);
    got = tap_detect_on_irq(&detect, TEST_T0, TEST_STATUS_Z_POS, &event);
    b_ok = (test_main_check(TAP_DETECT_ACCEPT == got, __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == event.count, __LINE__)) && b_ok;

    /* Re-spike: still present in the window. */
    tap_detect_sample(&detect, 0, 0, TEST_SPIKE_LSB);
    got = tap_detect_on_irq(&detect, TEST_T0 + TEST_REFRACTORY_MINUS_1,
                            TEST_STATUS_Z_POS, &event);
    b_ok = (test_main_check(TAP_DETECT_REJECT_REFRACTORY == got, __LINE__)) &&
           b_ok;

    /* Same spike, exactly one refractory period later: accepted, count 2. */
    got = tap_detect_on_irq(&detect, TEST_T0 + TEST_REFRACTORY,
                            TEST_STATUS_Z_POS, &event);
    b_ok = (test_main_check(TAP_DETECT_ACCEPT == got, __LINE__)) && b_ok;
    b_ok = (test_main_check(2u == event.count, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test K3: invalid count/axis give REJECT_STATUS and leave the
 *         window alone; polarity and axis decode from the status byte;
 *         double tap is ACCEPTed.
 * @return true when every check passed.
 */
bool test_tap_detect_status (void)
{
    bool                b_ok = true;
    tap_detect_t        detect;
    tap_detect_event_t  event = {0};
    tap_detect_result_t got;

    /* count 0 and axis 0 are both REJECT_STATUS; the window is untouched. */
    seed_and_spike(&detect, 0, 0, TEST_GRAVITY_LSB, 0, 0, TEST_SPIKE_LSB);
    got = tap_detect_on_irq(&detect, TEST_T0, TEST_STATUS_COUNT0, &event);
    b_ok = (test_main_check(TAP_DETECT_REJECT_STATUS == got, __LINE__)) &&
           b_ok;
    got = tap_detect_on_irq(&detect, TEST_T0, TEST_STATUS_AXIS0, &event);
    b_ok = (test_main_check(TAP_DETECT_REJECT_STATUS == got, __LINE__)) &&
           b_ok;
    /* Window still holds the spike: a valid status is accepted. */
    got = tap_detect_on_irq(&detect, TEST_T0, TEST_STATUS_Z_POS, &event);
    b_ok = (test_main_check(TAP_DETECT_ACCEPT == got, __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == event.count, __LINE__)) && b_ok;

    /* 0xB1: axis Z, negative polarity. */
    seed_and_spike(&detect, 0, 0, TEST_GRAVITY_LSB, 0, 0, TEST_SPIKE_LSB);
    got = tap_detect_on_irq(&detect, TEST_T0, TEST_STATUS_Z_NEG, &event);
    b_ok = (test_main_check(TAP_DETECT_ACCEPT == got, __LINE__)) && b_ok;
    b_ok = (test_main_check(TAP_DETECT_AXIS_Z == event.axis, __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(false == event.b_positive, __LINE__)) && b_ok;

    /* 0x11: axis X. */
    seed_and_spike(&detect, 0, 0, TEST_GRAVITY_LSB, 0, 0, TEST_SPIKE_LSB);
    got = tap_detect_on_irq(&detect, TEST_T0, TEST_STATUS_X_POS, &event);
    b_ok = (test_main_check(TAP_DETECT_ACCEPT == got, __LINE__)) && b_ok;
    b_ok = (test_main_check(TAP_DETECT_AXIS_X == event.axis, __LINE__)) &&
           b_ok;

    /* 0x21: axis Y. */
    seed_and_spike(&detect, 0, 0, TEST_GRAVITY_LSB, 0, 0, TEST_SPIKE_LSB);
    got = tap_detect_on_irq(&detect, TEST_T0, TEST_STATUS_Y_POS, &event);
    b_ok = (test_main_check(TAP_DETECT_ACCEPT == got, __LINE__)) && b_ok;
    b_ok = (test_main_check(TAP_DETECT_AXIS_Y == event.axis, __LINE__)) &&
           b_ok;

    /* 0x32 (double tap): accepted. */
    seed_and_spike(&detect, 0, 0, TEST_GRAVITY_LSB, 0, 0, TEST_SPIKE_LSB);
    got = tap_detect_on_irq(&detect, TEST_T0, TEST_STATUS_DOUBLE, &event);
    b_ok = (test_main_check(TAP_DETECT_ACCEPT == got, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test K4: 100 constant gravity samples give peak 0 in every still
 *         direction; INT16_MIN then INT16_MAX gives peak 65,535.
 * @return true when every check passed.
 */
bool test_tap_detect_gravity (void)
{
    bool         b_ok = true;
    tap_detect_t detect;

    /* +X, -X, +Y, -Y, +Z: 1 g in one still direction. */
    int16_t const dirs[TEST_DIRECTION_COUNT][3] = {{TEST_GRAVITY_LSB, 0, 0},
                                                   {-TEST_GRAVITY_LSB, 0, 0},
                                                   {0, TEST_GRAVITY_LSB, 0},
                                                   {0, -TEST_GRAVITY_LSB, 0},
                                                   {0, 0, TEST_GRAVITY_LSB}};

    for (uint32_t idx = 0u; idx < TEST_DIRECTION_COUNT; idx++)
    {
        tap_detect_init(&detect);

        for (uint32_t sample_idx = 0u; sample_idx < TEST_SAMPLE_COUNT;
             sample_idx++)
        {
            tap_detect_sample(&detect, dirs[idx][0], dirs[idx][1],
                              dirs[idx][2]);
        }

        b_ok = (test_main_check(0u == tap_detect_peak(&detect), __LINE__)) &&
               b_ok;
    }

    /* Extreme: seed at INT16_MIN, then INT16_MAX -> deviation 65,535. */
    tap_detect_init(&detect);
    tap_detect_sample(&detect, TEST_INT16_MIN, 0, 0);
    tap_detect_sample(&detect, TEST_INT16_MAX, 0, 0);
    b_ok = (test_main_check(TEST_UINT16_MAX == tap_detect_peak(&detect),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Test K5: a spike survives 19 quiet samples as the peak, is gone
 *         after 20, and the window is zeroed after ACCEPT.
 * @return true when every check passed.
 */
bool test_tap_detect_window (void)
{
    bool                b_ok = true;
    tap_detect_t        detect;
    tap_detect_event_t  event = {0};
    tap_detect_result_t got;

    /* Seed, then one spike; the spike is the window peak. */
    tap_detect_init(&detect);
    tap_detect_sample(&detect, 0, 0, TEST_GRAVITY_LSB);
    tap_detect_sample(&detect, 0, 0, TEST_SPIKE_LSB);

    /* 19 quiet samples equal to the seed: spike still the peak. */
    for (uint32_t idx = 0u; idx < TEST_QUIET_KEEP; idx++)
    {
        tap_detect_sample(&detect, 0, 0, TEST_GRAVITY_LSB);
    }
    b_ok = (test_main_check(TEST_SPIKE_ABOVE == tap_detect_peak(&detect),
                            __LINE__)) &&
           b_ok;

    /* The 20th quiet sample overwrites the spike slot: peak is gone. */
    tap_detect_sample(&detect, 0, 0, TEST_GRAVITY_LSB);
    b_ok = (test_main_check(TAP_DETECT_MIN_PEAK_LSB > tap_detect_peak(&detect),
                            __LINE__)) &&
           b_ok;

    /* After ACCEPT the window is zeroed: peak is 0. */
    tap_detect_init(&detect);
    tap_detect_sample(&detect, 0, 0, TEST_GRAVITY_LSB);
    tap_detect_sample(&detect, 0, 0, TEST_SPIKE_LSB);
    got = tap_detect_on_irq(&detect, TEST_T0, TEST_STATUS_Z_POS, &event);
    b_ok = (test_main_check(TAP_DETECT_ACCEPT == got, __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == tap_detect_peak(&detect), __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Test K6: both config pages equal the exact documented byte
 *         arrays.
 * @return true when every check passed.
 */
bool test_tap_detect_encode (void)
{
    bool    b_ok = true;
    uint8_t first[TAP_DETECT_CONFIG_BYTES];
    uint8_t second[TAP_DETECT_CONFIG_BYTES];

    uint8_t const expect_first[TAP_DETECT_CONFIG_BYTES] = {
        TEST_P1_B0, TEST_P1_B1, TEST_P1_B2, TEST_P1_B3,
        TEST_P1_B4, TEST_P1_B5, TEST_P1_B6, TEST_P1_B7};
    uint8_t const expect_second[TAP_DETECT_CONFIG_BYTES] = {
        TEST_P2_B0, TEST_P2_B1, TEST_P2_B2, TEST_P2_B3,
        TEST_P2_B4, TEST_P2_B5, TEST_P2_B6, TEST_P2_B7};

    tap_detect_encode_config(first, second);

    for (uint32_t idx = 0u; idx < TAP_DETECT_CONFIG_BYTES; idx++)
    {
        b_ok = (test_main_check(expect_first[idx] == first[idx], __LINE__)) &&
               b_ok;
        b_ok = (test_main_check(expect_second[idx] == second[idx], __LINE__)) &&
               b_ok;
    }

    return b_ok;
}

/*** end of file ***/
