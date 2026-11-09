/**
 * @file    score_font.c
 * @brief   3 x 5 digit font and the per-tile slicing of the final score
 *          (see score_font.h).
 *
 * Owner: Benjamin (Buddy 2). Written for build step 7.
 *
 * Glyph rows are 3-bit values: bit 2 is the left pixel of the row. Glyphs 0
 * to 9 are the digits, glyph 10 is the minus sign.
 */
#include "score_font.h"

#include "proto.h"

/** Glyph index of the minus sign. */
#define SCORE_FONT_MINUS 10u

/** Glyphs in a score: sign, tens, ones (-99 is the longest). */
#define SCORE_FONT_MAX_GLYPHS 3u

/** Glyphs in the font. */
#define SCORE_FONT_GLYPHS 11u

/** Largest score magnitude (STATUS score, -99 to 99). */
#define SCORE_FONT_LIMIT ((int32_t)PROTO_SCORE_MAX)

/** Tiles on a wall at most (layout_table.md section 5.2). */
#define SCORE_FONT_MAX_TILES PROTO_MAP_SLOTS

/** LEDs on one tile. */
#define SCORE_FONT_TILE_LEDS 64u

/** Pixels from one glyph's first column to the next glyph's first column. */
#define SCORE_FONT_PERIOD (SCORE_FONT_GLYPH_W + SCORE_FONT_GAP)

/** A score's glyphs and their count. */
typedef struct
{
    uint8_t  glyph[SCORE_FONT_MAX_GLYPHS];
    uint32_t count;
} score_text_t;

/** The bounding box of the active positions, in tiles. */
typedef struct
{
    uint32_t min_row;
    uint32_t max_row;
    uint32_t min_col;
    uint32_t max_col;
} score_box_t;

/** The font: 11 glyphs of 5 rows of 3 pixels. */
static uint8_t const g_score_glyphs[SCORE_FONT_GLYPHS][SCORE_FONT_GLYPH_H] = {
    {7u, 5u, 5u, 5u, 7u}, /* 0 */
    {2u, 6u, 2u, 2u, 7u}, /* 1 */
    {7u, 1u, 7u, 4u, 7u}, /* 2 */
    {7u, 1u, 7u, 1u, 7u}, /* 3 */
    {5u, 5u, 7u, 1u, 1u}, /* 4 */
    {7u, 4u, 7u, 1u, 7u}, /* 5 */
    {7u, 4u, 7u, 5u, 7u}, /* 6 */
    {7u, 1u, 2u, 2u, 2u}, /* 7 */
    {7u, 5u, 7u, 5u, 7u}, /* 8 */
    {7u, 5u, 7u, 1u, 7u}, /* 9 */
    {0u, 0u, 7u, 0u, 0u}, /* minus */
};

static void     score_font_text (int32_t score, score_text_t * p_text);
static uint32_t score_font_width (uint32_t count);
static bool     score_font_box (uint16_t active, uint8_t cols,
                                uint32_t rows_used, score_box_t * p_box);

/**
 * @brief  Whether wall pixel (pixel_x, pixel_y) is lit by the score text.
 * @param  score    Score, -99 to 99.
 * @param  rows     Rows of the wall, 1 to 9.
 * @param  cols     Columns of the wall, 1 to 9.
 * @param  active   Bit p set when map position p holds a tile.
 * @param  pixel_x  Column of the pixel on the wall.
 * @param  pixel_y  Row of the pixel on the wall.
 * @return true when the pixel is lit.
 */
bool score_font_wall_lit (int32_t score, uint8_t rows, uint8_t cols,
                          uint16_t active, uint32_t pixel_x,
                          uint32_t pixel_y)
{
    score_text_t text;
    score_box_t  box;
    bool         b_lit = false;
    uint32_t const tile_x = pixel_x / SCORE_FONT_TILE_SIDE;
    uint32_t const tile_y = pixel_y / SCORE_FONT_TILE_SIDE;

    score_font_text(score, &text);

    if ((rows >= 1u) && (cols >= 1u) && (rows <= PROTO_MAP_SLOTS) &&
        (cols <= PROTO_MAP_SLOTS) &&
        (pixel_x < (cols * SCORE_FONT_TILE_SIDE)) &&
        (pixel_y < (rows * SCORE_FONT_TILE_SIDE)) &&
        (score_font_box(active, cols, rows, &box)) &&
        (tile_x >= box.min_col) && (tile_x <= box.max_col) &&
        (tile_y >= box.min_row) && (tile_y <= box.max_row) &&
        (0u != (active & (UINT16_C(1) << ((tile_y * cols) + tile_x)))))
    {
        uint32_t const box_w = (box.max_col - box.min_col + 1u) *
                               SCORE_FONT_TILE_SIDE;
        uint32_t const box_h = (box.max_row - box.min_row + 1u) *
                               SCORE_FONT_TILE_SIDE;
        uint32_t const width = score_font_width(text.count);
        uint32_t const origin_x = box.min_col * SCORE_FONT_TILE_SIDE;
        uint32_t const origin_y = box.min_row * SCORE_FONT_TILE_SIDE;
        uint32_t const left = origin_x +
                              ((box_w > width) ? ((box_w - width) / 2u) : 0u);
        uint32_t const top  = origin_y +
                              ((box_h > SCORE_FONT_GLYPH_H)
                                   ? ((box_h - SCORE_FONT_GLYPH_H) / 2u)
                                   : 0u);

        if ((pixel_x >= left) && (pixel_y >= top) &&
            ((pixel_y - top) < SCORE_FONT_GLYPH_H))
        {
            uint32_t const offset = pixel_x - left;
            uint32_t const glyph  = offset / SCORE_FONT_PERIOD;
            uint32_t const within = offset % SCORE_FONT_PERIOD;

            if ((within < SCORE_FONT_GLYPH_W) && (glyph < text.count))
            {
                uint8_t const row_bits =
                    g_score_glyphs[text.glyph[glyph]][pixel_y - top];
                uint32_t const shift = SCORE_FONT_GLYPH_W - 1u - within;

                b_lit = (0u != ((row_bits >> shift) & 1u));
            }
        }
    }

    return b_lit;
}

