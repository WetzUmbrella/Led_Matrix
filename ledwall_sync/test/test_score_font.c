/**
 * @file    test_score_font.c
 * @brief   Host test of score_font (C19): glyph pixel counts, centring over
 *          the active tiles, and the slices of a wall.
 *
 * Owner: Benjamin (Buddy 2). Written for the whack-a-mole re-plan, build
 * step 7.
 */
#include "test_score_font.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

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

/**
 * @brief  The cells of a grid of up to 9 cells given as a position bitmap
 *         (bit p = cell (p / cols, p % cols)), the form these cases were
 *         written in before step 4d.
 * @param  cols    Columns, 1 to 9.
 * @param  active  Position bitmap.
 * @return The cells.
 */
static score_cells_t tst_cells (uint8_t cols, uint16_t active)
{
    score_cells_t cells = {{0u}};

    for (uint32_t pos = 0u; (0u != cols) && (pos < 16u); pos++)
    {
        if ((0u != (active & (UINT16_C(1) << pos))) &&
            ((pos / cols) < SCORE_FONT_GRID_MAX))
        {
            /* pos % cols < 9: the shift fits. */
            cells.row_bits[pos / cols] |= (uint16_t)(1u << (pos % cols));
        }
    }

    return cells;
}

/** score_font_picture_lit() over a position bitmap. */
static bool tst_lit (score_pic_t pic, int32_t value, uint8_t rows,
                     uint8_t cols, uint16_t active, uint32_t pixel_x,
                     uint32_t pixel_y)
{
    score_cells_t const cells = tst_cells(cols, active);

    return score_font_picture_lit(pic, value, rows, cols, &cells, pixel_x,
                                  pixel_y);
}

/** score_font_picture_mask() of the tile at a position. */
static uint64_t tst_mask (score_pic_t pic, int32_t value, uint8_t rows,
                          uint8_t cols, uint16_t active, uint8_t slot)
{
    score_cells_t const cells = tst_cells(cols, active);
    /* cols is 1 to 9 where it is used, and slot below 16: both fit. */
    uint8_t const row = (0u != cols) ? (uint8_t)(slot / cols) : 0u;
    /* As above. */
    uint8_t const col = (0u != cols) ? (uint8_t)(slot % cols) : 0u;

    return score_font_picture_mask(pic, value, rows, cols, &cells, row, col);
}

/** score_font_wall_lit() over a position bitmap. */
static bool tst_wall_lit (int32_t score, uint8_t rows, uint8_t cols,
                          uint16_t active, uint32_t pixel_x, uint32_t pixel_y)
{
    return tst_lit(SCORE_PIC_NUMBER, score, rows, cols, active, pixel_x,
                   pixel_y);
}

/** score_font_tile_mask() of the tile at a position. */
static uint64_t tst_tile_mask (int32_t score, uint8_t rows, uint8_t cols,
                               uint16_t active, uint8_t slot)
{
    return tst_mask(SCORE_PIC_NUMBER, score, rows, cols, active, slot);
}

/** score_font_sparse() over a position bitmap. */
static bool tst_sparse (uint8_t rows, uint8_t cols, uint16_t active)
{
    score_cells_t const cells = tst_cells(cols, active);

    return score_font_sparse(rows, cols, &cells);
}

/** Lit pixels of the whole wall, row by row, for a picture and a layout. */
static uint32_t tst_pic_count (score_pic_t pic, int32_t value, uint8_t rows,
                               uint8_t cols, uint16_t active)
{
    uint32_t count = 0u;

    for (uint32_t pixel_y = 0u; pixel_y < (rows * 8u); pixel_y++)
    {
        for (uint32_t pixel_x = 0u; pixel_x < (cols * 8u); pixel_x++)
        {
            if (tst_lit(pic, value, rows, cols, active, pixel_x, pixel_y))
            {
                count++;
            }
        }
    }

    return count;
}

