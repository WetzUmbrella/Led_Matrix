/**
 * @file    test_game_tile.c
 * @brief   Host tests of game_tile (the tile's whack-a-mole handling) and
 *          game_sprites (its drawings), upright (orientation 0) unless a
 *          test says otherwise.
 *
 * Owner: Cheston (Buddy 3) and Jia Jun (Buddy 5). Written for the
 * whack-a-mole re-plan, build step 7; sprites, round phases and the picture
 * list for step 3.
 */
#include "test_game_tile.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "display_frame.h"
#include "game_sprites.h"
#include "game_tile.h"
#include "proto.h"
#include "test_main.h"

/** Session, this tile's Node ID, and the Pico's times in microseconds. */
#define TST_SESSION 7u
#define TST_NODE    1u
#define TST_T_SHOW  INT64_C(1000000)

/** Largest message handled here, bytes (MOLE_ON is 14). */
#define TST_BUF_BYTES 32u

/** Milliseconds, in microseconds. */
#define TST_MS INT64_C(1000)

/**
 * @brief  Read one LED's (R, G, B) from a wire buffer.
 * @param  p_wire  Wire buffer.
 * @param  led     LED 0 to 63.
 * @param  p_rgb   Receives R, G, B.
 */
static void tst_led (uint8_t const * p_wire, uint32_t led, uint8_t * p_rgb)
{
    p_rgb[0] = p_wire[(3u * led) + 0u]; /* R */
    p_rgb[1] = p_wire[(3u * led) + 1u]; /* G */
    p_rgb[2] = p_wire[(3u * led) + 2u]; /* B */
}

/**
 * @brief  True when every LED has the same (R, G, B).
 * @param  p_wire  Wire buffer.
 * @param  r       Red.
 * @param  g       Green.
 * @param  b       Blue.
 * @return true when uniform.
 */
static bool tst_uniform (uint8_t const * p_wire, uint8_t red, uint8_t green,
                         uint8_t blue)
{
    bool b_same = true;

    for (uint32_t led = 0u; led < GAME_TILE_LEDS; led++)
    {
        uint8_t rgb[3];

        tst_led(p_wire, led, rgb);
        b_same = b_same && (rgb[0] == red) && (rgb[1] == green) &&
                 (rgb[2] == blue);
    }

    return b_same;
}

/**
 * @brief  Encode a MOLE_ON for a tile.
 * @param  p_buf    Receives the bytes.
 * @param  node_id  Addressed tile.
 * @param  seq      Header seq.
 * @param  t_show   Show time, Pico clock.
 * @param  up_ms    Up time.
 * @return Bytes written.
 */
static uint32_t tst_mole_on (uint8_t * p_buf, uint8_t node_id, uint16_t seq,
                             uint64_t t_show, uint16_t up_ms)
{
    proto_mole_on_t const msg = {
        {PROTO_TYPE_MOLE_ON, TST_SESSION, seq}, node_id, t_show, up_ms,
        (uint8_t)PROTO_TARGET_MOLE}; /* Enum code 0: fits uint8_t. */

    return proto_encode_mole_on(&msg, p_buf, PROTO_MOLE_ON_BYTES);
}

/**
 * @brief  Encode the start sprite (kind START, up until tapped) for a tile.
 * @param  p_buf    Receives the bytes.
 * @param  seq      Header seq.
 * @param  t_show   Show time, Pico clock.
 * @return Bytes written.
 */
static uint32_t tst_start_sprite (uint8_t * p_buf, uint16_t seq,
                                  uint64_t t_show)
{
    proto_mole_on_t const msg = {
        {PROTO_TYPE_MOLE_ON, TST_SESSION, seq}, TST_NODE, t_show, 0u,
        (uint8_t)PROTO_TARGET_START}; /* Enum code 1: fits uint8_t. */

    return proto_encode_mole_on(&msg, p_buf, PROTO_MOLE_ON_BYTES);
}

/**
 * @brief  Encode a GAME_CONTROL for this tile.
 * @param  p_buf      Receives the bytes.
 * @param  seq        Header seq.
 * @param  command    proto_game_cmd_t value.
 * @param  threshold  Tap threshold, 20 mg steps (ARM).
 * @return Bytes written.
 */
