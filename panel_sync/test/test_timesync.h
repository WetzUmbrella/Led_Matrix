/**
 * @file    test_timesync.h
 * @brief   Host tests of timesync (T-B4-02), run by test_main.c.
 *
 * Owner: Ahsanul (Buddy 4). Every time value in the tests is synthetic test
 * data.
 */
#ifndef TEST_TIMESYNC_H
#define TEST_TIMESYNC_H

#include <stdbool.h>

/**
 * @brief  Test 1: slew values -100, 0, +100 round-trip through the int8_t
 *         field unchanged; requests of -101 and +101 are limited to -100 and
 *         +100.
 * @return true when every check passed.
 */
bool test_timesync_slew_int8 (void);

/**
 * @brief  Test 2: 16 synthetic samples with one known minimum: the estimate
 *         equals that sample's offset exactly.
 * @return true when every check passed.
 */
bool test_timesync_min_filter (void);

/**
 * @brief  Test 3: an injected 2 ms offset step is absorbed in exactly 20
 *         ticks.
 * @return true when every check passed.
 */
bool test_timesync_step_20_ticks (void);

/**
 * @brief  Test 4: forced writer interleaving: every snapshot equals a
 *         complete old or new set, and after 3 forced failures SYNC_STALE is
 *         set and the last good snapshot returned.
 * @return true when every check passed.
 */
bool test_timesync_seqlock (void);

/**
 * @brief  Supplementary arithmetic checks (not counted in Pass = N of 4).
 * @return true when every check passed.
 */
bool test_timesync_supp_arithmetic (void);

#endif /* TEST_TIMESYNC_H */

/*** end of file ***/
