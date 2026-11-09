/**
 * @file    test_score_font.c
 * @brief   Host test of score_font (C19): glyph pixel counts, centring over
 *          the active tiles, and the slices of a wall.
 *
 * Owner: Benjamin (Buddy 2). Written for the whack-a-mole re-plan, build
 * step 7.
 */
#include "test_score_font.h"

#include <stdint.h>

#include "score_font.h"
#include "test_main.h"

/** Set bits of a 64-bit mask. */
static uint32_t tst_popcount (uint64_t value)
{
    uint32_t count = 0u;

    for (uint32_t bit = 0u; bit < 64u; bit++)
    {
        if (0u != (value & (UINT64_C(1) << bit)))
        {
            count++;
        }
    }

    return count;
}

/** Lit pixels of the whole wall, row by row, for a score and a layout. */
static uint32_t tst_wall_count (int32_t score, uint8_t rows, uint8_t cols,
                                uint16_t active)
{
    uint32_t count = 0u;

    for (uint32_t pixel_y = 0u; pixel_y < (rows * 8u); pixel_y++)
    {
        for (uint32_t pixel_x = 0u; pixel_x < (cols * 8u); pixel_x++)
        {
            if (score_font_wall_lit(score, rows, cols, active, pixel_x,
                                    pixel_y))
            {
                count++;
            }
        }
    }

    return count;
}

/**
 * @brief  C19: digits and sign are drawn with the right pixel counts, the
 *         text is centred over the active tiles only, the tile slices of a
 *         wall add up to the whole drawing, and bad positions draw nothing.
 * @return true when every check passed.
 */
bool test_score_font_slices (void)
{
    bool b_ok = true;

    /* One tile: 0 has 12 lit pixels, 1 has 8, -1 has 3 + 8 = 11. */
    b_ok = (test_main_check(12u == tst_wall_count(0, 1, 1, 0x1u), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(8u == tst_wall_count(1, 1, 1, 0x1u), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(11u == tst_wall_count(-1, 1, 1, 0x1u), __LINE__)) &&
           b_ok;

    /* Two digits: 10 is 1 then 0, 8 + 12 = 20 lit on a full 2 x 2 wall. */
    b_ok = (test_main_check(20u == tst_wall_count(10, 2, 2, 0x000Fu),
                            __LINE__)) && b_ok;

    /* One tile centred on the 8 x 8 tile: 0 starts at column 2, row 1. */
    b_ok = (test_main_check(score_font_wall_lit(0, 1, 1, 0x1u, 2u, 1u),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(!score_font_wall_lit(0, 1, 1, 0x1u, 3u, 2u),
                            __LINE__)) && b_ok;

    /* The text is centred on the active tiles, not the whole wall: with
     * only the top-left tile of a 2 x 2 active, a 1 is drawn inside it. */
    b_ok = (test_main_check(8u == tst_wall_count(1, 2, 2, 0x1u), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(!score_font_wall_lit(1, 2, 2, 0x1u, 0u, 9u),
                            __LINE__)) && b_ok;

    /* Slices of a 1 x 2 wall add up to the whole drawing, each on its tile. */
    uint64_t const left  = score_font_tile_mask(10, 1, 2, 0x3u, 0u);
    uint64_t const right = score_font_tile_mask(10, 1, 2, 0x3u, 1u);
    b_ok = (test_main_check(0u == (left & right), __LINE__)) && b_ok;
    b_ok = (test_main_check(20u == (tst_popcount(left) + tst_popcount(right)),
                            __LINE__)) && b_ok;

    /* Blank positions and bad positions draw nothing. */
    b_ok = (test_main_check(0u == score_font_tile_mask(10, 1, 2, 0x1u, 1u),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == score_font_tile_mask(0, 1, 1, 0x1u, 1u),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == score_font_tile_mask(0, 0, 1, 0x1u, 0u),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == score_font_tile_mask(0, 3, 4, 0x1u, 0u),
                            __LINE__)) && b_ok;

    /* Scores past 99 are clamped to 99. */
    b_ok = (test_main_check(score_font_tile_mask(150, 1, 1, 0x1u, 0u) ==
                                score_font_tile_mask(99, 1, 1, 0x1u, 0u),
                            __LINE__)) && b_ok;

    return b_ok;
}

/*** end of file ***/
