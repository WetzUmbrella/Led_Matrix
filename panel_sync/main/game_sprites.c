/**
 * @file    game_sprites.c
 * @brief   The game's 8 x 8 drawings (see game_sprites.h).
 *
 * Owner: Jia Jun (Buddy 5). Written for build step 3.
 *
 * Sprite rows are strings of 8 characters, one per pixel, row 0 at the top:
 * '.' dark, and one letter per colour of that sprite. Proposed 11 Nov 2026
 * (DS-11), tuned on the real tiles in step 3.
 */
#include "game_sprites.h"

#include "proto.h"

/** The mole: B body, W eyes, P nose. */
static char const * const g_mole_rows[GAME_SPRITE_SIDE] = {
    "..BBBB..",
    ".BBBBBB.",
    ".BWBBWB.",
    ".BBPPBB.",
    "BBBBBBBB",
    "BBBBBBBB",
    "BBBBBBBB",
    "BBBBBBBB",
};

/** The hole: H on the bottom two rows. */
static char const * const g_hole_rows[2] = {
    ".HHHHHH.",
    "HHHHHHHH",
};

/** Sprite colours, channels 0 to 8. */
static game_rgb_t const g_body  = {5u, 2u, 0u}; /* Brown. */
static game_rgb_t const g_eye   = {6u, 6u, 6u}; /* White. */
static game_rgb_t const g_nose  = {8u, 3u, 4u}; /* Pink. */
static game_rgb_t const g_hole  = {1u, 0u, 2u}; /* Dim purple. */
static game_rgb_t const g_green = {0u, 8u, 0u};
static game_rgb_t const g_gold  = {8u, 6u, 0u}; /* The burst's centre. */
static game_rgb_t const g_red   = {8u, 0u, 0u};

/** Wall picture colours (layout_table.md 5.14), in code order. */
static game_rgb_t const g_colours[PROTO_COLOUR_COUNT] = {
    {4u, 4u, 4u}, /* White, at the v1 score's half value. */
    {7u, 5u, 0u}, /* Yellow. */
    {0u, 7u, 0u}, /* Green. */
    {7u, 0u, 0u}, /* Red. */
    {0u, 1u, 8u}, /* Blue. */
    {8u, 3u, 0u}, /* Orange. */
    {5u, 0u, 6u}, /* Purple. */
    {0u, 6u, 6u}, /* Cyan. */
};

/** The start triangle: half and full brightness of its green. */
#define GS_START_LOW  4u
#define GS_START_HIGH 8u

/** Wrong tap: each half blink, microseconds. */
#define GS_BLINK_US INT64_C(75000)

/** Burst frames after the squash. */
#define GS_BURST_FRAMES 3

/** Sparkle: one pixel in GS_SPARKLE_ONE_IN is lit. */
#define GS_SPARKLE_ONE_IN 6u

static void     gs_set (game_canvas_t * p_canvas, uint32_t row, uint32_t col,
                        game_rgb_t colour);
static uint32_t gs_abs_offset (uint32_t pos);
static uint32_t gs_hash (uint32_t value);

/**
 * @brief  Clear the canvas to dark.
 * @param  p_canvas  Canvas.
 */
void game_sprites_clear (game_canvas_t * p_canvas)
{
    game_rgb_t const dark = {0u, 0u, 0u};

    for (uint32_t pixel = 0u; pixel < GAME_SPRITE_PIXELS; pixel++)
    {
        p_canvas->px[pixel] = dark;
    }
}

/**
 * @brief  The hole on the bottom two rows.
 * @param  p_canvas  Canvas.
 */
void game_sprites_hole (game_canvas_t * p_canvas)
{
    for (uint32_t row = 0u; row < 2u; row++)
    {
        for (uint32_t col = 0u; col < GAME_SPRITE_SIDE; col++)
        {
            if ('H' == g_hole_rows[row][col])
            {
                gs_set(p_canvas, GAME_SPRITE_SIDE - 2u + row, col, g_hole);
            }
        }
    }
}

/**
 * @brief  The mole with its top rows_up rows out of the hole: sprite row r
 *         is drawn on canvas row r + 8 - rows_up.
 * @param  p_canvas  Canvas.
 * @param  rows_up   0 to 8.
 */
void game_sprites_mole (game_canvas_t * p_canvas, uint32_t rows_up)
{
    uint32_t const shown = (rows_up > GAME_SPRITE_SIDE) ? GAME_SPRITE_SIDE
                                                        : rows_up;
    uint32_t const drop  = GAME_SPRITE_SIDE - shown;

    for (uint32_t row = 0u; row < shown; row++)
    {
        for (uint32_t col = 0u; col < GAME_SPRITE_SIDE; col++)
        {
            char const pixel = g_mole_rows[row][col];

            if ('B' == pixel)
            {
                gs_set(p_canvas, row + drop, col, g_body);
            }
            else if ('W' == pixel)
            {
                gs_set(p_canvas, row + drop, col, g_eye);
            }
            else if ('P' == pixel)
            {
                gs_set(p_canvas, row + drop, col, g_nose);
            }
            else
            {
                /* '.': the canvas below shows through. */
            }
        }
    }
}

