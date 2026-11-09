/**
 * @file    game_tile.c
 * @brief   Whack-a-mole on the tile (see game_tile.h).
 *
 * Owner: Cheston (Buddy 3) and Jia Jun (Buddy 5). Written for build step 7.
 *
 * The seqlock follows playback_control.c: one writer (the Network Task), a
 * reader on the Display Engine's core, 32-bit atomic words, an even seq when
 * the data is stable (DEVIATION D-1: C11 stdatomic.h).
 */
#include "game_tile.h"

#include <stdatomic.h>
#include <string.h>

#include "display_frame.h"

/** Colours in channel values 0 to 8 (R, G, B). */
#define GAME_TILE_MAX     8u
#define GAME_TILE_MOLE_R  8u /**< Mole: amber, one colour (re-plan). */
#define GAME_TILE_MOLE_G  3u
#define GAME_TILE_MOLE_B  0u
#define GAME_TILE_SCORE_V 4u /**< Score: white at half value. */

/** Microseconds per millisecond. */
#define GAME_TILE_US_PER_MS INT64_C(1000)

/** Seq comparison half: a seq is newer when 1 to 32,767 ahead. */
#define GAME_TILE_SEQ_HALF UINT16_C(0x8000)

/** FEEDBACK results on the wire (layout_table.md 5.9). */
#define GAME_TILE_RESULT_HIT  ((uint32_t)PROTO_FEEDBACK_HIT)  /* Wire value. */
#define GAME_TILE_RESULT_MISS ((uint32_t)PROTO_FEEDBACK_MISS) /* Wire value. */

/** Words of the state, for the seqlock. */
#define GAME_TILE_WORDS (sizeof(game_tile_state_t) / sizeof(uint32_t))

_Static_assert(0u == (sizeof(game_tile_state_t) % sizeof(uint32_t)),
               "game_tile_state_t must be a whole number of 32-bit words");

/** The seqlock: even when the words are stable, odd during a publish. */
static _Atomic uint32_t g_pub_seq = 0u;
static _Atomic uint32_t g_pub_words[GAME_TILE_WORDS];

static bool     game_tile_newer (game_tile_state_t const * p_state,
                                 uint16_t seq);
static void     game_tile_accept (game_tile_state_t * p_state, uint16_t seq);
static void     game_tile_mole_on (game_tile_state_t * p_state, int64_t t_show,
                                   uint16_t up_ms);
static void     game_tile_feedback (game_tile_state_t * p_state,
                                    uint8_t result, int64_t now_local_us);
static void     game_tile_set_wire (uint8_t * p_wire, uint32_t led,
                                    uint8_t red, uint8_t green, uint8_t blue);

/**
 * @brief  Clear the game state.
 * @param  p_state  State; must not be NULL.
 */
void game_tile_init (game_tile_state_t * p_state)
{
    (void)memset(p_state, 0, sizeof(*p_state)); /* Returns the state. */
}

/**
 * @brief  MOLE_ON for this tile: ACKed; applied when the seq is newer.
 * @param  p_state     State.
 * @param  p_buf       Datagram.
 * @param  len         Its length.
 * @param  p_header    Its header.
 * @param  p_acked_seq Receives the seq to ACK.
 * @return true when it is for this tile.
 */
static bool game_tile_on_mole (game_tile_state_t * p_state, uint8_t node_id,
                               uint8_t const * p_buf, uint32_t len,
                               proto_header_t const * p_header,
                               uint16_t * p_acked_seq)
{
    proto_mole_on_t msg;
    bool            b_mine = (proto_decode_mole_on(p_buf, len, &msg)) &&
                             (msg.node_id == node_id);

    if (b_mine)
    {
        *p_acked_seq = p_header->seq;

        if (game_tile_newer(p_state, p_header->seq))
        {
            game_tile_accept(p_state, p_header->seq);
            /* Pico times fit int64_t (about 292,000 years of us). */
            game_tile_mole_on(p_state, (int64_t)msg.t_show, msg.up_ms);
        }
    }

    return b_mine;
}

/**
 * @brief  FEEDBACK for this tile: ACKed; the flash starts when newer.
 * @param  p_state       State.
 * @param  node_id       This tile.
 * @param  p_buf         Datagram.
 * @param  len           Its length.
 * @param  p_header      Its header.
 * @param  now_local_us  Tile local time, microseconds.
 * @param  p_acked_seq   Receives the seq to ACK.
 * @return true when it is for this tile.
 */
