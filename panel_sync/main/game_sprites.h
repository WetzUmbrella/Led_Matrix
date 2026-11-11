/**
 * @file    game_sprites.h
 * @brief   The game's 8 x 8 drawings (DS-11): the mole rising out of its hole
 *          and sinking back, the hit (squash, then a green star burst), the
 *          wrong tap (a red X that blinks twice), the start sprite (a green
 *          play triangle that pulses), the hole, the wall pictures and the
 *          sparkle. Each frame is computed from an elapsed time, so drawing
 *          never blocks and every tile draws the same frame at the same
 *          shared time. Pure C with no hardware call.
 *
 * Owner: Jia Jun (Buddy 5) for the display, per the re-plan's report
 * allocation. Written for build step 3.
 *
 * - The canvas is upright: pixel n is row n / 8 (row 0 at the top) and
 *   column n % 8, the asset frame order of layout_table.md section 6. The
 *   caller turns it to the tile's rotation (display_frame_source_led()).
 * - Every channel is 0 to 8 (the wall's brightness limit).
 */
#ifndef GAME_SPRITES_H
#define GAME_SPRITES_H

#include <stdbool.h>
#include <stdint.h>

/** Pixels on the canvas and its side. */
#define GAME_SPRITE_PIXELS 64u
#define GAME_SPRITE_SIDE   8u

/** The mole rises 2 rows every 40 ms (160 ms in all) and sinks the same. */
#define GAME_SPRITE_ROW_STEP_US INT64_C(40000)
#define GAME_SPRITE_RISE_US     INT64_C(160000)

/** Hit: 2 squash frames of 40 ms, then 3 burst frames, 250 ms in all. */
#define GAME_SPRITE_SQUASH_US INT64_C(80000)
#define GAME_SPRITE_HIT_US    INT64_C(250000)

/** Wrong tap: a red X that blinks twice, 300 ms in all. */
#define GAME_SPRITE_WRONG_US INT64_C(300000)

/** Start sprite: one pulse (half to full brightness and back) a second. */
#define GAME_SPRITE_PULSE_US INT64_C(1000000)

/** Sparkle: a new twinkle pattern every 80 ms. */
#define GAME_SPRITE_TWINKLE_US INT64_C(80000)

/** One pixel, channels 0 to 8. */
typedef struct
{
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} game_rgb_t;

/** An upright 8 x 8 drawing. */
typedef struct
{
    game_rgb_t px[GAME_SPRITE_PIXELS];
} game_canvas_t;

/**
 * @brief  Clear the canvas to dark.
 * @param  p_canvas  Canvas; must not be NULL.
 */
void game_sprites_clear (game_canvas_t * p_canvas);

/**
 * @brief  The hole: a dim purple arc on the bottom two rows.
 * @param  p_canvas  Canvas; must not be NULL.
 */
void game_sprites_hole (game_canvas_t * p_canvas);

/**
 * @brief  The mole with its top rows_up rows out of the hole (0 to 8),
 *         drawn over the hole: brown body, white eyes, pink nose.
 * @param  p_canvas  Canvas; must not be NULL.
 * @param  rows_up   Rows shown, 0 (hidden) to 8 (fully up).
 */
void game_sprites_mole (game_canvas_t * p_canvas, uint32_t rows_up);

/**
 * @brief  Rows of the mole out of the hole a time after it starts rising:
 *         2 at once, 2 more every GAME_SPRITE_ROW_STEP_US, then 8.
 * @param  elapsed_us  Time since the show time; negative: 0 rows.
 * @return 0 to 8.
 */
uint32_t game_sprites_rows_up (int64_t elapsed_us);

/**
 * @brief  The start sprite: a green play triangle pointing right, pulsing
 *         between half and full brightness once a second.
 * @param  p_canvas  Canvas; must not be NULL.
 * @param  t_us      Shared time, microseconds (the pulse phase).
 */
void game_sprites_start (game_canvas_t * p_canvas, int64_t t_us);

/**
 * @brief  The hit, elapsed_us after the tap was judged: the mole squashes
 *         (only when b_squash, a mole was up), then a green star burst with
 *         a yellow centre grows in 3 frames. Draws over a cleared canvas.
 * @param  p_canvas    Canvas; must not be NULL.
 * @param  elapsed_us  0 to GAME_SPRITE_HIT_US.
 * @param  b_squash    true when a mole was hit (not the start sprite or a
 *                     tap test).
 */
void game_sprites_hit (game_canvas_t * p_canvas, int64_t elapsed_us,
                      bool b_squash);

/**
 * @brief  The wrong tap, elapsed_us after it was judged: a red X over the
 *         canvas, on for 75 ms, off for 75 ms, twice.
 * @param  p_canvas    Canvas; must not be NULL.
 * @param  elapsed_us  0 to GAME_SPRITE_WRONG_US.
 */
void game_sprites_wrong (game_canvas_t * p_canvas, int64_t elapsed_us);

/**
 * @brief  A wall picture piece: the lit bits of mask in one colour, the rest
 *         dark. Bit n is canvas pixel n.
 * @param  p_canvas  Canvas; must not be NULL.
 * @param  mask      The tile's 8 x 8 piece.
 * @param  colour    proto_colour_t value.
 */
void game_sprites_picture (game_canvas_t * p_canvas, uint64_t mask,
                          uint8_t colour);

/**
 * @brief  The sparkle (the end of round animation): about one pixel in six
 *         lit in the colour, a new pattern every GAME_SPRITE_TWINKLE_US.
 * @param  p_canvas  Canvas; must not be NULL.
 * @param  colour    proto_colour_t value.
 * @param  t_us      Shared time, microseconds.
 */
void game_sprites_sparkle (game_canvas_t * p_canvas, uint8_t colour,
                          int64_t t_us);

/**
 * @brief  The channels of a wall picture colour (layout_table.md 5.13).
 * @param  colour  proto_colour_t value; an unknown value gives white.
 * @return The colour, channels 0 to 8.
 */
game_rgb_t game_sprites_colour (uint8_t colour);

#endif /* GAME_SPRITES_H */

/*** end of file ***/
