/**
 * @file    test_tap_detect.h
 * @brief   Host tests of tap_detect (software detection, build step 2), run
 *          by test_main.c. Signals are synthetic, shaped like the 9 Nov bench
 *          data (docs/bench/9nov: F-05, F-10), at 1 kHz with gravity on Z.
 *
 * Rewritten 11 Nov 2026 with the detector; the old K1 to K6 tested the
 * QMI8658 tap engine interface, which is gone.
 */
#ifndef TEST_TAP_DETECT_H
#define TEST_TAP_DETECT_H

#include <stdbool.h>

/**
 * @brief  Test K1: a spike of 199 mg gives no tap, 200 mg gives one, timed at
 *         the spike and reported when the peak window ends.
 * @return true when every check passed.
 */
bool test_tap_detect_threshold (void);

/**
 * @brief  Test K2: the peak is the largest deviation in the 10 ms window,
 *         not the first sample over the threshold.
 * @return true when every check passed.
 */
bool test_tap_detect_peak_window (void);

/**
 * @brief  Test K3: a slap ringing over the threshold for 480 ms counts once.
 * @return true when every check passed.
 */
bool test_tap_detect_ringing (void);

/**
 * @brief  Test K4: two taps 300 ms apart count twice; a rebound 146 ms after
 *         a slap (bench, round 13) and a spike inside the quiet time count
 *         once with the first.
 * @return true when every check passed.
 */
bool test_tap_detect_rearm (void);

/**
 * @brief  Test K5: continuous shaking re-arms at most every 600 ms.
 * @return true when every check passed.
 */
bool test_tap_detect_rearm_cap (void);

/**
 * @brief  Test K6: a bad first sample is reseeded away within about a
 *         second; a later tap then has its true peak.
 * @return true when every check passed.
 */
bool test_tap_detect_reseed (void);

/**
 * @brief  Test K7: still gravity in any of six directions gives no tap and a
 *         deviation of 0.
 * @return true when every check passed.
 */
bool test_tap_detect_gravity (void);

/**
 * @brief  Test K8: the threshold is clamped to 50 to 5,000 mg and acts: at
 *         500 mg a 200 mg spike gives nothing and a 537 mg spike a tap.
 * @return true when every check passed.
 */
bool test_tap_detect_set_threshold (void);

#endif /* TEST_TAP_DETECT_H */

/*** end of file ***/
