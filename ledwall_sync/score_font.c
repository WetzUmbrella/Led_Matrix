/**
 * @file    score_font.c
 * @brief   The wall pictures: a 3 x 5 digit font and two star sizes, scaled
 *          to the wall and cut per tile (see score_font.h).
 *
 * Owner: Benjamin (Buddy 2). Written for build step 7; scaled pictures and
 * stars for the step 3 game (DS-09).
 *
 * A picture is first drawn small into a source bitmap (score_src_t), then
 * placed on the wall: the largest whole-number scale that fits the box of
 * the active tiles, centred in it (score_layout_t). Source rows are bit
 * fields: bit (width - 1 - x) is column x. Glyph rows work the same way:
 * bit 2 is the left pixel of a digit row.
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

/** Star sizes: big 5 x 5, small 3 x 3; one pixel between stars. */
#define SCORE_STAR_BIG   5u
#define SCORE_STAR_SMALL 3u
#define SCORE_STAR_GAP   1u

/** Source bitmap rows at most (the big star pyramid is 11 x 11). */
#define SCORE_SRC_ROWS 16u

/** Star arrangements tried for one star size: a row, a pyramid. */
#define SCORE_STAR_LAYOUTS 2u

/** A picture drawn small: rows of bits, and its size in pixels. */
typedef struct
{
    uint32_t row[SCORE_SRC_ROWS];
    uint32_t width;
    uint32_t height;
} score_src_t;

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

/** Where a picture lands on the wall. */
typedef struct
{
    bool        b_valid;  /**< The wall's shape and active tiles are good. */
    bool        b_sparse; /**< Each tile shows the whole small picture. */
    uint8_t     rows;
    uint8_t     cols;
    uint16_t    active;
    score_box_t box;      /**< Active tiles (dense walls). */
    score_src_t src;      /**< The picture drawn small. */
    uint32_t    scale;    /**< Each source pixel is scale x scale LEDs. */
    uint32_t    left;     /**< First wall column (tile column if sparse). */
    uint32_t    top;      /**< First wall row (tile row if sparse). */
} score_layout_t;

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

/** The big star (5 x 5) and the small star (3 x 3), rows from the top. */
static uint8_t const g_star_big[SCORE_STAR_BIG] = {
    0x04u, /* ..#.. */
    0x0Eu, /* .###. */
    0x1Fu, /* ##### */
    0x0Eu, /* .###. */
    0x0Au, /* .#.#. */
};
static uint8_t const g_star_small[SCORE_STAR_SMALL] = {
    0x2u, /* .#. */
    0x7u, /* ### */
    0x2u, /* .#. */
};

static void     score_font_text (int32_t score, score_text_t * p_text);
static uint32_t score_font_width (uint32_t count);
static bool     score_font_box (uint16_t active, uint8_t cols,
                                uint32_t rows_used, score_box_t * p_box);
static void     score_src_clear (score_src_t * p_src, uint32_t width,
                                 uint32_t height);
static void     score_src_stamp (score_src_t * p_src, uint8_t const * p_rows,
                                 uint32_t side, uint32_t pos_x,
                                 uint32_t pos_y);
static void     score_src_number (int32_t value, score_src_t * p_src);
static void     score_src_stars (uint32_t count, uint32_t side,
                                 bool b_pyramid, score_src_t * p_src);
static uint32_t score_src_fit (score_src_t const * p_src, uint32_t box_w,
                               uint32_t box_h);
static void     score_src_choose (score_pic_t pic, int32_t value,
                                  uint32_t box_w, uint32_t box_h,
                                  score_src_t * p_src, uint32_t * p_scale);
static void     score_layout_make (score_pic_t pic, int32_t value,
                                   uint8_t rows, uint8_t cols, uint16_t active,
                                   score_layout_t * p_layout);
static bool     score_layout_lit (score_layout_t const * p_layout,
                                  uint32_t pixel_x, uint32_t pixel_y);
static uint32_t score_popcount (uint16_t bits);

/**
 * @brief  Whether wall pixel (pixel_x, pixel_y) is lit by a picture.
 * @param  pic      What to draw.
 * @param  value    The number, or the star count.
 * @param  rows     Rows of the wall, 1 to 9.
 * @param  cols     Columns of the wall, 1 to 9.
 * @param  active   Bit p set when map position p holds a tile.
 * @param  pixel_x  Column of the pixel on the wall.
 * @param  pixel_y  Row of the pixel on the wall.
 * @return true when the pixel is lit.
 */
bool score_font_picture_lit (score_pic_t pic, int32_t value, uint8_t rows,
                             uint8_t cols, uint16_t active, uint32_t pixel_x,
                             uint32_t pixel_y)
{
    score_layout_t layout;

    score_layout_make(pic, value, rows, cols, active, &layout);

    return score_layout_lit(&layout, pixel_x, pixel_y);
}