static uint32_t tst_control (uint8_t * p_buf, uint16_t seq, uint8_t command,
                             uint8_t threshold)
{
    proto_game_control_t const msg = {
        {PROTO_TYPE_GAME_CONTROL, TST_SESSION, seq}, TST_NODE, command, 0u,
        threshold};

    return proto_encode_game_control(&msg, p_buf, PROTO_GAME_CONTROL_BYTES);
}

/**
 * @brief  Encode a WALL_PICTURE entry for this tile.
 * @param  p_buf     Receives the bytes.
 * @param  seq       Header seq.
 * @param  slot      Entry, 0 to 3.
 * @param  effect    proto_pic_effect_t value.
 * @param  b_repeat  1 = loop back to entry 1 after it.
 * @param  t_show    Show time, Pico clock.
 * @param  dur_ms    Duration, ms.
 * @param  mask      The piece.
 * @return Bytes written.
 */
static uint32_t tst_picture (uint8_t * p_buf, uint16_t seq, uint8_t slot,
                             uint8_t effect, uint8_t b_repeat, int64_t t_show,
                             uint16_t dur_ms, uint64_t mask)
{
    proto_wall_picture_t const msg = {
        {PROTO_TYPE_WALL_PICTURE, TST_SESSION, seq}, TST_NODE, slot, effect,
        (uint8_t)PROTO_COLOUR_YELLOW, /* Enum code 1: fits uint8_t. */
        b_repeat, (uint64_t)t_show, /* The tests' times are non-negative. */
        dur_ms, mask};

    return proto_encode_wall_picture(&msg, p_buf, PROTO_WALL_PICTURE_BYTES);
}

/**
 * @brief  Whether pixel (row, col) of an upright frame has a colour.
 * @param  p_wire  Wire buffer drawn at orientation 0.
 * @param  row     0 to 7, row 0 at the top.
 * @param  col     0 to 7.
 * @param  colour  Expected colour.
 * @return true when it matches.
 */
static bool tst_px (uint8_t const * p_wire, uint32_t row, uint32_t col,
                    game_rgb_t colour)
{
    uint8_t rgb[3];

    tst_led(p_wire, (8u * row) + col, rgb);

    return (rgb[0] == colour.red) && (rgb[1] == colour.green) &&
           (rgb[2] == colour.blue);
}

/**
 * @brief  Feed one datagram to the tile and expect it ACKed.
 * @param  p_state  State.
 * @param  p_buf    Datagram.
 * @param  len      Its length.
 * @param  now_us   Tile local time.
 * @return true when ACKed.
 */
static bool tst_feed (game_tile_state_t * p_state, uint8_t const * p_buf,
                      uint32_t len, int64_t now_us)
{
    uint16_t ack = 0u;

    return game_tile_on_pico(p_state, TST_NODE, p_buf, len, now_us, &ack);
}

/**
 * @brief  The picture entry slot (by its mask bit) showing at a time: the
 *         tests give entry n the mask with only bit n set.
 * @param  p_state  State.
 * @param  t_us     Pico time.
 * @return The entry, or 4 when the tile is dark.
 */
static uint32_t tst_showing (game_tile_state_t const * p_state, int64_t t_us)
{
    uint8_t  wire[GAME_TILE_WIRE_BYTES];
    uint32_t found = 4u;

    game_tile_fill_wire(p_state, t_us, 0, 0u, wire);

    for (uint32_t led = 0u; led < 4u; led++)
    {
        uint8_t rgb[3];

        tst_led(wire, led, rgb);
        found = ((0u != rgb[0]) || (0u != rgb[1])) ? led : found;
    }

    return found;
}

/**
 * @brief  Encode a FEEDBACK for a tile.
 * @param  p_buf    Receives the bytes.
 * @param  node_id  Addressed tile.
 * @param  seq      Header seq.
 * @param  result   proto_feedback_t value.
 * @return Bytes written.
 */
static uint32_t tst_feedback (uint8_t * p_buf, uint8_t node_id, uint16_t seq,
                              uint8_t result)
{
    proto_feedback_msg_t const msg = {
        {PROTO_TYPE_FEEDBACK, TST_SESSION, seq}, node_id, result};

    return proto_encode_feedback(&msg, p_buf, PROTO_FEEDBACK_BYTES);
}