/** Lit pixels of all the tile slices of a picture, summed. */
static uint32_t tst_slice_count (score_pic_t pic, int32_t value, uint8_t rows,
                                 uint8_t cols, uint16_t active)
{
    uint32_t count = 0u;

    for (uint32_t slot = 0u; slot < (rows * cols); slot++)
    {
        /* slot < 9: fits uint8_t. */
        count += tst_popcount(
            tst_mask(pic, value, rows, cols, active, (uint8_t)slot));
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
            if (tst_wall_lit(score, rows, cols, active, pixel_x, pixel_y))
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

    /* Two digits: 10 is 1 then 0, 8 + 12 = 20 pixels, at 2 x on a full
     * 2 x 2 wall (7 x 5 into 16 x 16): 80 lit. */
    b_ok = (test_main_check(80u == tst_wall_count(10, 2, 2, 0x000Fu),
                            __LINE__)) && b_ok;

    /* One tile centred on the 8 x 8 tile: 0 starts at column 2, row 1. */
    b_ok = (test_main_check(tst_wall_lit(0, 1, 1, 0x1u, 2u, 1u), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(!tst_wall_lit(0, 1, 1, 0x1u, 3u, 2u), __LINE__)) &&
           b_ok;

    /* The text is centred on the active tiles, not the whole wall: with
     * only the top-left tile of a 2 x 2 active, a 1 is drawn inside it. */
    b_ok = (test_main_check(8u == tst_wall_count(1, 2, 2, 0x1u), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(!tst_wall_lit(1, 2, 2, 0x1u, 0u, 9u), __LINE__)) &&
           b_ok;

    /* Slices of a 1 x 2 wall add up to the whole drawing, each on its tile. */
    uint64_t const left  = tst_tile_mask(10, 1, 2, 0x3u, 0u);
    uint64_t const right = tst_tile_mask(10, 1, 2, 0x3u, 1u);
    b_ok = (test_main_check(0u == (left & right), __LINE__)) && b_ok;
    b_ok = (test_main_check(20u == (tst_popcount(left) + tst_popcount(right)),
                            __LINE__)) &&
           b_ok;

    /* Blank positions and bad positions draw nothing. */
    b_ok =
        (test_main_check(0u == tst_tile_mask(10, 1, 2, 0x1u, 1u), __LINE__)) &&
        b_ok;
    b_ok =
        (test_main_check(0u == tst_tile_mask(0, 1, 1, 0x1u, 1u), __LINE__)) &&
        b_ok;
    b_ok =
        (test_main_check(0u == tst_tile_mask(0, 0, 1, 0x1u, 0u), __LINE__)) &&
        b_ok;
    /* Since step 4d a 3 x 4 grid is valid; 10 rows is not. */
    b_ok =
        (test_main_check(0u == tst_tile_mask(0, 10, 1, 0x1u, 0u), __LINE__)) &&
        b_ok;

    /* Scores past 99 are clamped to 99. */
    b_ok = (test_main_check(tst_tile_mask(150, 1, 1, 0x1u, 0u) ==
                                tst_tile_mask(99, 1, 1, 0x1u, 0u),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/** One A15 case: a picture on a wall shape and the lit pixels expected. */
typedef struct
{
    score_pic_t pic;
    int32_t     value;
    uint8_t     rows;
    uint8_t     cols;
    uint16_t    active;
    uint32_t    lit;
} tst_pic_case_t;

/**
 * @brief  C19b (A15, DS-09): every wall picture is drawn at the largest
 *         scale that fits each shape (1 x 1 to 3 x 3, strips, a sparse
 *         diagonal), the tile slices add up to the whole drawing, and the
 *         sparse wall shows the whole small picture on each tile.
 *         Digit 3 has 11 pixels, 10 has 20, a big star 14, a small star 5.
 * @return true when every check passed.
 */
bool test_score_font_pictures (void)
{
    static tst_pic_case_t const cases[] = {
        /* Countdown digit: 1 x on 1 x 1 and strips, 3 x on 2 x 2 and 2 x 3,
         * 4 x on 3 x 3; the 3 x 3 diagonal is sparse: 3 small copies. */
        {SCORE_PIC_NUMBER, 3, 1u, 1u, 0x001u, 11u},
        {SCORE_PIC_NUMBER, 3, 1u, 2u, 0x003u, 11u},
        {SCORE_PIC_NUMBER, 3, 2u, 2u, 0x00Fu, 99u},
        {SCORE_PIC_NUMBER, 3, 2u, 3u, 0x03Fu, 99u},
        {SCORE_PIC_NUMBER, 3, 3u, 3u, 0x1FFu, 176u},
        {SCORE_PIC_NUMBER, 3, 1u, 5u, 0x01Fu, 11u},
        {SCORE_PIC_NUMBER, 3, 1u, 9u, 0x1FFu, 11u},
        {SCORE_PIC_NUMBER, 3, 3u, 3u, 0x111u, 33u},
        /* Score 10: 7 x 5, so 1 x on 1 x 1, 2 x on 2 x 2, 3 x on 3 x 3. */
        {SCORE_PIC_NUMBER, 10, 1u, 1u, 0x001u, 20u},
        {SCORE_PIC_NUMBER, 10, 3u, 3u, 0x1FFu, 180u},
        /* 3 stars: small pyramid on 1 x 1 and 1 x 2, big pyramid on 2 x 2
         * (2 x on 3 x 3), a big row on 1 x 3 and 1 x 9. */
        {SCORE_PIC_STARS, 3, 1u, 1u, 0x001u, 15u},
        {SCORE_PIC_STARS, 3, 1u, 2u, 0x003u, 15u},
        {SCORE_PIC_STARS, 3, 2u, 2u, 0x00Fu, 42u},
        {SCORE_PIC_STARS, 3, 3u, 3u, 0x1FFu, 168u},
        {SCORE_PIC_STARS, 3, 1u, 3u, 0x007u, 42u},
        {SCORE_PIC_STARS, 3, 1u, 9u, 0x1FFu, 42u},
        /* 1 and 2 stars, and 0 drawn as 1. */
        {SCORE_PIC_STARS, 1, 1u, 1u, 0x001u, 14u},
        {SCORE_PIC_STARS, 2, 1u, 1u, 0x001u, 10u},
        {SCORE_PIC_STARS, 2, 2u, 2u, 0x00Fu, 28u},
        {SCORE_PIC_STARS, 0, 1u, 1u, 0x001u, 14u},
    };
    bool b_ok = true;

    for (uint32_t index = 0u; index < (sizeof(cases) / sizeof(cases[0]));
         index++)
    {
        tst_pic_case_t const * const p_case = &cases[index];
        uint32_t const wall  = tst_pic_count(p_case->pic, p_case->value,
                                             p_case->rows, p_case->cols,
                                             p_case->active);
        uint32_t const slice = tst_slice_count(p_case->pic, p_case->value,
                                               p_case->rows, p_case->cols,
                                               p_case->active);

        if ((wall != p_case->lit) || (slice != p_case->lit))
        {
            printf("    case %" PRIu32 ": wall %" PRIu32 " slices %" PRIu32
                   " expected %" PRIu32 "\n",
                   index, wall, slice, p_case->lit);
            b_ok = (test_main_check(false, __LINE__)) && b_ok;
        }
        else
        {
            /* As expected. */
        }
    }

    /* Sparse rule: 1 of 4 is sparse; 2 of 4 is not; 4 of 9 is sparse. */
    b_ok = (test_main_check(tst_sparse(2u, 2u, 0x001u), __LINE__)) && b_ok;
    b_ok = (test_main_check(!tst_sparse(2u, 2u, 0x003u), __LINE__)) && b_ok;
    b_ok = (test_main_check(tst_sparse(3u, 3u, 0x00Fu), __LINE__)) && b_ok;

    /* The 3 x 3 countdown digit is centred: 12 x 20 in 24 x 24 starts at
     * column 6, row 2 (the top bar of the 3). */
    b_ok =
        (test_main_check(tst_lit(SCORE_PIC_NUMBER, 3, 3u, 3u, 0x1FFu, 6u, 2u),
                         __LINE__)) &&
        b_ok;
    b_ok =
        (test_main_check(!tst_lit(SCORE_PIC_NUMBER, 3, 3u, 3u, 0x1FFu, 5u, 2u),
                         __LINE__)) &&
        b_ok;

    return b_ok;
}

/** Lit pixels of the whole grid and of the summed tile slices, for cells. */
static void tst_cells_count (score_pic_t pic, int32_t value, uint8_t rows,
                             uint8_t cols, score_cells_t const * p_cells,
                             uint32_t * p_wall, uint32_t * p_slices)
{
    *p_wall   = 0u;
    *p_slices = 0u;

    for (uint32_t pixel_y = 0u; pixel_y < (rows * 8u); pixel_y++)
    {
        for (uint32_t pixel_x = 0u; pixel_x < (cols * 8u); pixel_x++)
        {
            if (score_font_picture_lit(pic, value, rows, cols, p_cells,
                                       pixel_x, pixel_y))
            {
                (*p_wall)++;
            }
        }
    }

    for (uint8_t row = 0u; row < rows; row++)
    {
        for (uint8_t col = 0u; col < cols; col++)
        {
            *p_slices += tst_popcount(score_font_picture_mask(
                pic, value, rows, cols, p_cells, row, col));
        }
    }
}

/**
 * @brief  C19c (step 4d, DS-15): grids of more than 9 cells. A 5 x 5 and a
 *         9 x 9 diagonal are sparse, so each tile shows the small 3 (11
 *         pixels); 9 tiles in one corner of 9 x 9 are sparse too; 9 of the
 *         10 cells of 2 x 5 are dense, so the 3 is drawn big across the
 *         grid and the empty cell gets nothing; a cell bit outside the grid
 *         draws nothing.
 * @return true when every check passed.
 */
bool test_score_font_cells (void)
{
    score_cells_t diag5  = {{0u}};
    score_cells_t diag9  = {{0u}};
    score_cells_t corner = {{0u}};
    score_cells_t dense  = {{0x1Fu, 0x0Fu}}; /* (1, 4) empty. */
    score_cells_t stray  = {{0x01u | 0x20u}}; /* (0, 0) and (0, 5). */
    uint32_t      wall   = 0u;
    uint32_t      slices = 0u;
    bool          b_ok   = true;

    for (uint32_t index = 0u; index < SCORE_FONT_GRID_MAX; index++)
    {
        /* index < 9: the shifts fit uint16_t. */
        diag9.row_bits[index] = (uint16_t)(1u << index);
        /* As above. */
        diag5.row_bits[index] = (index < 5u) ? (uint16_t)(1u << index) : 0u;
        /* Columns 6 to 8: 0x1C0 fits uint16_t. */
        corner.row_bits[index] = (index >= 6u) ? (uint16_t)0x1C0u : 0u;
    }

    b_ok = (test_main_check(score_font_sparse(5u, 5u, &diag5), __LINE__)) &&
           b_ok;
    tst_cells_count(SCORE_PIC_NUMBER, 3, 5u, 5u, &diag5, &wall, &slices);
    b_ok = (test_main_check((55u == wall) && (55u == slices), __LINE__)) &&
           b_ok;
    tst_cells_count(SCORE_PIC_NUMBER, 3, 9u, 9u, &diag9, &wall, &slices);
    b_ok = (test_main_check((99u == wall) && (99u == slices), __LINE__)) &&
           b_ok;
    tst_cells_count(SCORE_PIC_NUMBER, 3, 9u, 9u, &corner, &wall, &slices);
    b_ok = (test_main_check((99u == wall) && (99u == slices), __LINE__)) &&
           b_ok;

    /* Dense: one big 3, more pixels than one small copy, none on (1, 4). */
    b_ok = (test_main_check(!score_font_sparse(2u, 5u, &dense), __LINE__)) &&
           b_ok;
    tst_cells_count(SCORE_PIC_NUMBER, 3, 2u, 5u, &dense, &wall, &slices);
    b_ok = (test_main_check((wall == slices) && (wall > 11u), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(0u == score_font_picture_mask(SCORE_PIC_NUMBER, 3,
                                                          2u, 5u, &dense, 1u,
                                                          4u),
                            __LINE__)) &&
           b_ok;

    /* (0, 5) is outside a 3 x 3 grid: only (0, 0) counts and draws. */
    b_ok = (test_main_check(score_font_sparse(3u, 3u, &stray), __LINE__)) &&
           b_ok;
    tst_cells_count(SCORE_PIC_NUMBER, 3, 3u, 3u, &stray, &wall, &slices);
    b_ok = (test_main_check((11u == wall) && (11u == slices), __LINE__)) &&
           b_ok;

    return b_ok;
}

/*** end of file ***/