static bool game_tile_on_feedback (game_tile_state_t * p_state,
                                   uint8_t node_id, uint8_t const * p_buf,
                                   uint32_t len,
                                   proto_header_t const * p_header,
                                   int64_t now_local_us,
                                   uint16_t * p_acked_seq)
{
    proto_feedback_msg_t msg;
    bool                 b_mine = (proto_decode_feedback(p_buf, len, &msg)) &&
                                  (msg.node_id == node_id);

    if (b_mine)
    {
        *p_acked_seq = p_header->seq;

        if (game_tile_newer(p_state, p_header->seq))
        {
            game_tile_accept(p_state, p_header->seq);
            game_tile_feedback(p_state, msg.result, now_local_us);
        }
    }

    return b_mine;
}

/**
 * @brief  GAME_CONTROL for this tile: START or END when newer; ACKed.
 * @param  p_state     State.
 * @param  node_id     This tile.
 * @param  p_buf       Datagram.
 * @param  len         Its length.
 * @param  p_header    Its header.
 * @param  p_acked_seq Receives the seq to ACK.
 * @return true when it is for this tile.
 */
static bool game_tile_on_control (game_tile_state_t * p_state, uint8_t node_id,
                                  uint8_t const * p_buf, uint32_t len,
                                  proto_header_t const * p_header,
                                  uint16_t * p_acked_seq)
{
    proto_game_control_t msg;
    bool                 b_mine =
        (proto_decode_game_control(p_buf, len, &msg)) &&
        (msg.node_id == node_id);

    if (b_mine)
    {
        *p_acked_seq = p_header->seq;

        if (game_tile_newer(p_state, p_header->seq))
        {
            game_tile_accept(p_state, p_header->seq);

            if (PROTO_GAME_START == msg.command)
            {
                p_state->b_game     = 1u;
                p_state->b_mole     = 0u;
                p_state->b_flash    = 0u;
                p_state->b_score    = 0u;
                p_state->score_mask = 0u;
            }
            else if (PROTO_GAME_END == msg.command)
            {
                /* The score stays on the tile until the next START. */
                p_state->b_game  = 0u;
                p_state->b_mole  = 0u;
                p_state->b_flash = 0u;
            }
            else
            {
                /* Mode codes are the PC's; the Pico does not send them. */
            }
        }
    }

    return b_mine;
}

/**
 * @brief  SCORE_MASK for this tile: ACKed; the mask is held when newer.
 * @param  p_state     State.
 * @param  node_id     This tile.
 * @param  p_buf       Datagram.
 * @param  len         Its length.
 * @param  p_header    Its header.
 * @param  p_acked_seq Receives the seq to ACK.
 * @return true when it is for this tile.
 */
static bool game_tile_on_score (game_tile_state_t * p_state, uint8_t node_id,
                                uint8_t const * p_buf, uint32_t len,
                                proto_header_t const * p_header,
                                uint16_t * p_acked_seq)
{
    proto_score_mask_t msg;
    bool               b_mine =
        (proto_decode_score_mask(p_buf, len, &msg)) &&
        (msg.node_id == node_id);

    if (b_mine)
    {
        *p_acked_seq = p_header->seq;

        if (game_tile_newer(p_state, p_header->seq))
        {
            game_tile_accept(p_state, p_header->seq);
            p_state->b_score    = 1u;
            p_state->score_mask = msg.mask;
        }
    }

    return b_mine;
}

/**
 * @brief  Handle one datagram from the Pico (see game_tile.h).
 * @param  p_state      State; must not be NULL.
 * @param  node_id      This tile's Node ID, 1 to 9.
 * @param  p_buf        Datagram.
 * @param  len          Its length.
 * @param  now_local_us Tile local time, microseconds (the flash clock).
 * @param  p_acked_seq  Receives the header seq to ACK.
 * @return true when the message is for this tile and must be ACKed.
 */
bool game_tile_on_pico (game_tile_state_t * p_state, uint8_t node_id,
                        uint8_t const * p_buf, uint32_t len,
                        int64_t now_local_us, uint16_t * p_acked_seq)
{
    proto_header_t header;
    bool           b_ack = false;

    if (proto_decode_header(p_buf, len, &header))
    {
        /* A decoded type is below 24, a valid proto_type_t. */
        proto_type_t const type = (proto_type_t)header.type;

        if (PROTO_TYPE_MOLE_ON == type)
        {
            b_ack = game_tile_on_mole(p_state, node_id, p_buf, len, &header,
                                      p_acked_seq);
        }
        else if (PROTO_TYPE_FEEDBACK == type)
        {
            b_ack = game_tile_on_feedback(p_state, node_id, p_buf, len,
                                          &header, now_local_us, p_acked_seq);
        }
        else if (PROTO_TYPE_GAME_CONTROL == type)
        {
            b_ack = game_tile_on_control(p_state, node_id, p_buf, len, &header,
                                         p_acked_seq);
        }
        else if (PROTO_TYPE_SCORE_MASK == type)
        {
            b_ack = game_tile_on_score(p_state, node_id, p_buf, len, &header,
                                       p_acked_seq);
        }
        else
        {
            /* Not a game message: the caller routes it elsewhere. */
        }
    }

    return b_ack;
}