/** The colours the sprites use (DS-11), as the tests expect them. */
static game_rgb_t const g_dark   = {0u, 0u, 0u};
static game_rgb_t const g_body   = {5u, 2u, 0u};
static game_rgb_t const g_eye    = {6u, 6u, 6u};
static game_rgb_t const g_nose   = {8u, 3u, 4u};
static game_rgb_t const g_hole   = {1u, 0u, 2u};
static game_rgb_t const g_green  = {0u, 8u, 0u};
static game_rgb_t const g_gold   = {8u, 6u, 0u};
static game_rgb_t const g_red    = {8u, 0u, 0u};
static game_rgb_t const g_yellow = {7u, 5u, 0u};

/**
 * @brief  G1, the miss: the red X on for 75 ms over the mole, off, on,
 *         then gone; the mole stays up throughout; and an unknown colour
 *         code draws white.
 * @param  p_state  State, after the hit of G1.
 * @return true when every check passed.
 */
static bool tst_check_miss (game_tile_state_t * p_state)
{
    uint8_t  buf[TST_BUF_BYTES];
    uint8_t  wire[GAME_TILE_WIRE_BYTES];
    uint16_t ack = 0u;
    uint32_t len = 0u;
    bool b_ok = true;

    /* A miss: the red X on for 75 ms over the mole, off, on, then gone; the
     * mole stays up throughout. */
    len = tst_mole_on(buf, TST_NODE, 7u, TST_T_SHOW + 2000000, 1000u);
    b_ok = (test_main_check(game_tile_on_pico(p_state, TST_NODE, buf, len, 0,
                                              &ack), __LINE__)) && b_ok;
    len = tst_feedback(buf, TST_NODE, 8u, PROTO_FEEDBACK_MISS);
    b_ok = (test_main_check(game_tile_on_pico(p_state, TST_NODE, buf, len,
                                              3000 * TST_MS, &ack),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(p_state, TST_T_SHOW + 2500000, 3010 * TST_MS, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 0u, 0u, g_red)) &&
                                (tst_px(wire, 1u, 1u, g_red)) &&
                                (tst_px(wire, 0u, 2u, g_body)),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(p_state, TST_T_SHOW + 2500000, 3100 * TST_MS, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 0u, 0u, g_dark)) &&
                                (tst_px(wire, 1u, 1u, g_body)),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(p_state, TST_T_SHOW + 2500000, 3160 * TST_MS, 0u, wire);
    b_ok = (test_main_check(tst_px(wire, 1u, 1u, g_red), __LINE__)) && b_ok;
    game_tile_fill_wire(p_state, TST_T_SHOW + 2500000, 3310 * TST_MS, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 1u, 1u, g_body)) &&
                                (1u == p_state->b_mole),
                            __LINE__)) && b_ok;

    /* An unknown colour code draws white. */
    b_ok = (test_main_check(4u == game_sprites_colour(9u).red, __LINE__)) &&
           b_ok;


    return b_ok;
}

/**
 * @brief  G1: the mole rises out of its hole (2 rows every 40 ms), stays up
 *         until its hide time and sinks back; a hit squashes it and plays
 *         the green burst (the mole ends); a miss blinks the red X twice
 *         over the mole, which stays.
 * @return true when every check passed.
 */
