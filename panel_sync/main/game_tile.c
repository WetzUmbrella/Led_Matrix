/**
 * @file    game_tile.c
 * @brief   Whack-a-mole on the tile (see game_tile.h).
 *
 * Owner: Cheston (Buddy 3) and Jia Jun (Buddy 5). Written for build step 7;
 * round phases, sprites and the picture list for step 3.
 *
 * The seqlock follows playback_control.c: one writer (the Network Task),
 * readers on other tasks, 32-bit atomic words, an even seq when the data is
 * stable (DEVIATION D-1: C11 stdatomic.h).
 */
#include "game_tile.h"

#include <stdatomic.h>
#include <string.h>

#include "display_frame.h"
#include "game_sprites.h"

/** Microseconds per millisecond. */
#define GAME_TILE_US_PER_MS INT64_C(1000)

/** Seq comparison half: a seq is newer when 1 to 32,767 ahead. */
#define GAME_TILE_SEQ_HALF UINT16_C(0x8000)

/** FEEDBACK results on the wire (layout_table.md 5.9). */
#define GAME_TILE_RESULT_HIT  ((uint32_t)PROTO_FEEDBACK_HIT)  /* Wire value. */
#define GAME_TILE_RESULT_MISS ((uint32_t)PROTO_FEEDBACK_MISS) /* Wire value. */

/** Target kinds the tile draws (layout_table.md 5.12). */
#define GAME_TILE_KIND_MOLE  ((uint32_t)PROTO_TARGET_MOLE)  /* Wire value. */
#define GAME_TILE_KIND_START ((uint32_t)PROTO_TARGET_START) /* Wire value. */

/** Words of the state, for the seqlock. */
#define GAME_TILE_WORDS (sizeof(game_tile_state_t) / sizeof(uint32_t))

_Static_assert(0u == (sizeof(game_tile_state_t) % sizeof(uint32_t)),
               "game_tile_state_t must be a whole number of 32-bit words");

/** The seqlock: even when the words are stable, odd during a publish. */
static _Atomic uint32_t g_pub_seq = 0u;
static _Atomic uint32_t g_pub_words[GAME_TILE_WORDS];

static bool game_tile_ahead (uint16_t seq, uint32_t than);
static bool game_tile_newer (game_tile_state_t const * p_state, uint16_t seq);
static void game_tile_accept (game_tile_state_t * p_state, uint16_t seq);
static void game_tile_control (game_tile_state_t * p_state,
                               proto_game_control_t const * p_msg,
                               uint16_t seq);
static void game_tile_clear_older (game_tile_state_t * p_state, uint16_t seq);
static void game_tile_picture (game_tile_state_t * p_state,
                               proto_wall_picture_t const * p_msg,
                               uint16_t seq);
static void game_tile_mole_on (game_tile_state_t * p_state,
                               proto_mole_on_t const * p_msg, uint16_t seq);
static void game_tile_feedback (game_tile_state_t * p_state, uint8_t result,
                                int64_t now_local_us);
static game_tile_pic_t const * game_tile_showing (
    game_tile_state_t const * p_state, int64_t now_pico_us);
static void game_tile_draw_target (game_tile_state_t const * p_state,
                                   int64_t now_pico_us,
                                   game_canvas_t * p_canvas);
static bool game_tile_flash_on (game_tile_state_t const * p_state,
                                int64_t now_local_us);

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
 * @param  node_id     This tile.
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

    if ((b_mine) && (game_tile_newer(p_state, p_header->seq)))
    {
        game_tile_accept(p_state, p_header->seq);
        game_tile_mole_on(p_state, &msg, p_header->seq);
    }

    if (b_mine)
    {
        *p_acked_seq = p_header->seq;
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

    if ((b_mine) && (game_tile_newer(p_state, p_header->seq)))
    {
        game_tile_accept(p_state, p_header->seq);
        game_tile_feedback(p_state, msg.result, now_local_us);
    }

    if (b_mine)
    {
        *p_acked_seq = p_header->seq;
    }

    return b_mine;
}

