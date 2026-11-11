/**
 * @file    test_score_font.h
 * @brief   Host test of the final score font and its per-tile slices (C19).
 *
 * Owner: Benjamin (Buddy 2). Written for the whack-a-mole re-plan, build
 * step 7.
 */
#ifndef TEST_SCORE_FONT_H
#define TEST_SCORE_FONT_H

#include <stdbool.h>

/**
 * @brief  C19: digits and sign are drawn with the right pixel counts, the
 *         text is centred over the active tiles only, the tile slices of a
 *         wall add up to the whole drawing, and bad positions draw nothing.
 * @return true when every check passed.
 */
bool test_score_font_slices (void);

/**
 * @brief  C19b: wall pictures scaled to every shape, sparse fallback.
 * @return true when every check passed.
 */
bool test_score_font_pictures (void);

#endif /* TEST_SCORE_FONT_H */

/*** end of file ***/