/**
 * @brief  Rows out of the hole a time after the rise starts.
 * @param  elapsed_us  Time since the show time.
 * @return 0 to 8.
 */
uint32_t game_sprites_rows_up (int64_t elapsed_us)
{
    uint32_t rows = 0u;

    if (elapsed_us >= GAME_SPRITE_RISE_US)
    {
        rows = GAME_SPRITE_SIDE;
    }
    else if (elapsed_us >= 0)
    {
        /* Below 160 ms: the step count is 0 to 3. */
        rows = 2u * ((uint32_t)(elapsed_us / GAME_SPRITE_ROW_STEP_US) + 1u);
    }
    else
    {
        /* Not shown yet. */
    }

    return rows;
}

/**
 * @brief  The start triangle, its brightness a triangle wave over a second.
 * @param  p_canvas  Canvas.
 * @param  t_us      Shared time.
 */
void game_sprites_start (game_canvas_t * p_canvas, int64_t t_us)
{
    int64_t const  half  = GAME_SPRITE_PULSE_US / 2;
    int64_t const  phase = ((t_us % GAME_SPRITE_PULSE_US) +
                            GAME_SPRITE_PULSE_US) % GAME_SPRITE_PULSE_US;
    int64_t const  rise  = (phase < half) ? phase
                                          : (GAME_SPRITE_PULSE_US - phase);
    int64_t const  span  = (int64_t)(GS_START_HIGH - GS_START_LOW); /* 4. */
    /* 0 <= rise <= half, so the step is 0 to 4 and the level 4 to 8. */
    uint32_t const step = (uint32_t)((rise * span) / half); /* 0 to 4. */
    uint8_t const  level = (uint8_t)(GS_START_LOW + step); /* 4 to 8. */
    game_rgb_t const colour = {0u, level, 0u};

    for (uint32_t row = 0u; row < GAME_SPRITE_SIDE; row++)
    {
        /* 1, 2, 3, 4, 4, 3, 2, 1 pixels from column 2. */
        uint32_t const width = (row < (GAME_SPRITE_SIDE / 2u))
                                   ? (row + 1u)
                                   : (GAME_SPRITE_SIDE - row);

        for (uint32_t col = 0u; col < width; col++)
        {
            gs_set(p_canvas, row, 2u + col, colour);
        }
    }
}

/**
 * @brief  The hit: squash, then the burst. The burst's rays are the pixels
 *         on the 8 lines through the centre; ring k is the square ring at
 *         distance k from the centre's 2 x 2 (k 0 is the centre itself).
 *         Frame f lights ring f + 1 in green; the centre stays gold.
 * @param  p_canvas    Canvas.
 * @param  elapsed_us  0 to GAME_SPRITE_HIT_US.
 * @param  b_squash    true when a mole was hit.
 */
void game_sprites_hit (game_canvas_t * p_canvas, int64_t elapsed_us,
                      bool b_squash)
{
    game_sprites_clear(p_canvas);

    if ((b_squash) && (elapsed_us < GAME_SPRITE_SQUASH_US))
    {
        /* Two frames: 5 rows up, then 3, the mole pressed into its hole. */
        uint32_t const rows =
            (elapsed_us < GAME_SPRITE_ROW_STEP_US) ? 5u : 3u;

        game_sprites_hole(p_canvas);
        game_sprites_mole(p_canvas, rows);
    }
    else
    {
        int64_t const start = b_squash ? GAME_SPRITE_SQUASH_US : 0;
        int64_t const length = GAME_SPRITE_HIT_US - start;
        int64_t const since  = (elapsed_us > start) ? (elapsed_us - start) : 0;
        int64_t const raw    = (since * GS_BURST_FRAMES) / length;
        /* Clamped to 0 to 2 first, so the narrowing keeps the value. */
        uint32_t const frame =
            (uint32_t)((raw >= GS_BURST_FRAMES) ? (GS_BURST_FRAMES - 1) : raw);

        for (uint32_t row = 0u; row < GAME_SPRITE_SIDE; row++)
        {
            for (uint32_t col = 0u; col < GAME_SPRITE_SIDE; col++)
            {
                /* Doubled offsets from the centre: 1, 3, 5 or 7. */
                uint32_t const d_r  = gs_abs_offset(row);
                uint32_t const d_c  = gs_abs_offset(col);
                uint32_t const far  = (d_r > d_c) ? d_r : d_c;
                uint32_t const ring = (far - 1u) / 2u; /* 0 to 3. */
                bool const     b_ray = (d_r == d_c) || (1u == d_r) ||
                                       (1u == d_c);

                if (0u == ring)
                {
                    gs_set(p_canvas, row, col, g_gold);
                }
                else if ((b_ray) && (ring == (frame + 1u)))
                {
                    gs_set(p_canvas, row, col, g_green);
                }
                else
                {
                    /* Dark between the rays and outside this frame's ring. */
                }
            }
        }
    }
}