/**
 * @brief  GAME_CONTROL for this tile: applied when newer than the last
 *         GAME_CONTROL; ACKed.
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

    if ((b_mine) && ((0u == p_state->b_ctl_known) ||
                     (game_tile_ahead(p_header->seq, p_state->ctl_seq))))
    {
        p_state->b_ctl_known = 1u;
        p_state->ctl_seq     = p_header->seq;

        if (game_tile_newer(p_state, p_header->seq))
        {
            game_tile_accept(p_state, p_header->seq);
        }

        game_tile_control(p_state, &msg, p_header->seq);
    }

    if (b_mine)
    {
        *p_acked_seq = p_header->seq;
    }

    return b_mine;
}

/**
 * @brief  WALL_PICTURE for this tile: ACKed; stored when newer than its
 *         entry and than the last clearing GAME_CONTROL.
 * @param  p_state     State.
 * @param  node_id     This tile.
 * @param  p_buf       Datagram.
 * @param  len         Its length.
 * @param  p_header    Its header.
 * @param  p_acked_seq Receives the seq to ACK.
 * @return true when it is for this tile.
 */
static bool game_tile_on_picture (game_tile_state_t * p_state, uint8_t node_id,
                                  uint8_t const * p_buf, uint32_t len,
                                  proto_header_t const * p_header,
                                  uint16_t * p_acked_seq)
{
    proto_wall_picture_t msg;
    bool                 b_mine =
        (proto_decode_wall_picture(p_buf, len, &msg)) &&
        (msg.node_id == node_id);

    if (b_mine)
    {
        *p_acked_seq = p_header->seq;

        if (game_tile_newer(p_state, p_header->seq))
        {
            game_tile_accept(p_state, p_header->seq);
        }

        game_tile_picture(p_state, &msg, p_header->seq);
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
        else if (PROTO_TYPE_WALL_PICTURE == type)
        {
            b_ack = game_tile_on_picture(p_state, node_id, p_buf, len,
                                         &header, p_acked_seq);
        }
        else
        {
            /* Not a game message: the caller routes it elsewhere. */
        }
    }

    return b_ack;
}

/**
 * @brief  Whether the game owns the display (see game_tile.h).
 * @param  p_state       State.
 * @param  now_local_us  Tile local time.
 * @return true when the game draws the tile.
 */
bool game_tile_active (game_tile_state_t const * p_state,
                       int64_t now_local_us)
{
    bool b_pics = false;

    for (uint32_t index = 0u; index < GAME_TILE_PICS; index++)
    {
        b_pics = b_pics || (0u != p_state->pic[index].b_valid);
    }

    return (0u != p_state->b_round) || (0u != p_state->b_tap_test) ||
           (b_pics) || (game_tile_flash_on(p_state, now_local_us));
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
    game_canvas_t                 canvas;
    game_tile_pic_t const * const p_pic =
        game_tile_showing(p_state, now_pico_us);
    int64_t const since = now_local_us - p_state->flash_start_us;

    game_sprite_clear(&canvas);

    if ((0u != p_state->b_round) || (0u != p_state->b_tap_test))
    {
        game_sprite_hole(&canvas);
    }

    game_tile_draw_target(p_state, now_pico_us, &canvas);

    if (NULL == p_pic)
    {
        /* No picture showing: the hole and the target stay. */
    }
    else if (PROTO_PIC_SPARKLE == p_pic->effect)
    {
        /* Below 8 colours: fits uint8_t. */
        game_sprite_sparkle(&canvas, (uint8_t)p_pic->colour, now_pico_us);
    }
    else
    {
        /* Below 8 colours: fits uint8_t. */
        game_sprite_picture(&canvas, p_pic->mask, (uint8_t)p_pic->colour);
    }

    if (!game_tile_flash_on(p_state, now_local_us))
    {
        /* No flash: the layers below stand. */
    }
    else if (GAME_TILE_RESULT_HIT == p_state->flash_result)
    {
        game_sprite_hit(&canvas, since, 0u != p_state->b_squash);
    }
    else
    {
        game_sprite_wrong(&canvas, since);
    }

    for (uint32_t led = 0u; led < GAME_TILE_LEDS; led++)
    {
        /* The canvas is upright: each LED shows its source pixel. */
        uint32_t const   source = display_frame_source_led(led, orientation);
        game_rgb_t const pixel  = canvas.px[source];

        p_wire[(3u * led) + 0u] = pixel.red;
        p_wire[(3u * led) + 1u] = pixel.green;
        p_wire[(3u * led) + 2u] = pixel.blue;
    }
}

/**
 * @brief  The game-mode pulse for a frame (see game_tile.h).
 * @param  p_state        State.
 * @param  prev_pico_us   Pico time of the previous frame.
 * @param  now_pico_us    Pico time of this frame.
 * @param  prev_local_us  Local time of the previous frame.
 * @param  now_local_us   Local time of this frame.
 * @return Pulse width, microseconds, or 0.
 */