bool test_game_tile_mole_and_flash (void)
{
    game_tile_state_t state;
    uint8_t           buf[TST_BUF_BYTES];
    uint8_t           wire[GAME_TILE_WIRE_BYTES];
    uint16_t          ack = 0u;
    uint32_t          len = tst_mole_on(buf, TST_NODE, 5u, TST_T_SHOW, 1000u);
    bool              b_ok = true;

    game_tile_init(&state);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len, 0,
                                              &ack), __LINE__)) && b_ok;
    b_ok = (test_main_check(5u == ack, __LINE__)) && b_ok;

    /* Rows out of the hole: 2 at once, 2 more every 40 ms. */
    b_ok = (test_main_check((0u == game_sprites_rows_up(-1)) &&
                                (2u == game_sprites_rows_up(0)) &&
                                (2u == game_sprites_rows_up(39999)) &&
                                (4u == game_sprites_rows_up(40000)) &&
                                (8u == game_sprites_rows_up(120000)) &&
                                (8u == game_sprites_rows_up(500000)),
                            __LINE__)) && b_ok;

    /* Dark before the show; at the show the top 2 sprite rows show on the
     * bottom 2 canvas rows; 160 ms later the whole mole is up. */
    game_tile_fill_wire(&state, TST_T_SHOW - 1, 0, 0u, wire);
    b_ok = (test_main_check(tst_uniform(wire, 0u, 0u, 0u), __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW, 0, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 6u, 2u, g_body)) &&
                                (tst_px(wire, 6u, 0u, g_dark)) &&
                                (tst_px(wire, 7u, 1u, g_body)) &&
                                (tst_px(wire, 0u, 2u, g_dark)),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW + (160 * TST_MS), 0, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 0u, 2u, g_body)) &&
                                (tst_px(wire, 2u, 2u, g_eye)) &&
                                (tst_px(wire, 3u, 3u, g_nose)) &&
                                (tst_px(wire, 0u, 0u, g_dark)),
                            __LINE__)) && b_ok;

    /* At the hide time it starts sinking (6 rows up), gone 160 ms later. */
    game_tile_fill_wire(&state, TST_T_SHOW + (1000 * TST_MS), 0, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 2u, 2u, g_body)) &&
                                (tst_px(wire, 0u, 2u, g_dark)),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW + (1160 * TST_MS), 0, 0u, wire);
    b_ok = (test_main_check(tst_uniform(wire, 0u, 0u, 0u), __LINE__)) && b_ok;

    /* A hit at local 1.5 s on an up mole: squashed (5 rows up) at 10 ms,
     * the burst's first ring at 100 ms, its last at 200 ms, then dark: the
     * hit ended the mole. */
    len = tst_feedback(buf, TST_NODE, 6u, PROTO_FEEDBACK_HIT);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len,
                                              1500 * TST_MS, &ack),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check((0u == state.b_mole) && (1u == state.b_squash),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW, 1510 * TST_MS, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 3u, 2u, g_body)) &&
                                (tst_px(wire, 7u, 0u, g_body)) &&
                                (tst_px(wire, 0u, 2u, g_dark)),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW, 1600 * TST_MS, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 3u, 3u, g_gold)) &&
                                (tst_px(wire, 2u, 2u, g_green)) &&
                                (tst_px(wire, 0u, 0u, g_dark)),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW, 1700 * TST_MS, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 0u, 0u, g_green)) &&
                                (tst_px(wire, 2u, 2u, g_dark)) &&
                                (tst_px(wire, 4u, 4u, g_gold)),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW, 1760 * TST_MS, 0u, wire);
    b_ok = (test_main_check(tst_uniform(wire, 0u, 0u, 0u), __LINE__)) && b_ok;

    b_ok = (tst_check_miss(&state)) && b_ok;

    return b_ok;
}

/**
 * @brief  G2: only messages for this tile are ACKed; a repeat (same seq) is
 *         ACKed but not applied; an older seq is ACKed, not applied.
 * @return true when every check passed.
 */