/**
 * @brief  The wrong tap: the X on in the first and third 75 ms.
 * @param  p_canvas    Canvas.
 * @param  elapsed_us  0 to GAME_SPRITE_WRONG_US.
 */
void game_sprites_wrong (game_canvas_t * p_canvas, int64_t elapsed_us)
{
    bool const b_on = (elapsed_us >= 0) &&
                      (0 == ((elapsed_us / GS_BLINK_US) % 2));

    if (b_on)
    {
        for (uint32_t index = 0u; index < GAME_SPRITE_SIDE; index++)
        {
            gs_set(p_canvas, index, index, g_red);
            gs_set(p_canvas, index, GAME_SPRITE_SIDE - 1u - index, g_red);
        }
    }
}

/**
 * @brief  A wall picture piece in one colour.
 * @param  p_canvas  Canvas.
 * @param  mask      Bit n = pixel n lit.
 * @param  colour    proto_colour_t value.
 */
void game_sprites_picture (game_canvas_t * p_canvas, uint64_t mask,
                          uint8_t colour)
{
    game_rgb_t const lit = game_sprites_colour(colour);

    game_sprites_clear(p_canvas);

    for (uint32_t pixel = 0u; pixel < GAME_SPRITE_PIXELS; pixel++)
    {
        if (0u != ((mask >> pixel) & UINT64_C(1)))
        {
            p_canvas->px[pixel] = lit;
        }
    }
}

/**
 * @brief  The sparkle: a hash of the pixel and the twinkle step picks the
 *         lit pixels, so every tile shows a fresh pattern each step.
 * @param  p_canvas  Canvas.
 * @param  colour    proto_colour_t value.
 * @param  t_us      Shared time.
 */
void game_sprites_sparkle (game_canvas_t * p_canvas, uint8_t colour,
                          int64_t t_us)
{
    game_rgb_t const lit  = game_sprites_colour(colour);
    /* Modulo 2^32 on purpose: only the change from step to step matters. */
    uint32_t const   step = (uint32_t)(t_us / GAME_SPRITE_TWINKLE_US);

    game_sprites_clear(p_canvas);

    for (uint32_t pixel = 0u; pixel < GAME_SPRITE_PIXELS; pixel++)
    {
        if (0u == (gs_hash((step * GAME_SPRITE_PIXELS) + pixel) %
                   GS_SPARKLE_ONE_IN))
        {
            p_canvas->px[pixel] = lit;
        }
    }
}

/**
 * @brief  The channels of a picture colour.
 * @param  colour  proto_colour_t value.
 * @return The colour; white for an unknown code.
 */
game_rgb_t game_sprites_colour (uint8_t colour)
{
    return (colour < PROTO_COLOUR_COUNT) ? g_colours[colour]
                                         : g_colours[PROTO_COLOUR_WHITE];
}

/**
 * @brief  Set one pixel.
 * @param  p_canvas  Canvas.
 * @param  row       0 to 7.
 * @param  col       0 to 7.
 * @param  colour    The colour.
 */
static void gs_set (game_canvas_t * p_canvas, uint32_t row, uint32_t col,
                    game_rgb_t colour)
{
    p_canvas->px[(row * GAME_SPRITE_SIDE) + col] = colour;
}

/**
 * @brief  Doubled distance of a row or column from the canvas centre (3.5).
 * @param  pos  0 to 7.
 * @return 1, 3, 5 or 7.
 */
static uint32_t gs_abs_offset (uint32_t pos)
{
    uint32_t const twice = 2u * pos; /* 0 to 14; the centre is 7. */

    return (twice > 7u) ? (twice - 7u) : (7u - twice);
}

/**
 * @brief  A small integer hash (xorshift-multiply), for the sparkle.
 * @param  value  Input.
 * @return Mixed bits.
 */
static uint32_t gs_hash (uint32_t value)
{
    uint32_t mixed = value;

    mixed ^= mixed >> 16u;
    mixed *= UINT32_C(0x7FEB352D);
    mixed ^= mixed >> 15u;
    mixed *= UINT32_C(0x846CA68B);
    mixed ^= mixed >> 16u;

    return mixed;
}

/*** end of file ***/
