/**
 * @file    test_tap_detect.c
 * @brief   Host tests of tap_detect (software detection, build step 2), run
 *          by test_main.c. Build and run: make -C panel_sync/test.
 *
 * Rewritten 11 Nov 2026 with the detector (DS-02).
 */
#include "test_tap_detect.h"

#include <stddef.h>
#include <stdint.h>

#include "test_main.h"
#include "../main/tap_detect.h"

/** 1 g at +/-16 g, LSB. */
#define TST_G 2048

/** Sample period at 1 kHz, microseconds. */
#define TST_DT_US INT64_C(1000)

/** First sample time, microseconds. */
#define TST_T0 INT64_C(1000000)

/** Samples to settle the baseline before a test signal. */
#define TST_SETTLE 200u

/** Deviations in LSB on Z: 199 mg and 200 mg (x 1,000 / 2,048, rounded
 *  down), 292 mg and 537 mg. */
#define TST_199_MG 409
#define TST_200_MG 410
#define TST_292_MG 600
#define TST_537_MG 1100

/** Taps recorded by tst_feed(). */
#define TST_MAX_TAPS 8u

/** A run of samples and the taps it produced. */
typedef struct
{
    tap_detect_t       detect;
    int64_t            t_us;
    uint32_t           taps;
    tap_detect_event_t tap[TST_MAX_TAPS];
    int64_t            t_report[TST_MAX_TAPS]; /**< Sample that reported it. */
} tst_run_t;

static void tst_start (tst_run_t * p_run);
static void tst_feed (tst_run_t * p_run, uint32_t samples, int16_t accel_x,
                      int16_t accel_y, int16_t accel_z);
static int16_t tst_square (uint32_t sample, int32_t amplitude);

/**
 * @brief  Test K1: the threshold, the tap time and the report time.
 * @return true when every check passed.
 */