/**
 * @brief  The 8 x 8 slice of a wall picture for one tile. The layout is
 *         worked out once for the 64 LEDs.
 * @param  pic     What to draw.
 * @param  value   The number, or the star count.
 * @param  rows    Rows of the wall, 1 to 9.
 * @param  cols    Columns of the wall, 1 to 9.
 * @param  active  Bit p set when map position p holds a tile.
 * @param  slot    Map position of the tile.
 * @return Bit n set when LED n of the tile is lit; 0 when slot is blank.
 */
uint64_t score_font_picture_mask (score_pic_t pic, int32_t value, uint8_t rows,
                                  uint8_t cols, uint16_t active, uint8_t slot)
{
    score_layout_t layout;
    uint64_t       mask = 0u;

    score_layout_make(pic, value, rows, cols, active, &layout);

    if ((layout.b_valid) && (slot < (rows * cols)) &&
        (0u != (active & (UINT16_C(1) << slot))))
    {
        uint32_t const origin_x = (slot % cols) * SCORE_FONT_TILE_SIDE;
        uint32_t const origin_y = (slot / cols) * SCORE_FONT_TILE_SIDE;

        for (uint32_t led = 0u; led < SCORE_FONT_TILE_LEDS; led++)
        {
            uint32_t const pixel_x =
                origin_x + (led % SCORE_FONT_TILE_SIDE);
            uint32_t const pixel_y =
                origin_y + (led / SCORE_FONT_TILE_SIDE);

            if (score_layout_lit(&layout, pixel_x, pixel_y))
            {
                mask |= (UINT64_C(1) << led);
            }
        }
    }

    return mask;
}

/**
 * @brief  Whether the wall is sparse (fewer than half its positions hold an
 *         active tile).
 * @param  rows    Rows of the wall, 1 to 9.
 * @param  cols    Columns of the wall, 1 to 9.
 * @param  active  Bit p set when map position p holds a tile.
 * @return true when sparse.
 */
bool score_font_sparse (uint8_t rows, uint8_t cols, uint16_t active)
{
    uint32_t const cells = (uint32_t)rows * cols; /* At most 81. */
    /* Only the wall's own positions count; cells < 16 here or none. */
    uint16_t const own = (cells < 16u)
                             ? (uint16_t)(active & ((UINT16_C(1) << cells) -
                                                    1u))
                             : active;

    return ((2u * score_popcount(own)) < cells);
}

/**
 * @brief  Whether wall pixel (pixel_x, pixel_y) is lit by the score.
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
    return score_font_picture_lit(SCORE_PIC_NUMBER, score, rows, cols, active,
                                  pixel_x, pixel_y);
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
    return score_font_picture_mask(SCORE_PIC_NUMBER, score, rows, cols, active,
                                   slot);
}

/**
 * @brief  Work out where a picture lands: the box it is centred on (the
 *         active tiles, or one tile when sparse), its source bitmap, its
 *         scale and its top-left pixel.
 * @param  pic       What to draw.
 * @param  value     The number, or the star count.
 * @param  rows      Rows of the wall.
 * @param  cols      Columns of the wall.
 * @param  active    Bit p set when map position p holds a tile.
 * @param  p_layout  Receives the layout; b_valid false on a bad wall.
 */
static void score_layout_make (score_pic_t pic, int32_t value, uint8_t rows,
                               uint8_t cols, uint16_t active,
                               score_layout_t * p_layout)
{
    p_layout->b_valid = (rows >= 1u) && (cols >= 1u) &&
                        ((rows * cols) <= SCORE_FONT_MAX_TILES) &&
                        (score_font_box(active, cols, rows, &p_layout->box));
    p_layout->rows   = rows;
    p_layout->cols   = cols;
    p_layout->active = active;
    p_layout->scale  = 1u;
    p_layout->left   = 0u;
    p_layout->top    = 0u;
    score_src_clear(&p_layout->src, 1u, 1u);

    if (p_layout->b_valid)
    {
        uint32_t origin_x = 0u;
        uint32_t origin_y = 0u;
        uint32_t box_w    = SCORE_FONT_TILE_SIDE;
        uint32_t box_h    = SCORE_FONT_TILE_SIDE;

        p_layout->b_sparse = score_font_sparse(rows, cols, active);

        if (!p_layout->b_sparse)
        {
            box_w = (p_layout->box.max_col - p_layout->box.min_col + 1u) *
                    SCORE_FONT_TILE_SIDE;
            box_h = (p_layout->box.max_row - p_layout->box.min_row + 1u) *
                    SCORE_FONT_TILE_SIDE;
            origin_x = p_layout->box.min_col * SCORE_FONT_TILE_SIDE;
            origin_y = p_layout->box.min_row * SCORE_FONT_TILE_SIDE;
        }
        else
        {
            /* Each tile alone: the box is one tile, at the tile's origin. */
        }

        score_src_choose(pic, value, box_w, box_h, &p_layout->src,
                         &p_layout->scale);

        {
            uint32_t const width  = p_layout->src.width * p_layout->scale;
            uint32_t const height = p_layout->src.height * p_layout->scale;

            p_layout->left = origin_x +
                             ((box_w > width) ? ((box_w - width) / 2u) : 0u);
            p_layout->top  = origin_y +
                             ((box_h > height) ? ((box_h - height) / 2u) : 0u);
        }
    }
    else
    {
        p_layout->b_sparse = false;
    }
}