/**
 * @brief  Fill the 64 LEDs for one moment (see game_tile.h).
 * @param  p_state       State; must not be NULL.
 * @param  now_pico_us   Pico corrected time, microseconds.
 * @param  now_local_us  Tile local time, microseconds.
 * @param  orientation   Tile rotation, 0 to 3.
 * @param  p_wire        Receives GAME_TILE_WIRE_BYTES bytes.
 */
void game_tile_fill_wire (game_tile_state_t const * p_state,
                          int64_t now_pico_us, int64_t now_local_us,
                          uint8_t orientation, uint8_t * p_wire)
{
    bool const b_flash = (0u != p_state->b_flash) &&
                         (now_local_us < p_state->flash_until_us);
    bool const b_mole  = (0u != p_state->b_mole) &&
                         (now_pico_us >= p_state->show_us) &&
                         (now_pico_us < p_state->hide_us);

    for (uint32_t led = 0u; led < GAME_TILE_LEDS; led++)
    {
        if (b_flash)
        {
            if (GAME_TILE_RESULT_HIT == p_state->flash_result)
            {
                game_tile_set_wire(p_wire, led, 0u, GAME_TILE_MAX, 0u);
            }
            else
            {
                game_tile_set_wire(p_wire, led, GAME_TILE_MAX, 0u, 0u);
            }
        }
        else if (b_mole)
        {
            game_tile_set_wire(p_wire, led, GAME_TILE_MOLE_R, GAME_TILE_MOLE_G,
                               GAME_TILE_MOLE_B);
        }
        else if (0u != p_state->b_score)
        {
            /* The mask is before the rotation: look up the source LED. */
            uint32_t const source = display_frame_source_led(led, orientation);
            bool const     b_lit  = (0u != ((p_state->score_mask >> source) &
                                            UINT64_C(1)));

            if (b_lit)
            {
                game_tile_set_wire(p_wire, led, GAME_TILE_SCORE_V,
                                   GAME_TILE_SCORE_V, GAME_TILE_SCORE_V);
            }
            else
            {
                game_tile_set_wire(p_wire, led, 0u, 0u, 0u);
            }
        }
        else
        {
            game_tile_set_wire(p_wire, led, 0u, 0u, 0u);
        }
    }
}

/**
 * @brief  Encode a TAP (see game_tile.h).
 * @param  p_buf       Receives the bytes.
 * @param  session_id  Session on the header.
 * @param  seq         Header seq.
 * @param  node_id     This tile.
 * @param  peak        tap_detect peak.
 * @param  t_tap_us    Tap time, Pico corrected clock.
 * @return Bytes written, or 0 when out of range.
 */
uint32_t game_tile_encode_tap (uint8_t * p_buf, uint8_t session_id,
                               uint16_t seq, uint8_t node_id, uint16_t peak,
                               int64_t t_tap_us)
{
    uint32_t len = 0u;

    if ((node_id >= 1u) && (node_id <= PROTO_NODE_MAX) && (t_tap_us >= 0))
    {
        proto_tap_t const msg = {
            .header  = {.type       = PROTO_TYPE_TAP,
                        .session_id = session_id,
                        .seq        = seq},
            .node_id = node_id,
            .peak    = peak,
            .t_tap   = (uint64_t)t_tap_us, /* Checked >= 0 above. */
        };

        len = proto_encode_tap(&msg, p_buf, PROTO_TAP_BYTES);
    }

    return len;
}

/**
 * @brief  Encode the ACK to the Pico (see game_tile.h).
 * @param  p_buf       Receives the bytes.
 * @param  session_id  Session on the header.
 * @param  seq         Header seq of the ACK.
 * @param  node_id     This tile.
 * @param  acked_seq   Seq acknowledged.
 * @return Bytes written.
 */
uint32_t game_tile_encode_ack (uint8_t * p_buf, uint8_t session_id,
                               uint16_t seq, uint8_t node_id,
                               uint16_t acked_seq)
{
    proto_ack_t const msg = {
        .header    = {.type       = PROTO_TYPE_ACK,
                      .session_id = session_id,
                      .seq        = seq},
        .node_id   = node_id,
        .acked_seq = acked_seq,
    };

    return proto_encode_ack(&msg, p_buf, PROTO_ACK_BYTES);
}

/**
 * @brief  Publish the state (writer side of the seqlock).
 * @param  p_state  State to publish.
 */