bool test_game_tile_acks (void)
{
    game_tile_state_t state;
    uint8_t           buf[TST_BUF_BYTES];
    uint16_t          ack = 0u;
    uint32_t          len = 0u;
    bool              b_ok = true;

    game_tile_init(&state);

    /* A MOLE_ON for tile 2 is not ours: not ACKed, nothing shown. */
    len = tst_mole_on(buf, 2u, 10u, TST_T_SHOW, 1000u);
    b_ok = (test_main_check(!game_tile_on_pico(&state, TST_NODE, buf, len, 0,
                                               &ack), __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == state.b_mole, __LINE__)) && b_ok;

    /* Ours, seq 10: ACKed and shown. */
    len = tst_mole_on(buf, TST_NODE, 10u, TST_T_SHOW, 1000u);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len, 0,
                                              &ack), __LINE__)) && b_ok;
    b_ok = (test_main_check(10u == ack, __LINE__)) && b_ok;

    /* A repeat of seq 10 with a later show time: ACKed again, not applied. */
    len = tst_mole_on(buf, TST_NODE, 10u, TST_T_SHOW + 500000, 1000u);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len, 0,
                                              &ack), __LINE__)) && b_ok;
    b_ok = (test_main_check(10u == ack, __LINE__)) && b_ok;
    b_ok = (test_main_check(TST_T_SHOW == state.show_us, __LINE__)) && b_ok;

    /* An older seq (9) is ACKed but not applied. */
    len = tst_mole_on(buf, TST_NODE, 9u, TST_T_SHOW + 700000, 1000u);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len, 0,
                                              &ack), __LINE__)) && b_ok;
    b_ok = (test_main_check(TST_T_SHOW == state.show_us, __LINE__)) && b_ok;

    /* A newer seq (11) after 10 applies. */
    len = tst_mole_on(buf, TST_NODE, 11u, TST_T_SHOW + 700000, 1000u);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len, 0,
                                              &ack), __LINE__)) && b_ok;
    b_ok = (test_main_check(TST_T_SHOW + 700000 == state.show_us,
                            __LINE__)) && b_ok;

    /* A message that does not decode is not ACKed. */
    b_ok = (test_main_check(!game_tile_on_pico(&state, TST_NODE, buf, 3u, 0,
                                               &ack), __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  G3, the order: a lost ARM (seq 30) resent after its start
 *         sprite (seq 31) still arms the round, and the sprite stays.
 * @param  p_state  State (reset here).
 * @return true when every check passed.
 */
static bool tst_check_late_arm (game_tile_state_t * p_state)
{
    uint8_t  buf[TST_BUF_BYTES];
    bool b_ok = true;

    /* A lost ARM (seq 30) resent after its start sprite (seq 31): the round
     * is armed and the sprite stays. */
    game_tile_init(p_state);
    b_ok = (test_main_check(tst_feed(p_state, buf,
                                     tst_start_sprite(buf, 31u, TST_T_SHOW),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check(tst_feed(p_state, buf,
                                     tst_control(buf, 30u, PROTO_GAME_ARM, 0u),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check((1u == p_state->b_round) &&
                                (1u == p_state->b_mole) &&
                                (0u == game_tile_threshold_mg(p_state)),
                            __LINE__)) && b_ok;


    return b_ok;
}

/**
 * @brief  G3: the round phases on the tile: ARM shows the holes and carries
 *         the tap threshold; the start sprite pulses until tapped (a hit
 *         without a squash); an ARM resent after the sprite still arms and
 *         keeps the sprite; START plays, END clears the holes; an older
 *         GAME_CONTROL is not applied; the tap test shows the holes.
 * @return true when every check passed.
 */
bool test_game_tile_round (void)
{
    game_tile_state_t state;
    uint8_t           buf[TST_BUF_BYTES];
    uint8_t           wire[GAME_TILE_WIRE_BYTES];
    game_rgb_t const  half = {0u, 6u, 0u}; /* The pulse at 250 ms. */
    bool              b_ok = true;

    game_tile_init(&state);
    b_ok = (test_main_check(!game_tile_active(&state, 0), __LINE__)) && b_ok;
    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_control(buf, 20u, PROTO_GAME_ARM, 15u),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check((1u == state.b_round) && (0u == state.b_game) &&
                                (300u == game_tile_threshold_mg(&state)) &&
                                (game_tile_active(&state, 0)),
                            __LINE__)) && b_ok;

    /* The hole: a dim purple arc on the bottom two rows. */
    game_tile_fill_wire(&state, 0, 0, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 7u, 0u, g_hole)) &&
                                (tst_px(wire, 6u, 0u, g_dark)) &&
                                (tst_px(wire, 6u, 1u, g_hole)) &&
                                (tst_px(wire, 0u, 0u, g_dark)),
                            __LINE__)) && b_ok;

    /* The start sprite: a triangle from column 2, pulsing 4 to 8, still up
     * 100 s later. */
    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_start_sprite(buf, 21u, TST_T_SHOW),
                                     0), __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW + (250 * TST_MS), 0, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 0u, 2u, half)) &&
                                (tst_px(wire, 3u, 5u, half)) &&
                                (tst_px(wire, 0u, 3u, g_dark)),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW + (500 * TST_MS), 0, 0u, wire);
    b_ok = (test_main_check(tst_px(wire, 4u, 5u, g_green), __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW + (100000 * TST_MS), 0, 0u, wire);
    b_ok = (test_main_check(!tst_px(wire, 0u, 2u, g_dark), __LINE__)) && b_ok;

    /* The tap on it: a hit with no squash, the burst's centre at once. */
    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_feedback(buf, TST_NODE, 22u,
                                                  PROTO_FEEDBACK_HIT),
                                     5000 * TST_MS), __LINE__)) && b_ok;
    b_ok = (test_main_check((0u == state.b_mole) && (0u == state.b_squash),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW, 5010 * TST_MS, 0u, wire);
    b_ok = (test_main_check(tst_px(wire, 3u, 3u, g_gold), __LINE__)) && b_ok;

    /* START plays; END ends: no holes, nothing left to draw. */
    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_control(buf, 24u, PROTO_GAME_START,
                                                 0u),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check((1u == state.b_game) && (1u == state.b_round),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_control(buf, 25u, PROTO_GAME_END, 0u),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check((0u == state.b_game) && (0u == state.b_round) &&
                                (!game_tile_active(&state, 10000 * TST_MS)),
                            __LINE__)) && b_ok;

    /* A late START (seq 23, older than END): ACKed, not applied. */
    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_control(buf, 23u, PROTO_GAME_START,
                                                 0u),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == state.b_game, __LINE__)) && b_ok;

    /* The tap test: holes while on, nothing after off. */
    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_control(buf, 26u,
                                                 PROTO_GAME_TAP_TEST_ON, 0u),
                                     0), __LINE__)) && b_ok;
    game_tile_fill_wire(&state, 0, 10000 * TST_MS, 0u, wire);
    b_ok = (test_main_check((tst_px(wire, 7u, 0u, g_hole)) &&
                                (game_tile_active(&state, 10000 * TST_MS)),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_control(buf, 27u,
                                                 PROTO_GAME_TAP_TEST_OFF, 0u),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check(!game_tile_active(&state, 10000 * TST_MS),
                            __LINE__)) && b_ok;

    b_ok = (tst_check_late_arm(&state)) && b_ok;

    return b_ok;
}