/**
 * @brief  Whether a wall pixel is lit under a layout.
 * @param  p_layout  From score_layout_make().
 * @param  pixel_x   Column of the pixel on the wall.
 * @param  pixel_y   Row of the pixel on the wall.
 * @return true when the pixel is lit.
 */
static bool score_layout_lit (score_layout_t const * p_layout,
                              uint32_t pixel_x, uint32_t pixel_y)
{
    uint32_t const tile_x = pixel_x / SCORE_FONT_TILE_SIDE;
    uint32_t const tile_y = pixel_y / SCORE_FONT_TILE_SIDE;
    bool           b_lit  = false;

    if ((p_layout->b_valid) && (tile_x < p_layout->cols) &&
        (tile_y < p_layout->rows) &&
        (0u != (p_layout->active &
                (UINT16_C(1) << ((tile_y * p_layout->cols) + tile_x)))))
    {
        uint32_t pos_x = pixel_x;
        uint32_t pos_y = pixel_y;

        if (p_layout->b_sparse)
        {
            pos_x = pixel_x % SCORE_FONT_TILE_SIDE; /* Within its tile. */
            pos_y = pixel_y % SCORE_FONT_TILE_SIDE;
        }
        else
        {
            /* Wall coordinates. */
        }

        if ((pos_x >= p_layout->left) && (pos_y >= p_layout->top))
        {
            uint32_t const src_x = (pos_x - p_layout->left) / p_layout->scale;
            uint32_t const src_y = (pos_y - p_layout->top) / p_layout->scale;

            if ((src_x < p_layout->src.width) &&
                (src_y < p_layout->src.height))
            {
                uint32_t const shift = p_layout->src.width - 1u - src_x;

                b_lit = (0u != ((p_layout->src.row[src_y] >> shift) & 1u));
            }
        }
    }

    return b_lit;
}

/**
 * @brief  The source bitmap and scale of a picture for a box: numbers at
 *         the largest scale that fits; stars in the arrangement drawn
 *         largest, big stars first, small stars when no big one fits.
 *         Nothing fits: scale 1, clipped.
 * @param  pic      What to draw.
 * @param  value    The number, or the star count.
 * @param  box_w    Box width, pixels.
 * @param  box_h    Box height, pixels.
 * @param  p_src    Receives the bitmap.
 * @param  p_scale  Receives the scale, at least 1.
 */
static void score_src_choose (score_pic_t pic, int32_t value, uint32_t box_w,
                              uint32_t box_h, score_src_t * p_src,
                              uint32_t * p_scale)
{
    uint32_t scale = 0u;

    if (SCORE_PIC_STARS == pic)
    {
        /* 1 to 3 stars; value is clamped first, so the narrowing keeps it. */
        uint32_t const count =
            (value < 1) ? 1u
                        : ((value > (int32_t)SCORE_FONT_STARS_MAX)
                               ? SCORE_FONT_STARS_MAX
                               : (uint32_t)value);
        uint32_t const sides[2] = {SCORE_STAR_BIG, SCORE_STAR_SMALL};
        uint32_t       best     = 0u; /* Drawn area of the best so far. */

        for (uint32_t size = 0u; (size < 2u) && (0u == best); size++)
        {
            for (uint32_t kind = 0u; kind < SCORE_STAR_LAYOUTS; kind++)
            {
                score_src_t    candidate;
                bool const     b_pyramid = (1u == kind);
                uint32_t       fit       = 0u;
                uint32_t       area      = 0u;

                score_src_stars(count, sides[size], b_pyramid, &candidate);
                fit  = score_src_fit(&candidate, box_w, box_h);
                area = (candidate.width * fit) * (candidate.height * fit);

                if (area > best)
                {
                    best   = area;
                    scale  = fit;
                    *p_src = candidate;
                }
            }
        }

        if (0u == best)
        {
            score_src_stars(count, SCORE_STAR_SMALL, false, p_src);
        }
    }
    else
    {
        score_src_number(value, p_src);
        scale = score_src_fit(p_src, box_w, box_h);
    }

    *p_scale = (0u == scale) ? 1u : scale;
}