void game_tile_publish (game_tile_state_t const * p_state)
{
    uint32_t words[GAME_TILE_WORDS];
    uint32_t const seq = atomic_load_explicit(&g_pub_seq,
                                              memory_order_relaxed);

    (void)memcpy(words, p_state, sizeof(words)); /* Returns words. */

    atomic_store_explicit(&g_pub_seq, seq + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);

    for (uint32_t word = 0u; word < GAME_TILE_WORDS; word++)
    {
        atomic_store_explicit(&g_pub_words[word], words[word],
                              memory_order_relaxed);
    }

    atomic_store_explicit(&g_pub_seq, seq + 2u, memory_order_release);
}

/**
 * @brief  Read the published state (reader side of the seqlock).
 * @param  p_state  Receives the state.
 */
void game_tile_read (game_tile_state_t * p_state)
{
    uint32_t words[GAME_TILE_WORDS];
    uint32_t seq_before = 0u;
    uint32_t seq_after  = 0u;

    do
    {
        seq_before = atomic_load_explicit(&g_pub_seq, memory_order_acquire);

        for (uint32_t word = 0u; word < GAME_TILE_WORDS; word++)
        {
            words[word] = atomic_load_explicit(&g_pub_words[word],
                                               memory_order_relaxed);
        }

        atomic_thread_fence(memory_order_acquire);
        seq_after = atomic_load_explicit(&g_pub_seq, memory_order_relaxed);
    } while ((0u != (seq_before & 1u)) || (seq_before != seq_after));

    (void)memcpy(p_state, words, sizeof(words)); /* Returns the state. */
}

/**
 * @brief  Whether a command seq is newer than the last one accepted.
 * @param  p_state  State.
 * @param  seq      Header seq received.
 * @return true when it is new (or none was accepted yet).
 */
static bool game_tile_newer (game_tile_state_t const * p_state, uint16_t seq)
{
    /* Both are 16-bit seqs; the subtraction wraps modulo 2^16. */
    uint16_t const ahead = (uint16_t)(seq - (uint16_t)p_state->seq);

    return (0u == p_state->b_seq_known) ||
           ((0u != ahead) && (ahead < GAME_TILE_SEQ_HALF));
}

/**
 * @brief  Record an accepted command seq.
 * @param  p_state  State.
 * @param  seq      Seq accepted.
 */
static void game_tile_accept (game_tile_state_t * p_state, uint16_t seq)
{
    p_state->b_seq_known = 1u;
    p_state->seq         = seq;
}

/**
 * @brief  MOLE_ON: a mole from t_show for up_ms, in the Pico's clock.
 * @param  p_state  State.
 * @param  t_show   Show time, Pico corrected clock.
 * @param  up_ms    Up time, milliseconds.
 */
static void game_tile_mole_on (game_tile_state_t * p_state, int64_t t_show,
                               uint16_t up_ms)
{
    p_state->b_mole = 1u;
    p_state->show_us = t_show;
    /* up_ms is at most 2,000, so the product fits int64_t. */
    p_state->hide_us = t_show + ((int64_t)up_ms * GAME_TILE_US_PER_MS);
}

/**
 * @brief  FEEDBACK: green on a hit (the mole ends), red on a miss (the mole
 *         stays). The decoy code is reserved and does nothing here.
 * @param  p_state       State.
 * @param  result        proto_feedback_t value.
 * @param  now_local_us  Tile local time, microseconds.
 */
static void game_tile_feedback (game_tile_state_t * p_state, uint8_t result,
                                int64_t now_local_us)
{
    if ((GAME_TILE_RESULT_HIT == result) || (GAME_TILE_RESULT_MISS == result))
    {
        p_state->b_flash      = 1u;
        p_state->flash_result = result;
        p_state->flash_until_us = now_local_us + GAME_TILE_FLASH_US;

        if (GAME_TILE_RESULT_HIT == result)
        {
            p_state->b_mole = 0u;
        }
    }
}

/**
 * @brief  Write one LED in WS2812B order (G, R, B).
 * @param  p_wire  Wire buffer, GAME_TILE_WIRE_BYTES bytes.
 * @param  led     LED 0 to 63.
 * @param  red     Red channel, 0 to 8.
 * @param  green   Green channel, 0 to 8.
 * @param  blue    Blue channel, 0 to 8.
 */
static void game_tile_set_wire (uint8_t * p_wire, uint32_t led, uint8_t red,
                                uint8_t green, uint8_t blue)
{
    p_wire[(3u * led) + 0u] = green;
    p_wire[(3u * led) + 1u] = red;
    p_wire[(3u * led) + 2u] = blue;
}

/*** end of file ***/