/**
 * @brief  G4, the rotation: only bit 0 of the piece, until replaced; under
 *         each rotation exactly one LED lights, the one that
 *         display_frame_source_led() maps to pixel 0.
 * @param  p_state  State.
 * @param  t_1      A Pico time to draw at.
 * @return true when every check passed.
 */
static bool tst_check_pic_rotation (game_tile_state_t * p_state, int64_t t_1)
{
    uint8_t  buf[TST_BUF_BYTES];
    uint8_t  wire[GAME_TILE_WIRE_BYTES];
    uint8_t  rgb[3];
    bool b_ok = true;

    /* Only bit 0 of the piece, until replaced: under each rotation exactly
     * one LED lights, the one display_frame_source_led() maps to pixel 0. */
    b_ok = (test_main_check(tst_feed(p_state, buf,
                                     tst_picture(buf, 50u, 0u,
                                                 PROTO_PIC_PICTURE, 0u, 0u, 0u,
                                                 UINT64_C(1)),
                                     0), __LINE__)) && b_ok;

    for (uint8_t orientation = 0u; orientation < 4u; orientation++)
    {
        uint32_t lit      = 0u;
        uint32_t expected = 0u;

        game_tile_fill_wire(p_state, t_1, 0, orientation, wire);

        for (uint32_t led = 0u; led < GAME_TILE_LEDS; led++)
        {
            tst_led(wire, led, rgb);
            lit += (0u != rgb[0]) ? 1u : 0u;

            if (0u == display_frame_source_led(led, orientation))
            {
                expected = led;
            }
        }

        tst_led(wire, expected, rgb);
        b_ok = (test_main_check((1u == lit) && (0u != rgb[0]), __LINE__)) &&
               b_ok;
    }


    return b_ok;
}

/**
 * @brief  G4, the results: sparkle 1.5 s, then entries 1 and 2 (2.5 s
 *         each), the second looping back to the first; END keeps the
 *         list; CLEAR empties it.
 * @param  p_state  State, armed.
 * @param  t_1      Show time of the sparkle, Pico clock.
 * @return true when every check passed.
 */