/**
 * @brief  Largest whole-number scale at which a bitmap fits a box.
 * @param  p_src  The bitmap.
 * @param  box_w  Box width, pixels.
 * @param  box_h  Box height, pixels.
 * @return The scale, 0 when it does not fit at 1.
 */
static uint32_t score_src_fit (score_src_t const * p_src, uint32_t box_w,
                               uint32_t box_h)
{
    uint32_t const fit_w = box_w / p_src->width;
    uint32_t const fit_h = box_h / p_src->height;

    return (fit_w < fit_h) ? fit_w : fit_h;
}

/**
 * @brief  A blank source bitmap of a size.
 * @param  p_src   The bitmap.
 * @param  width   Pixels, 1 to 32.
 * @param  height  Pixels, 1 to SCORE_SRC_ROWS.
 */
static void score_src_clear (score_src_t * p_src, uint32_t width,
                             uint32_t height)
{
    for (uint32_t row = 0u; row < SCORE_SRC_ROWS; row++)
    {
        p_src->row[row] = 0u;
    }

    p_src->width  = width;
    p_src->height = height;
}

/**
 * @brief  Draw a square glyph (rows of side bits, bit side - 1 the left
 *         pixel) into a bitmap with its top-left at (pos_x, pos_y).
 * @param  p_src   The bitmap; the glyph must lie inside it.
 * @param  p_rows  The glyph's rows.
 * @param  side    Glyph side, pixels.
 * @param  pos_x   Left column.
 * @param  pos_y   Top row.
 */
static void score_src_stamp (score_src_t * p_src, uint8_t const * p_rows,
                             uint32_t side, uint32_t pos_x, uint32_t pos_y)
{
    uint32_t const shift = p_src->width - side - pos_x;

    for (uint32_t row = 0u; row < side; row++)
    {
        /* Widened before the shift; the glyph lies inside the bitmap. */
        p_src->row[pos_y + row] |= ((uint32_t)p_rows[row] << shift);
    }
}

/**
 * @brief  A number drawn small: its glyphs left to right, one pixel apart.
 * @param  value  The number, clamped to -99 to 99.
 * @param  p_src  Receives the bitmap.
 */
static void score_src_number (int32_t value, score_src_t * p_src)
{
    score_text_t text;

    score_font_text(value, &text);
    score_src_clear(p_src, score_font_width(text.count), SCORE_FONT_GLYPH_H);

    for (uint32_t glyph = 0u; glyph < text.count; glyph++)
    {
        uint32_t const shift = p_src->width - SCORE_FONT_GLYPH_W -
                               (glyph * SCORE_FONT_PERIOD);

        for (uint32_t row = 0u; row < SCORE_FONT_GLYPH_H; row++)
        {
            /* Widened before the shift: at most 11 pixels wide. */
            p_src->row[row] |=
                ((uint32_t)g_score_glyphs[text.glyph[glyph]][row] << shift);
        }
    }
}

/**
 * @brief  Stars drawn small: in a row, or (3 stars) a pyramid with one star
 *         on top of two. A pyramid of fewer stars is a row.
 * @param  count      1 to 3.
 * @param  side       SCORE_STAR_BIG or SCORE_STAR_SMALL.
 * @param  b_pyramid  true for the pyramid.
 * @param  p_src      Receives the bitmap.
 */
static void score_src_stars (uint32_t count, uint32_t side, bool b_pyramid,
                             score_src_t * p_src)
{
    uint8_t const * const p_rows = (SCORE_STAR_BIG == side) ? g_star_big
                                                            : g_star_small;
    uint32_t const        period = side + SCORE_STAR_GAP;

    if ((b_pyramid) && (SCORE_FONT_STARS_MAX == count))
    {
        score_src_clear(p_src, (2u * side) + SCORE_STAR_GAP,
                        (2u * side) + SCORE_STAR_GAP);
        score_src_stamp(p_src, p_rows, side, period / 2u, 0u);
        score_src_stamp(p_src, p_rows, side, 0u, period);
        score_src_stamp(p_src, p_rows, side, period, period);
    }
    else
    {
        score_src_clear(p_src, (count * period) - SCORE_STAR_GAP, side);

        for (uint32_t star = 0u; star < count; star++)
        {
            score_src_stamp(p_src, p_rows, side, star * period, 0u);
        }
    }
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

/**
 * @brief  Set bits of a 16-bit value.
 * @param  bits  The value.
 * @return The count.
 */
static uint32_t score_popcount (uint16_t bits)
{
    uint32_t count = 0u;

    for (uint32_t bit = 0u; bit < 16u; bit++)
    {
        if (0u != (bits & (UINT16_C(1) << bit)))
        {
            count++;
        }
    }

    return count;
}

/*** end of file ***/
