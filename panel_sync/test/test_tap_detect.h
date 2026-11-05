/**
 * @file    test_tap_detect.h
 * @brief   Host tests of tap_detect (tap events with peak size), run by
 *          test_main.c.
 *
 * Implemented by Ahsanul (B4) for Jia Jun.
 */
#ifndef TEST_TAP_DETECT_H
#define TEST_TAP_DETECT_H

#include <stdbool.h>

/**
 * @brief  Test K1: a peak just below the threshold is REJECT_WEAK, exactly
 *         the threshold is ACCEPTed with peak, count 1, axis Z, positive.
 * @return true when every check passed.
 */
bool test_tap_detect_threshold (void);

/**
 * @brief  Test K2: a new spike inside the refractory period is
 *         REJECT_REFRACTORY; the same spike exactly one period later is
 *         ACCEPTed and count becomes 2.
 * @return true when every check passed.
 */
bool test_tap_detect_refractory (void);

/**
 * @brief  Test K3: invalid count/axis give REJECT_STATUS and leave the
 *         window alone; polarity and axis decode from the status byte;
 *         double tap is ACCEPTed.
 * @return true when every check passed.
 */
bool test_tap_detect_status (void);

/**
 * @brief  Test K4: 100 constant gravity samples give peak 0 in every still
 *         direction; INT16_MIN then INT16_MAX gives peak 65,535.
 * @return true when every check passed.
 */
bool test_tap_detect_gravity (void);

/**
 * @brief  Test K5: a spike survives 19 quiet samples as the peak, is gone
 *         after 20, and the window is zeroed after ACCEPT.
 * @return true when every check passed.
 */
bool test_tap_detect_window (void);

/**
 * @brief  Test K6: both config pages equal the exact documented byte
 *         arrays.
 * @return true when every check passed.
 */
bool test_tap_detect_encode (void);

#endif /* TEST_TAP_DETECT_H */

/*** end of file ***/