bool test_tap_detect_threshold (void)
{
    tst_run_t run;
    bool      b_ok = true;

    tst_start(&run);
    tst_feed(&run, 1u, 0, 0, TST_G + TST_199_MG);
    tst_feed(&run, 50u, 0, 0, TST_G);
    b_ok = (test_main_check(0u == run.taps, __LINE__)) && b_ok;

    int64_t const t_spike = run.t_us;

    tst_feed(&run, 1u, 0, 0, TST_G + TST_200_MG);
    tst_feed(&run, 20u, 0, 0, TST_G);
    b_ok = (test_main_check(1u == run.taps, __LINE__)) && b_ok;
    b_ok = (test_main_check(t_spike == run.tap[0].t_us, __LINE__)) && b_ok;
    b_ok = (test_main_check(200u == run.tap[0].peak, __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == run.tap[0].count, __LINE__)) && b_ok;
    b_ok = (test_main_check((t_spike + TAP_DETECT_PEAK_WINDOW_US) ==
                                run.t_report[0],
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test K2: the peak over the window.
 * @return true when every check passed.
 */
bool test_tap_detect_peak_window (void)
{
    tst_run_t run;
    bool      b_ok = true;

    tst_start(&run);

    int64_t const t_spike = run.t_us;

    tst_feed(&run, 1u, 0, 0, TST_G + 500);
    tst_feed(&run, 1u, 0, 0, TST_G + 1500);
    tst_feed(&run, 1u, 0, 0, TST_G + 3000);
    tst_feed(&run, 1u, 0, 0, TST_G + 1000);
    tst_feed(&run, 40u, 0, 0, TST_G);
    b_ok = (test_main_check(1u == run.taps, __LINE__)) && b_ok;
    b_ok = (test_main_check(t_spike == run.tap[0].t_us, __LINE__)) && b_ok;
    /* 3,000 LSB x 1,000 / 2,048 = 1,464 mg. */
    b_ok = (test_main_check(1464u == run.tap[0].peak, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test K3: a ringing slap counts once.
 * @return true when every check passed.
 */
bool test_tap_detect_ringing (void)
{
    tst_run_t run;
    bool      b_ok = true;

    tst_start(&run);

    /* 6 g falling linearly to 0 over 500 ms, sign flipping every 5 ms. */
    for (uint32_t sample = 0u; sample < 500u; sample++)
    {
        /* sample is below 500: it fits int32_t, and the amplitude falls
         * from 6 g to 0, so the sum stays inside int16_t. */
        int32_t const amplitude =
            ((6 * TST_G) * (500 - (int32_t)sample)) / 500;

        tst_feed(&run, 1u, 0, 0,
                 (int16_t)(TST_G + tst_square(sample, amplitude))); /* Fits. */
    }

    tst_feed(&run, 200u, 0, 0, TST_G);
    b_ok = (test_main_check(1u == run.taps, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test K4: re-arm after quiet.
 * @return true when every check passed.
 */
bool test_tap_detect_rearm (void)
{
    tst_run_t run;
    bool      b_ok = true;

    tst_start(&run);
    tst_feed(&run, 2u, 0, 0, TST_G + 2000);
    tst_feed(&run, 60u, 0, 0, TST_G);
    tst_feed(&run, 2u, 0, 0, TST_G + 2000);
    tst_feed(&run, 60u, 0, 0, TST_G);
    b_ok = (test_main_check(2u == run.taps, __LINE__)) && b_ok;

    /* Second spike 20 ms into the quiet time: part of the first tap. */
    tst_start(&run);
    tst_feed(&run, 2u, 0, 0, TST_G + 2000);
    tst_feed(&run, 30u, 0, 0, TST_G); /* 10 ms window, then 20 ms quiet. */
    tst_feed(&run, 2u, 0, 0, TST_G + 2000);
    tst_feed(&run, 100u, 0, 0, TST_G);
    b_ok = (test_main_check(1u == run.taps, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test K5: the re-arm cap under continuous shaking.
 * @return true when every check passed.
 */
bool test_tap_detect_rearm_cap (void)
{
    tst_run_t run;
    bool      b_ok = true;

    tst_start(&run);

    for (uint32_t sample = 0u; sample < 2000u; sample++)
    {
        /* 1 g +/- 2 g stays inside int16_t. */
        tst_feed(&run, 1u, 0, 0,
                 (int16_t)(TST_G + tst_square(sample, 2 * TST_G)));
    }

    b_ok = (test_main_check((run.taps >= 3u) && (run.taps <= 4u), __LINE__)) &&
           b_ok;

    if (run.taps >= 2u)
    {
        int64_t const gap = run.tap[1].t_us - run.tap[0].t_us;

        b_ok = (test_main_check((gap >= TAP_DETECT_REARM_MAX_US) &&
                                    (gap <= (TAP_DETECT_REARM_MAX_US +
                                             (20 * TST_DT_US))),
                                __LINE__)) && b_ok;
    }

    return b_ok;
}

/**
 * @brief  Test K6: a bad first sample is reseeded away.
 * @return true when every check passed.
 */
bool test_tap_detect_reseed (void)
{
    tst_run_t run;
    bool      b_ok = true;

    /* Seed from a 16 g X reading, then still gravity on Z for 1.5 s. */
    tap_detect_init(&run.detect);
    run.t_us   = TST_T0;
    run.taps   = 0u;
    tst_feed(&run, 1u, INT16_MAX, 0, 0);
    tst_feed(&run, 1500u, 0, 0, TST_G);
    b_ok = (test_main_check(run.taps <= 2u, __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == tap_detect_last_mg(&run.detect),
                            __LINE__)) && b_ok;

    uint32_t const before = run.taps;

    tst_feed(&run, 1u, 0, 0, TST_G + TST_292_MG);
    tst_feed(&run, 20u, 0, 0, TST_G);
    b_ok = (test_main_check((before + 1u) == run.taps, __LINE__)) && b_ok;
    b_ok = (test_main_check(292u == run.tap[run.taps - 1u].peak, __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Test K7: still gravity in six directions.
 * @return true when every check passed.
 */
bool test_tap_detect_gravity (void)
{
    static int16_t const dirs[6][3] = {
        {TST_G, 0, 0}, {-TST_G, 0, 0}, {0, TST_G, 0},
        {0, -TST_G, 0}, {0, 0, TST_G}, {0, 0, -TST_G},
    };
    bool b_ok = true;

    for (uint32_t dir = 0u; dir < 6u; dir++)
    {
        tst_run_t run;

        tap_detect_init(&run.detect);
        run.t_us = TST_T0;
        run.taps = 0u;
        tst_feed(&run, 1000u, dirs[dir][0], dirs[dir][1], dirs[dir][2]);
        b_ok = (test_main_check(0u == run.taps, __LINE__)) && b_ok;
        b_ok = (test_main_check(0u == tap_detect_last_mg(&run.detect),
                                __LINE__)) && b_ok;
    }

    return b_ok;
}

/**
 * @brief  Test K8: the threshold setting.
 * @return true when every check passed.
 */
bool test_tap_detect_set_threshold (void)
{
    tst_run_t run;
    bool      b_ok = true;

    tst_start(&run);
    tap_detect_set_threshold(&run.detect, 10u);
    b_ok = (test_main_check(TAP_DETECT_THRESHOLD_MIN_MG ==
                                run.detect.threshold_mg,
                            __LINE__)) && b_ok;
    tap_detect_set_threshold(&run.detect, 9000u);
    b_ok = (test_main_check(TAP_DETECT_THRESHOLD_MAX_MG ==
                                run.detect.threshold_mg,
                            __LINE__)) && b_ok;

    tap_detect_set_threshold(&run.detect, 500u);
    tst_feed(&run, 1u, 0, 0, TST_G + TST_200_MG);
    tst_feed(&run, 50u, 0, 0, TST_G);
    b_ok = (test_main_check(0u == run.taps, __LINE__)) && b_ok;
    tst_feed(&run, 1u, 0, 0, TST_G + TST_537_MG);
    tst_feed(&run, 20u, 0, 0, TST_G);
    b_ok = (test_main_check(1u == run.taps, __LINE__)) && b_ok;
    b_ok = (test_main_check(537u == run.tap[0].peak, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  A fresh detector with its baseline settled on gravity along Z.
 * @param  p_run  The run.
 */
static void tst_start (tst_run_t * p_run)
{
    tap_detect_init(&p_run->detect);
    p_run->t_us = TST_T0;
    p_run->taps = 0u;
    tst_feed(p_run, TST_SETTLE, 0, 0, TST_G);
    p_run->taps = 0u;
}

/**
 * @brief  Feed the same sample several times at 1 kHz, recording taps.
 * @param  p_run    The run.
 * @param  samples  How many.
 * @param  accel_x  Raw X.
 * @param  accel_y  Raw Y.
 * @param  accel_z  Raw Z.
 */
static void tst_feed (tst_run_t * p_run, uint32_t samples, int16_t accel_x,
                      int16_t accel_y, int16_t accel_z)
{
    for (uint32_t index = 0u; index < samples; index++)
    {
        tap_detect_event_t event;

        if (tap_detect_sample(&p_run->detect, p_run->t_us, accel_x, accel_y,
                              accel_z, &event))
        {
            if (p_run->taps < TST_MAX_TAPS)
            {
                p_run->tap[p_run->taps]      = event;
                p_run->t_report[p_run->taps] = p_run->t_us;
            }

            p_run->taps++;
        }

        p_run->t_us += TST_DT_US;
    }
}

/**
 * @brief  A square wave: +amplitude for 5 samples, then -amplitude for 5.
 * @param  sample     Sample index.
 * @param  amplitude  LSB.
 * @return +amplitude or -amplitude, as int16_t.
 */
static int16_t tst_square (uint32_t sample, int32_t amplitude)
{
    int32_t const value = (0u == ((sample / 5u) % 2u)) ? amplitude : -amplitude;

    return (int16_t)value; /* Callers keep |amplitude| under 2^15. */
}

/*** end of file ***/