/**
 * @brief  The 8 x 8 slice of the wall's score drawing for one tile.
 * @param  score   Score, -99 to 99.
 * @param  rows    Rows of the wall, 1 to 9.
 * @param  cols    Columns of the wall, 1 to 9.
 * @param  active  Bit p set when map position p holds a tile.
 * @param  slot    Map position of the tile.
 * @return Bit n set when LED n of the tile is lit; 0 when slot is blank.
 */
uint64_t score_font_tile_mask (int32_t score, uint8_t rows, uint8_t cols,
                               uint16_t active, uint8_t slot)
{
    uint64_t mask = 0u;

    if ((rows >= 1u) && (cols >= 1u) && (rows <= PROTO_MAP_SLOTS) &&
        (cols <= PROTO_MAP_SLOTS) && (rows * cols <= SCORE_FONT_MAX_TILES) &&
        (slot < (rows * cols)) && (0u != (active & (UINT16_C(1) << slot))))
    {
        uint32_t const origin_x = (slot % cols) * SCORE_FONT_TILE_SIDE;
        uint32_t const origin_y = (slot / cols) * SCORE_FONT_TILE_SIDE;

        for (uint32_t led = 0u; led < SCORE_FONT_TILE_LEDS; led++)
        {
            uint32_t const pixel_x =
                origin_x + (led % SCORE_FONT_TILE_SIDE);
            uint32_t const pixel_y =
                origin_y + (led / SCORE_FONT_TILE_SIDE);

            if (score_font_wall_lit(score, rows, cols, active, pixel_x,
                                    pixel_y))
            {
                mask |= (UINT64_C(1) << led);
            }
        }
    }

    return mask;
}

/**
 * @brief  The glyphs of a score: the sign when negative, then the tens when
 *         there are any, then the ones.
 * @param  score   Score, clamped to -99 to 99 here.
 * @param  p_text  Receives the glyphs.
 */
static void score_font_text (int32_t score, score_text_t * p_text)
{
    int32_t  clamped = score;
    uint32_t magnitude = 0u;

    if (clamped > SCORE_FONT_LIMIT)
    {
        clamped = SCORE_FONT_LIMIT;
    }
    else if (clamped < -SCORE_FONT_LIMIT)
    {
        clamped = -SCORE_FONT_LIMIT;
    }
    else
    {
        /* Inside -99 to 99: unchanged. */
    }

    p_text->count = 0u;

    if (clamped < 0)
    {
        p_text->glyph[p_text->count] = SCORE_FONT_MINUS;
        p_text->count++;
        magnitude = (uint32_t)(-clamped); /* Positive, at most 99. */
    }
    else
    {
        magnitude = (uint32_t)clamped; /* Non-negative, at most 99. */
    }

    if (magnitude >= 10u)
    {
        p_text->glyph[p_text->count] = (uint8_t)(magnitude / 10u); /* 1 to 9. */
        p_text->count++;
    }

    p_text->glyph[p_text->count] = (uint8_t)(magnitude % 10u); /* 0 to 9. */
    p_text->count++;
}

/**
 * @brief  Width of a run of glyphs, pixels.
 * @param  count  Glyphs, at least 1.
 * @return Pixels from the first column to the last.
 */
static uint32_t score_font_width (uint32_t count)
{
    return (count * SCORE_FONT_GLYPH_W) + ((count - 1u) * SCORE_FONT_GAP);
}

/**
 * @brief  Bounding box, in tiles, of the active positions of a wall.
 * @param  active      Bit p set when map position p holds a tile.
 * @param  cols        Columns of the wall.
 * @param  rows_used   Rows of the wall.
 * @param  p_box       Receives the box.
 * @return false when no position is active.
 */
static bool score_font_box (uint16_t active, uint8_t cols, uint32_t rows_used,
                            score_box_t * p_box)
{
    bool b_any = false;

    for (uint32_t pos = 0u; pos < (rows_used * cols); pos++)
    {
        if (0u != (active & (UINT16_C(1) << pos)))
        {
            uint32_t const row = pos / cols;
            uint32_t const col = pos % cols;

            if (!b_any)
            {
                p_box->min_row = row;
                p_box->max_row = row;
                p_box->min_col = col;
                p_box->max_col = col;
                b_any          = true;
            }
            else
            {
                p_box->min_row = (row < p_box->min_row) ? row : p_box->min_row;
                p_box->max_row = (row > p_box->max_row) ? row : p_box->max_row;
                p_box->min_col = (col < p_box->min_col) ? col : p_box->min_col;
                p_box->max_col = (col > p_box->max_col) ? col : p_box->max_col;
            }
        }
    }

    return b_any;
}

/*** end of file ***/
