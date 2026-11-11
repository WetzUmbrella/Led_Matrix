/**
 * @file    score_font.h
 * @brief   Final game score drawn across the whole wall (whack-a-mole
 *          re-plan: "the Pico draws the final score across the wall"). A
 *          3 x 5 digit font, centred on the wall, cut into one 64-bit mask per
 *          tile for WALL_PICTURE. Pure C with no hardware call.
 *
 * Owner: Benjamin (Buddy 2). Written for build step 7.
 *
 * - Pixel n of a tile is LED n = 8 x row + column, the asset frame order of
 *   layout_table.md section 6, so bit n of the mask is that LED before the
 *   tile's rotation.
 * - The wall is rows x cols tiles, row-major over the map (layout_table.md
 *   section 3, SESSION_CONFIG). A tile at position p is row p / cols and
 *   column p % cols. Positions that hold no Node ID (blank) draw nothing.
 * - The text is centred on the box that holds the active tiles, and only
 *   lit pixels on active tiles are drawn ("over the active slots").
 * - Text wider than the box (a 1 x 1 tile for a score of -10 or lower) is
 *   clipped on the right; the sign is kept first.
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

/**
 * @brief  Whether wall pixel (x, y) is lit by the score text.
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
