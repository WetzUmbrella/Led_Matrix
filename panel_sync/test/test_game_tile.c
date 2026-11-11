/**
 * @file    test_game_tile.c
 * @brief   Host tests of game_tile (the tile's whack-a-mole handling).
 *
 * Owner: Cheston (Buddy 3) and Jia Jun (Buddy 5). Written for the
 * whack-a-mole re-plan, build step 7.
 */
#include "test_game_tile.h"

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include "display_frame.h"
#include "game_tile.h"
#include "proto.h"
#include "test_main.h"

/** Session, this tile's Node ID, and the Pico's times in microseconds. */
#define TST_SESSION 7u
#define TST_NODE    1u
#define TST_T_SHOW  INT64_C(1000000)

/** Largest message handled here, bytes (MOLE_ON is 14). */
#define TST_BUF_BYTES 16u

/** Colours of one LED, as tst_led() reads them (R, G, B). */
#define TST_MOLE_G 3u
#define TST_MOLE_R 8u
#define TST_GREEN_G 8u
#define TST_RED_R 8u

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
        {PROTO_TYPE_MOLE_ON, TST_SESSION, seq}, node_id, t_show, up_ms};

    return proto_encode_mole_on(&msg, p_buf, PROTO_MOLE_ON_BYTES);
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

/**
 * @brief  The mole is a solid fill from show to hide in the Pico's clock; a
 *         FEEDBACK flashes green (a hit ends the mole) or red (the mole stays)
 *         on the tile's own clock.
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

    /* Dark before the show, the mole from show to hide, dark at the hide. */
    game_tile_fill_wire(&state, TST_T_SHOW - 1, 0, 0, wire);
    b_ok = (test_main_check(tst_uniform(wire, 0u, 0u, 0u), __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW, 0, 0, wire);
    b_ok = (test_main_check(tst_uniform(wire, TST_MOLE_R, TST_MOLE_G, 0u),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW + 999999, 0, 0, wire);
    b_ok = (test_main_check(tst_uniform(wire, TST_MOLE_R, TST_MOLE_G, 0u),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW + 1000000, 0, 0, wire);
    b_ok = (test_main_check(tst_uniform(wire, 0u, 0u, 0u), __LINE__)) && b_ok;

    /* A hit at local 1.5 s: green for 250 ms over the mole, then the mole
     * is gone (the hit ended it). */
    len = tst_feedback(buf, TST_NODE, 6u, PROTO_FEEDBACK_HIT);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len,
                                              1500000, &ack),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW + 100000, 1600000, 0, wire);
    b_ok = (test_main_check(tst_uniform(wire, 0u, TST_GREEN_G, 0u),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW + 100000, 1750000, 0, wire);
    b_ok = (test_main_check(tst_uniform(wire, 0u, 0u, 0u), __LINE__)) && b_ok;

    /* A miss flashes red and the mole stays up: the mole shows again after. */
    len = tst_mole_on(buf, TST_NODE, 7u, TST_T_SHOW + 2000000, 1000u);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len, 0,
                                              &ack), __LINE__)) && b_ok;
    len = tst_feedback(buf, TST_NODE, 8u, PROTO_FEEDBACK_MISS);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len,
                                              3000000, &ack),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW + 2100000, 3100000, 0, wire);
    b_ok = (test_main_check(tst_uniform(wire, TST_RED_R, 0u, 0u),
                            __LINE__)) && b_ok;
    game_tile_fill_wire(&state, TST_T_SHOW + 2100000, 3300000, 0, wire);
    b_ok = (test_main_check(tst_uniform(wire, TST_MOLE_R, TST_MOLE_G, 0u),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Only messages for this tile are ACKed; a repeat (same seq) is ACKed
 *         but not applied; an older seq is neither applied nor ACKed twice.
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
 * @brief  START clears the score, SCORE_MASK is drawn in the tile's rotation,
 *         END keeps the score on the tile.
 * @return true when every check passed.
 */
bool test_game_tile_score (void)
{
    game_tile_state_t state;
    uint8_t           buf[TST_BUF_BYTES];
    uint8_t           wire[GAME_TILE_WIRE_BYTES];
    uint8_t           rgb[3];
    uint16_t          ack = 0u;
    uint32_t          len = 0u;
    bool              b_ok = true;

    game_tile_init(&state);
    proto_game_control_t const start = {
        {PROTO_TYPE_GAME_CONTROL, TST_SESSION, 20u}, TST_NODE,
        PROTO_GAME_START};
    len = proto_encode_game_control(&start, buf, PROTO_GAME_CONTROL_BYTES);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len, 0,
                                              &ack), __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == state.b_game, __LINE__)) && b_ok;

    /* SCORE_MASK: only LED 0 of the asset frame (pixel 0). With rotation 0 the
     * lit LED is LED 0; under each rotation exactly one LED lights, the one
     * that display_frame_source_led() maps to source 0. */
    proto_score_mask_t const mask = {
        {PROTO_TYPE_SCORE_MASK, TST_SESSION, 21u}, TST_NODE, UINT64_C(1)};
    len = proto_encode_score_mask(&mask, buf, PROTO_SCORE_MASK_BYTES);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len, 0,
                                              &ack), __LINE__)) && b_ok;
    for (uint8_t orientation = 0u; orientation < 4u; orientation++)
    {
        uint32_t lit = 0u;
        uint32_t expected = 0u;

        game_tile_fill_wire(&state, 0, 0, orientation, wire);
        for (uint32_t led = 0u; led < GAME_TILE_LEDS; led++)
        {
            tst_led(wire, led, rgb);
            if (0u != rgb[0])
            {
                lit++;
            }
            if (0u == display_frame_source_led(led, orientation))
            {
                expected = led;
            }
        }
        tst_led(wire, expected, rgb);
        b_ok = (test_main_check((1u == lit) && (0u != rgb[0]), __LINE__)) &&
               b_ok;
    }

    /* END: the mole and the flash go; the score stays until the next START. */
    proto_game_control_t const end = {
        {PROTO_TYPE_GAME_CONTROL, TST_SESSION, 22u}, TST_NODE,
        PROTO_GAME_END};
    len = proto_encode_game_control(&end, buf, PROTO_GAME_CONTROL_BYTES);
    b_ok = (test_main_check(game_tile_on_pico(&state, TST_NODE, buf, len, 0,
                                              &ack), __LINE__)) && b_ok;
    b_ok = (test_main_check((0u == state.b_game) && (1u == state.b_score),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  TAP and ACK encodings decode back, out-of-range Node IDs are
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
    state.score_mask = UINT64_C(0x8000000000000001);
    game_tile_publish(&state);
    game_tile_read(&back);
    b_ok = (test_main_check(UINT64_C(0x8000000000000001) == back.score_mask,
                            __LINE__)) && b_ok;

    return b_ok;
}

/*** end of file ***/
