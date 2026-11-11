/**
 * @file    score_font.h
 * @brief   The wall pictures (DS-09, A15): numbers (the countdown digits and
 *          the final score) and the result stars, drawn across the whole
 *          wall at the largest whole-number scale that fits, and cut into
 *          one 64-bit mask per tile for WALL_PICTURE. Pure C with no
 *          hardware call.
 *
 * Owner: Benjamin (Buddy 2). Written for build step 7; scaled pictures and
 * stars for the step 3 game (DS-09).
 *
 * - Pixel n of a tile is LED n = 8 x row + column, the asset frame order of
 *   layout_table.md section 6, so bit n of the mask is that LED before the
 *   tile's rotation.
 * - The wall is rows x cols tiles, row-major over the map (layout_table.md
 *   section 3, SESSION_CONFIG). A tile at position p is row p / cols and
 *   column p % cols. Positions that hold no Node ID (blank) draw nothing.
 * - The picture is centred on the box that holds the active tiles, at the
 *   largest whole-number scale that fits the box; only lit pixels on active
 *   tiles are drawn. Numbers use a 3 x 5 font (1 x 1: 3 x 5, two digits 7
 *   px wide; 2 x 2: a digit at 3 x; 3 x 3: at 4 x).
 * - Stars: 1 to 3 (a value below 1 draws 1 star), 5 x 5 stars in a row or
 *   in a pyramid (1 on top, 2 below), whichever is drawn larger; when
 *   neither fits (1 x 1, 1 x 2) 3 x 3 stars in the same two arrangements.
 * - Sparse wall (A15, DS-15): when the active tiles cover less than half of
 *   the wall's positions, each active tile shows the whole picture as drawn
 *   for one tile (1 x 1), so every tile is readable on its own.
 * - Text wider than the box is clipped on the right; the sign is kept first.
 */
#ifndef SCORE_FONT_H
#define SCORE_FONT_H

#include <stdbool.h>
#include <stdint.h>

/** Glyph size in pixels, and the gap between glyphs (plan choice). */
#define SCORE_FONT_GLYPH_W 3u
#define SCORE_FONT_GLYPH_H 5u
#define SCORE_FONT_GAP     1u

/** Tile side in LEDs (8 x 8). */
#define SCORE_FONT_TILE_SIDE 8u

/** Stars at most (3 stars, DS-07). */
#define SCORE_FONT_STARS_MAX 3u

/** What a wall picture shows. */
typedef enum
{
    SCORE_PIC_NUMBER = 0, /**< A number, -99 to 99 (countdown, score). */
    SCORE_PIC_STARS,      /**< 1 to 3 stars (the result). */
    SCORE_PIC_COUNT
} score_pic_t;

/**
 * @brief  Whether wall pixel (x, y) is lit by a picture.
 * @param  pic      What to draw.
 * @param  value    The number, or the star count.
 * @param  rows     Rows of the wall, 1 to 9.
 * @param  cols     Columns of the wall, 1 to 9.
 * @param  active   Bit p set when map position p holds a tile.
 * @param  pixel_x  Column of the pixel on the wall, 0 to cols x 8 - 1.
 * @param  pixel_y  Row of the pixel on the wall, 0 to rows x 8 - 1.
 * @return true when the pixel is lit.
 */
bool score_font_picture_lit (score_pic_t pic, int32_t value, uint8_t rows,
                             uint8_t cols, uint16_t active, uint32_t pixel_x,
                             uint32_t pixel_y);

/**
 * @brief  The 8 x 8 slice of a wall picture for one tile.
 * @param  pic     What to draw.
 * @param  value   The number, or the star count.
 * @param  rows    Rows of the wall, 1 to 9.
 * @param  cols    Columns of the wall, 1 to 9.
 * @param  active  Bit p set when map position p holds a tile.
 * @param  slot    Map position of the tile, 0 to rows x cols - 1.
 * @return Bit n set when LED n of the tile is lit; 0 when slot is blank.
 */
uint64_t score_font_picture_mask (score_pic_t pic, int32_t value, uint8_t rows,
                                  uint8_t cols, uint16_t active, uint8_t slot);

/**
 * @brief  Whether the wall is sparse: its active tiles cover less than half
 *         of its positions, so each tile shows the whole small picture.
 * @param  rows    Rows of the wall, 1 to 9.
 * @param  cols    Columns of the wall, 1 to 9.
 * @param  active  Bit p set when map position p holds a tile.
 * @return true when sparse.
 */
bool score_font_sparse (uint8_t rows, uint8_t cols, uint16_t active);

/**
 * @brief  Whether wall pixel (x, y) is lit by the score (a number picture).
 * @param  score   Score, -99 to 99.
 * @param  rows    Rows of the wall, 1 to 9.
 * @param  cols    Columns of the wall, 1 to 9.
 * @param  active  Bit p set when map position p holds a tile.
 * @param  x       Column of the pixel on the wall, 0 to cols x 8 - 1.
 * @param  y       Row of the pixel on the wall, 0 to rows x 8 - 1.
 * @return true when the pixel is lit.
 */
bool score_font_wall_lit (int32_t score, uint8_t rows, uint8_t cols,
                          uint16_t active, uint32_t pixel_x,
                          uint32_t pixel_y);

/**
 * @brief  The 8 x 8 slice of the wall's score drawing for one tile.
 * @param  score   Score, -99 to 99.
 * @param  rows    Rows of the wall, 1 to 9.
 * @param  cols    Columns of the wall, 1 to 9.
 * @param  active  Bit p set when map position p holds a tile.
 * @param  slot    Map position of the tile, 0 to rows x cols - 1.
 * @return Bit n set when LED n of the tile is lit; 0 when slot is blank.
 */
uint64_t score_font_tile_mask (int32_t score, uint8_t rows, uint8_t cols,
                               uint16_t active, uint8_t slot);

#endif /* SCORE_FONT_H */

/*** end of file ***/