static bool tst_check_results (game_tile_state_t * p_state, int64_t t_1)
{
    uint8_t  buf[TST_BUF_BYTES];
    uint8_t  wire[GAME_TILE_WIRE_BYTES];
    uint8_t  rgb[3];
    bool b_ok = true;

    /* The results: sparkle 1.5 s, then entries 1 and 2 (2.5 s each), the
     * second looping back to the first. */
    b_ok = (test_main_check(tst_feed(p_state, buf,
                                     tst_picture(buf, 41u, 0u,
                                                 PROTO_PIC_SPARKLE, 0u,
                                                 t_1, 1500u, 0u),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check(tst_feed(p_state, buf,
                                     tst_picture(buf, 42u, 1u,
                                                 PROTO_PIC_PICTURE, 0u,
                                                 t_1 + 1500000,
                                                 2500u, UINT64_C(2)),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check(tst_feed(p_state, buf,
                                     tst_picture(buf, 43u, 2u,
                                                 PROTO_PIC_PICTURE, 1u,
                                                 t_1 + 4000000,
                                                 2500u, UINT64_C(4)),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check(tst_feed(p_state, buf,
                                     tst_control(buf, 44u, PROTO_GAME_END, 0u),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check((1u == tst_showing(p_state, t_1 + 2000000)) &&
                                (2u == tst_showing(p_state, t_1 + 4500000)) &&
                                (1u == tst_showing(p_state, t_1 + 6600000)) &&
                                (2u == tst_showing(p_state, t_1 + 9100000)) &&
                                (1u == tst_showing(p_state, t_1 + 11600000)),
                            __LINE__)) && b_ok;

    /* The sparkle lights some pixels, not all. */
    {
        uint32_t lit = 0u;

        game_tile_fill_wire(p_state, t_1 + 500000, 0, 0u, wire);

        for (uint32_t led = 0u; led < GAME_TILE_LEDS; led++)
        {
            tst_led(wire, led, rgb);
            lit += (0u != rgb[0]) ? 1u : 0u;
        }

        b_ok = (test_main_check((lit >= 3u) && (lit <= 24u), __LINE__)) &&
               b_ok;
    }

    /* CLEAR empties the list: nothing left to draw. */
    b_ok = (test_main_check(tst_feed(p_state, buf,
                                     tst_picture(buf, 45u, 0u, PROTO_PIC_CLEAR,
                                                 0u, 0u, 0u, 0u),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check(!game_tile_active(p_state, 0), __LINE__)) && b_ok;


    return b_ok;
}

/**
 * @brief  G4: the picture list: countdown entries arriving out of order all
 *         show at their times; ARM clears the list and drops a late older
 *         entry; the results loop entry 1 and 2 after the sparkle; END
 *         keeps the list; CLEAR empties it; a picture is drawn in the tile's
 *         rotation. Entry n's test mask is bit n alone.
 * @return true when every check passed.
 */
bool test_game_tile_pictures (void)
{
    game_tile_state_t state;
    uint8_t           buf[TST_BUF_BYTES];
    uint8_t           wire[GAME_TILE_WIRE_BYTES];
    int64_t const     t_0 = 10000 * TST_MS;
    int64_t const     t_1 = 20000 * TST_MS;
    bool              b_ok = true;

    game_tile_init(&state);

    /* The countdown, entries 2, 0, 1 in that order, 1 s each. */
    for (uint32_t index = 0u; index < 3u; index++)
    {
        uint8_t const slot = (uint8_t)((index + 2u) % 3u); /* 2, 0, 1. */
        uint16_t const seq = (uint16_t)(30u + slot); /* 30 to 32. */

        b_ok = (test_main_check(tst_feed(&state, buf,
                                         tst_picture(buf, seq, slot,
                                                     PROTO_PIC_PICTURE, 0u,
                                                     t_0 + (slot * 1000 *
                                                            TST_MS),
                                                     1000u,
                                                     UINT64_C(1) << slot),
                                         0), __LINE__)) && b_ok;
    }

    b_ok = (test_main_check((4u == tst_showing(&state, t_0 - 1)) &&
                                (0u == tst_showing(&state, t_0)) &&
                                (1u == tst_showing(&state, t_0 + 1500000)) &&
                                (2u == tst_showing(&state, t_0 + 2999999)) &&
                                (4u == tst_showing(&state, t_0 + 3000000)),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, t_0, 0, 0u, wire);
    b_ok = (test_main_check(tst_px(wire, 0u, 0u, g_yellow), __LINE__)) && b_ok;

    /* ARM clears it; an older entry arriving late is dropped. */
    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_control(buf, 40u, PROTO_GAME_ARM, 0u),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_picture(buf, 35u, 3u,
                                                 PROTO_PIC_PICTURE, 0u,
                                                 t_0, 0u,
                                                 UINT64_C(8)),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check((4u == tst_showing(&state, t_0)) &&
                                (0u == state.pic[3].b_valid),
                            __LINE__)) && b_ok;

    b_ok = (tst_check_results(&state, t_1)) && b_ok;

    b_ok = (tst_check_pic_rotation(&state, t_1)) && b_ok;

    return b_ok;
}

/**
 * @brief  G5: the game-mode GPIO4 pulses (DS-13): 10 ms in the mole's first
 *         frame, 6 ms in the hit burst's first frame, none otherwise (no
 *         pulse for the start sprite or a miss).
 * @return true when every check passed.
 */
bool test_game_tile_pulses (void)
{
    game_tile_state_t state;
    uint8_t           buf[TST_BUF_BYTES];
    int64_t const     frame = 16667;
    bool              b_ok  = true;

    game_tile_init(&state);
    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_mole_on(buf, TST_NODE, 60u,
                                                 TST_T_SHOW, 1000u),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check(
                (GAME_TILE_PULSE_MOLE_US ==
                 game_tile_pulse_us(&state, TST_T_SHOW - 1, TST_T_SHOW, 0,
                                    0)) &&
                    (0u == game_tile_pulse_us(&state, TST_T_SHOW,
                                              TST_T_SHOW + frame, 0, 0)) &&
                    (0u == game_tile_pulse_us(&state, TST_T_SHOW - frame,
                                              TST_T_SHOW - 1, 0, 0)),
                __LINE__)) && b_ok;

    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_feedback(buf, TST_NODE, 61u,
                                                  PROTO_FEEDBACK_HIT),
                                     2000 * TST_MS), __LINE__)) && b_ok;
    b_ok = (test_main_check(
                (GAME_TILE_PULSE_HIT_US ==
                 game_tile_pulse_us(&state, 0, 0, (2000 * TST_MS) - frame,
                                    2000 * TST_MS)) &&
                    (0u == game_tile_pulse_us(&state, 0, 0, 2000 * TST_MS,
                                              (2000 * TST_MS) + frame)),
                __LINE__)) && b_ok;

    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_feedback(buf, TST_NODE, 62u,
                                                  PROTO_FEEDBACK_MISS),
                                     3000 * TST_MS), __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == game_tile_pulse_us(&state, 0, 0,
                                                     (3000 * TST_MS) - frame,
                                                     3000 * TST_MS),
                            __LINE__)) && b_ok;

    b_ok = (test_main_check(tst_feed(&state, buf,
                                     tst_start_sprite(buf, 63u,
                                                      TST_T_SHOW * 5),
                                     0), __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == game_tile_pulse_us(&state,
                                                     (TST_T_SHOW * 5) - 1,
                                                     TST_T_SHOW * 5, 0, 0),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  G6: TAP and ACK encodings decode back, out-of-range Node IDs are
 *         refused, and the state publishes and reads back whole.
 * @return true when every check passed.
 */
bool test_game_tile_codecs (void)
{
    uint8_t           buf[PROTO_TAP_BYTES];
    game_tile_state_t state;
    game_tile_state_t back;
    proto_tap_t       tap;
    proto_ack_t       ack;
    uint32_t          len = 0u;
    bool              b_ok = true;

    len = game_tile_encode_tap(buf, TST_SESSION, 3u, 4u, 1234u, 5000000);
    b_ok = (test_main_check(PROTO_TAP_BYTES == len, __LINE__)) && b_ok;
    b_ok = (test_main_check(proto_decode_tap(buf, len, &tap), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check((4u == tap.node_id) && (1234u == tap.peak) &&
                                (5000000u == tap.t_tap) &&
                                (3u == tap.header.seq), __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == game_tile_encode_tap(buf, TST_SESSION, 3u, 0u,
                                                       1u, 5), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(0u == game_tile_encode_tap(buf, TST_SESSION, 3u, 1u,
                                                       1u, -5), __LINE__)) &&
           b_ok;

    len = game_tile_encode_ack(buf, TST_SESSION, 9u, TST_NODE, 3u);
    b_ok = (test_main_check(proto_decode_ack(buf, len, &ack), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check((TST_NODE == ack.node_id) && (3u == ack.acked_seq),
                            __LINE__)) && b_ok;

    /* Publish, then read it back whole, then a second publish replaces it. */
    game_tile_init(&state);
    state.b_mole  = 1u;
    state.show_us = TST_T_SHOW;
    state.hide_us = TST_T_SHOW + 1000;
    game_tile_publish(&state);
    game_tile_read(&back);
    b_ok = (test_main_check(0 == memcmp(&state, &back, sizeof(state)),
                            __LINE__)) && b_ok;
    state.pic[3].mask = UINT64_C(0x8000000000000001);
    game_tile_publish(&state);
    game_tile_read(&back);
    b_ok = (test_main_check(UINT64_C(0x8000000000000001) == back.pic[3].mask,
                            __LINE__)) && b_ok;

    return b_ok;
}

/*** end of file ***/