uint32_t game_tile_pulse_us (game_tile_state_t const * p_state,
                             int64_t prev_pico_us, int64_t now_pico_us,
                             int64_t prev_local_us, int64_t now_local_us)
{
    uint32_t   width = 0u;
    bool const b_mole =
        (0u != p_state->b_mole) && (GAME_TILE_KIND_MOLE == p_state->kind) &&
        (p_state->show_us > prev_pico_us) && (p_state->show_us <= now_pico_us);
    bool const b_hit =
        (0u != p_state->b_flash) &&
        (GAME_TILE_RESULT_HIT == p_state->flash_result) &&
        (p_state->flash_start_us > prev_local_us) &&
        (p_state->flash_start_us <= now_local_us);

    if (b_mole)
    {
        width = GAME_TILE_PULSE_MOLE_US;
    }
    else if (b_hit)
    {
        width = GAME_TILE_PULSE_HIT_US;
    }
    else
    {
        /* No event starts in this frame. */
    }

    return width;
}

/**
 * @brief  ARM's tap threshold in mg.
 * @param  p_state  State.
 * @return mg, or 0 for the tile's default.
 */
uint32_t game_tile_threshold_mg (game_tile_state_t const * p_state)
{
    return p_state->threshold * PROTO_THRESHOLD_STEP_MG;
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
 * @brief  Whether a 16-bit seq is 1 to 32,767 ahead of another.
 * @param  seq   Seq received.
 * @param  than  Seq it is compared with (a 16-bit value kept in 32 bits).
 * @return true when seq is newer.
 */
static bool game_tile_ahead (uint16_t seq, uint32_t than)
{
    /* Both are 16-bit seqs; the subtraction wraps modulo 2^16. */
    uint16_t const ahead = (uint16_t)(seq - (uint16_t)than);

    return (0u != ahead) && (ahead < GAME_TILE_SEQ_HALF);
}

/**
 * @brief  Whether a seq is newer than every message accepted.
 * @param  p_state  State.
 * @param  seq      Header seq received.
 * @return true when it is new (or none was accepted yet).
 */
static bool game_tile_newer (game_tile_state_t const * p_state, uint16_t seq)
{
    return (0u == p_state->b_seq_known) ||
           (game_tile_ahead(seq, p_state->seq));
}

/**
 * @brief  Record an accepted message seq.
 * @param  p_state  State.
 * @param  seq      Seq accepted.
 */
static void game_tile_accept (game_tile_state_t * p_state, uint16_t seq)
{
    p_state->b_seq_known = 1u;
    p_state->seq         = seq;
}

/**
 * @brief  Apply a GAME_CONTROL (layout_table.md 5.10).
 * @param  p_state  State.
 * @param  p_msg    The command.
 * @param  seq      Its header seq.
 */
static void game_tile_control (game_tile_state_t * p_state,
                               proto_game_control_t const * p_msg,
                               uint16_t seq)
{
    if (PROTO_GAME_ARM == p_msg->command)
    {
        game_tile_clear_older(p_state, seq);
        p_state->b_round    = 1u;
        p_state->b_game     = 0u;
        p_state->b_tap_test = 0u;
        p_state->b_flash    = 0u;
        p_state->threshold  = p_msg->threshold;
    }
    else if (PROTO_GAME_START == p_msg->command)
    {
        game_tile_clear_older(p_state, seq);
        p_state->b_round    = 1u;
        p_state->b_game     = 1u;
        p_state->b_tap_test = 0u;
        p_state->b_flash    = 0u;
    }
    else if (PROTO_GAME_END == p_msg->command)
    {
        /* The results list stays on the tile until the next ARM or START. */
        p_state->b_round = 0u;
        p_state->b_game  = 0u;
        p_state->b_mole  = 0u;
        p_state->b_flash = 0u;
    }
    else if (PROTO_GAME_TAP_TEST_ON == p_msg->command)
    {
        p_state->b_tap_test = 1u;
    }
    else if (PROTO_GAME_TAP_TEST_OFF == p_msg->command)
    {
        p_state->b_tap_test = 0u;
    }
    else
    {
        /* MODE and START_NOW are the PC's; the Pico does not send them. */
    }
}

/**
 * @brief  ARM or START: clear the target and the picture entries older than
 *         the command (a newer one arrived first and stays), and remember
 *         the command, so an older picture that arrives late is dropped.
 * @param  p_state  State.
 * @param  seq      The command's seq.
 */
static void game_tile_clear_older (game_tile_state_t * p_state, uint16_t seq)
{
    /* target_seq holds a 16-bit seq: the narrowing keeps it. */
    if ((0u != p_state->b_mole) &&
        (!game_tile_ahead((uint16_t)p_state->target_seq, seq)))
    {
        p_state->b_mole = 0u;
    }

    for (uint32_t index = 0u; index < GAME_TILE_PICS; index++)
    {
        game_tile_pic_t * const p_pic = &p_state->pic[index];

        /* seq holds a 16-bit seq: the narrowing keeps it. */
        if ((0u != p_pic->b_valid) &&
            (!game_tile_ahead((uint16_t)p_pic->seq, seq)))
        {
            p_pic->b_valid = 0u;
        }
    }

    if ((0u == p_state->b_clear_known) ||
        (game_tile_ahead(seq, p_state->clear_seq)))
    {
        p_state->b_clear_known = 1u;
        p_state->clear_seq     = seq;
    }
}

/**
 * @brief  Store one picture entry, or clear the list (effect CLEAR), when
 *         the seq is newer than the entry and than the last clear.
 * @param  p_state  State.
 * @param  p_msg    The entry.
 * @param  seq      Its header seq.
 */
static void game_tile_picture (game_tile_state_t * p_state,
                               proto_wall_picture_t const * p_msg,
                               uint16_t seq)
{
    /* The decoder checked slot 0 to 3. */
    game_tile_pic_t * const p_pic = &p_state->pic[p_msg->slot];
    bool const b_fresh =
        ((0u == p_state->b_clear_known) ||
         (game_tile_ahead(seq, p_state->clear_seq))) &&
        ((0u == p_pic->b_valid) || (game_tile_ahead(seq, p_pic->seq)));

    if ((b_fresh) && (PROTO_PIC_CLEAR == p_msg->effect))
    {
        for (uint32_t index = 0u; index < GAME_TILE_PICS; index++)
        {
            p_state->pic[index].b_valid = 0u;
        }

        p_state->b_clear_known = 1u;
        p_state->clear_seq     = seq;
    }
    else if (b_fresh)
    {
        p_pic->b_valid  = 1u;
        p_pic->seq      = seq;
        p_pic->effect   = p_msg->effect;
        p_pic->colour   = p_msg->colour;
        p_pic->b_repeat = p_msg->b_repeat;
        p_pic->dur_ms   = p_msg->dur_ms;
        /* Pico times fit int64_t (about 292,000 years of us). */
        p_pic->t_show   = (int64_t)p_msg->t_show;
        p_pic->mask     = p_msg->mask;
    }
    else
    {
        /* Older than its entry or than the last clear: dropped. */
    }
}

/**
 * @brief  MOLE_ON: a target from t_show; a mole for up_ms, a start sprite
 *         (up_ms 0) until tapped or cleared.
 * @param  p_state  State.
 * @param  p_msg    The target.
 * @param  seq      Its header seq.
 */
static void game_tile_mole_on (game_tile_state_t * p_state,
                               proto_mole_on_t const * p_msg, uint16_t seq)
{
    p_state->b_mole     = 1u;
    p_state->kind       = p_msg->kind;
    p_state->target_seq = seq;
    /* Pico times fit int64_t (about 292,000 years of us). */
    p_state->show_us    = (int64_t)p_msg->t_show;
    /* up_ms is at most 4,000, so the product fits int64_t. */
    p_state->hide_us    = (0u == p_msg->up_ms)
                              ? INT64_MAX
                              : (p_state->show_us +
                                 ((int64_t)p_msg->up_ms * GAME_TILE_US_PER_MS));
}

/**
 * @brief  FEEDBACK: a hit ends the target and plays the burst (squashing a
 *         mole that was up); a miss blinks the red X and the target stays.
 *         The decoy code is reserved and does nothing here.
 * @param  p_state       State.
 * @param  result        proto_feedback_t value.
 * @param  now_local_us  Tile local time, microseconds.
 */
static void game_tile_feedback (game_tile_state_t * p_state, uint8_t result,
                                int64_t now_local_us)
{
    if ((GAME_TILE_RESULT_HIT == result) || (GAME_TILE_RESULT_MISS == result))
    {
        p_state->b_flash        = 1u;
        p_state->flash_result   = result;
        p_state->flash_start_us = now_local_us;

        if (GAME_TILE_RESULT_HIT == result)
        {
            p_state->b_squash = ((0u != p_state->b_mole) &&
                                 (GAME_TILE_KIND_MOLE == p_state->kind))
                                    ? 1u
                                    : 0u;
            p_state->b_mole   = 0u;
        }
    }
}

/**
 * @brief  The picture entry showing at a time: the repeat entry loops the
 *         list back to entry 1, then the latest started entry that has not
 *         ended shows.
 * @param  p_state      State.
 * @param  now_pico_us  Pico corrected time.
 * @return The entry, or NULL when none shows.
 */
static game_tile_pic_t const * game_tile_showing (
    game_tile_state_t const * p_state, int64_t now_pico_us)
{
    game_tile_pic_t const * p_best = NULL;
    game_tile_pic_t const * const p_first = &p_state->pic[1];
    int64_t                 when   = now_pico_us;

    for (uint32_t index = 0u; index < GAME_TILE_PICS; index++)
    {
        game_tile_pic_t const * const p_pic = &p_state->pic[index];
        int64_t const end = p_pic->t_show +
                            ((int64_t)p_pic->dur_ms * GAME_TILE_US_PER_MS);

        /* The loop runs from entry 1's start to the repeat entry's end. */
        if ((0u != p_pic->b_valid) && (0u != p_pic->b_repeat) &&
            (0u != p_pic->dur_ms) && (0u != p_first->b_valid) &&
            (end > p_first->t_show) && (when >= end))
        {
            int64_t const period = end - p_first->t_show;

            when = p_first->t_show + ((when - p_first->t_show) % period);
        }
    }

    for (uint32_t index = 0u; index < GAME_TILE_PICS; index++)
    {
        game_tile_pic_t const * const p_pic = &p_state->pic[index];
        /* dur_ms is at most 8,191, so the product fits int64_t. */
        int64_t const end = (0u == p_pic->dur_ms)
                                ? INT64_MAX
                                : (p_pic->t_show +
                                   ((int64_t)p_pic->dur_ms *
                                    GAME_TILE_US_PER_MS));

        if ((0u != p_pic->b_valid) && (when >= p_pic->t_show) &&
            (when < end) &&
            ((NULL == p_best) || (p_pic->t_show > p_best->t_show)))
        {
            p_best = p_pic;
        }
    }

    return p_best;
}

/**
 * @brief  The target at a time: the start sprite from its show time; a mole
 *         rising, up, then sinking after its hide time.
 * @param  p_state      State.
 * @param  now_pico_us  Pico corrected time.
 * @param  p_canvas     Canvas drawn on.
 */
static void game_tile_draw_target (game_tile_state_t const * p_state,
                                   int64_t now_pico_us,
                                   game_canvas_t * p_canvas)
{
    if ((0u == p_state->b_mole) || (now_pico_us < p_state->show_us))
    {
        /* No target, or not shown yet. */
    }
    else if (GAME_TILE_KIND_START == p_state->kind)
    {
        game_sprite_start(p_canvas, now_pico_us);
    }
    else if (GAME_TILE_KIND_MOLE != p_state->kind)
    {
        /* Rotate, golden and decoy are reserved for the stretch goals. */
    }
    else if (now_pico_us < p_state->hide_us)
    {
        game_sprite_mole(p_canvas,
                         game_sprite_rows_up(now_pico_us - p_state->show_us));
    }
    else
    {
        /* Missed: it sinks back, the reverse of the rise. */
        uint32_t const gone =
            game_sprite_rows_up(now_pico_us - p_state->hide_us);

        game_sprite_mole(p_canvas, GAME_SPRITE_SIDE - gone);
    }
}

/**
 * @brief  Whether the FEEDBACK animation still runs.
 * @param  p_state       State.
 * @param  now_local_us  Tile local time.
 * @return true while the hit burst or the red X shows.
 */
static bool game_tile_flash_on (game_tile_state_t const * p_state,
                                int64_t now_local_us)
{
    int64_t const length = (GAME_TILE_RESULT_HIT == p_state->flash_result)
                               ? GAME_SPRITE_HIT_US
                               : GAME_SPRITE_WRONG_US;

    return (0u != p_state->b_flash) &&
           (now_local_us >= p_state->flash_start_us) &&
           (now_local_us < (p_state->flash_start_us + length));
}

/*** end of file ***/
