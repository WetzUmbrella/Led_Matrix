/**
 * @file    test_wall_turn.h
 * @brief   Host tests of wall_turn (D-17: whole-wall rotation, E3 to E5).
 *
 * Written for D-17, 24 Nov 2026.
 */
#ifndef TEST_WALL_TURN_H
#define TEST_WALL_TURN_H

#include <stdbool.h>

/**
 * @brief  C35: a quarter turn moves (r, c) to (c, rows - 1 - r) and swaps
 *         rows and cols (2x3 to 3x2, 1x9 to 9x1); four quarter turns give
 *         back every grid from 1x1 to 9x9, sparse ones too; the match finds
 *         the turn between two layouts, prefers the turn asked for on a
 *         symmetric layout, and fails for a layout that is not a turn.
 * @return true when every check passed.
 */
bool test_wall_turn_layout (void);

/**
 * @brief  C36: the detector: a turn of every active tile by the same amount
 *         is a wall turn when the 1.5 s window closes (90, 270 and a 180
 *         that passes through 90); a partial or mixed turn is not, and a
 *         later window can still complete a slow turn; a tile never heard
 *         blocks it; references move on after a turn, so turning back is
 *         the opposite turn.
 * @return true when every check passed.
 */
bool test_wall_turn_detect (void);

#endif /* TEST_WALL_TURN_H */

/*** end of file ***/
