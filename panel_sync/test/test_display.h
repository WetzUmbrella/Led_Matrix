/**
 * @file    test_display.h
 * @brief   Host tests of display_frame (T-B5-02) and orient_filter
 *          (T-B5-05), run by test_main.c.
 *
 * Implemented by Ahsanul (B4) for Jia Jun, tasks T-B5-02 and T-B5-05.
 */
#ifndef TEST_DISPLAY_H
#define TEST_DISPLAY_H

#include <stdbool.h>

/**
 * @brief  Test D1 (T-B5-02 acceptance): the T-B1-01 packed-frame vector
 *         protocol/vectors/frame_pattern.bin unpacks to 192 of 192 bytes
 *         equal to the PC reference (nibble k = k mod 9, wire order G, R, B)
 *         and every byte is 0 to 8; the OD-05 worked vector gives pixel 0 =
 *         (1, 2, 3) and pixel 1 = (4, 5, 6).
 * @return true when every check passed.
 */
bool test_display_unpack_vector (void);

/**
 * @brief  Test D2: rotation remap. Four 90 degree turns are the identity,
 *         180 degrees maps LED i to 63 - i, and every orientation is a
 *         permutation of the 64 LEDs.
 * @return true when every check passed.
 */
bool test_display_rotation (void);

/**
 * @brief  Test D3: frame index from ticks. Nothing before the first tick,
 *         frame k on tick first + k, wrapping at frame_count.
 * @return true when every check passed.
 */
bool test_display_frame_index (void);

/**
 * @brief  Test O1 (T-B5-05 acceptance): 25 equal samples give 0
 *         ORIENTATION_CHANGED, 26 give 1.
 * @return true when every check passed.
 */
bool test_display_orient_debounce (void);

/**
 * @brief  Test O2 (T-B5-05 acceptance, SRM-6E): forced I2C failures keep the
 *         last orientation in 100% of injected samples, set the IMU bit, and
 *         the bit clears when the failures stop.
 * @return true when every check passed.
 */
bool test_display_orient_nack (void);

/**
 * @brief  Test O3: quadrants, weak gravity (no candidate), and a flicker
 *         that restarts the count.
 * @return true when every check passed.
 */
bool test_display_orient_classify (void);

#endif /* TEST_DISPLAY_H */

/*** end of file ***/
