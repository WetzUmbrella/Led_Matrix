/**
 * @file    test_game_rules.h
 * @brief   Host tests of the whack-a-mole rules (C17, C18).
 *
 * Owner: Benjamin (Buddy 2). Written for the whack-a-mole re-plan, build
 * step 7.
 */
#ifndef TEST_GAME_RULES_H
#define TEST_GAME_RULES_H

#include <stdbool.h>

/**
 * @brief  C17: a 30 s round with no taps: mole time 1193 ms at the first
 *         mole, 1200 ms at the start and 500 ms at the end, a 300 ms gap
 *         after each timeout, never the same tile twice in a row, only active
 *         tiles, the round ends at 30 s, and game_next_due() never lies.
 * @return true when every check passed.
 */
bool test_game_rules_timing (void);

/**
 * @brief  C18: hits (+1, the mole ends at once), wrong tiles and taps with no
 *         mole up (-1), dedupe within 50 ms (highest peak counts, later taps
 *         count separately), and a stopped round ignores taps.
 * @return true when every check passed.
 */
bool test_game_rules_scoring (void);

#endif /* TEST_GAME_RULES_H */

/*** end of file ***/
