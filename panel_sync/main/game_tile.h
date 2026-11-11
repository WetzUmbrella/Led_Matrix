/**
 * @file    game_tile.h
 * @brief   Whack-a-mole on the tile (whack-a-mole re-plan, build step 7):
 *          the tile's game state, the handling of MOLE_ON, FEEDBACK,
 *          GAME_CONTROL, SCORE_MASK and the ACK of a TAP, the pixels it draws
 *          (a solid mole, a green or red flash, the final score), the TAP and
 *          ACK encoders, and a seqlock that hands the state to the Display
 *          Engine. Pure C with no ESP-IDF call except the atomics of the
 *          seqlock (DEVIATION D-1, as playback_control.c), so the host test
 *          in panel_sync/test runs it on a PC.
 *
 * Owner: Cheston (Buddy 3) for the tile comms rows, Jia Jun (Buddy 5) for the
 * display, per the re-plan's report allocation. Written for build step 7.
 *
 * Rules on the tile (layout_table.md sections 3 and 5):
 * - The tile draws the mole itself: a solid fill from t_show until
 *   t_show + up_ms x 1000 us, in the Pico's corrected clock. No asset.
 * - FEEDBACK flashes the whole tile green (hit) or red (miss) for
 *   GAME_TILE_FLASH_US on the tile's own clock, and ends a mole on a hit.
 * - SCORE_MASK is the tile's 8 x 8 slice of the final score, before the
 *   tile's rotation; it stays on the tile until the next round starts.
 * - Every MOLE_ON, FEEDBACK, GAME_CONTROL and SCORE_MASK addressed to this
 *   tile is ACKed, a repeat too. Only a newer seq acts (the Pico shares one
 *   seq counter for these, layout_table.md section 10 item 13).
 */
#ifndef GAME_TILE_H
#define GAME_TILE_H

#include <stdbool.h>
#include <stdint.h>

#include "proto.h"

/** LEDs on one tile and wire bytes per tile (R, G, B, 3 per LED). */
#define GAME_TILE_LEDS 64u
#define GAME_TILE_WIRE_BYTES 192u

/** Flash length after a FEEDBACK, microseconds (plan choice: a quarter of a
 *  second, long enough to see on the wall, short enough for the next mole). */
#define GAME_TILE_FLASH_US 250000

/** The game state of one tile. Every field is a 32-bit or 64-bit integer, so
 *  the struct is a whole number of 32-bit words for the seqlock. */
typedef struct
{
    uint32_t b_game;        /**< 1 while a round runs (START seen, no END). */
    uint32_t b_mole;        /**< 1 when this tile has a mole window. */
    uint32_t b_flash;       /**< 1 while a FEEDBACK flash runs. */
    uint32_t flash_result;  /**< proto_feedback_t of the flash. */
    uint32_t b_score;       /**< 1 when a SCORE_MASK is held. */
    uint32_t b_seq_known;   /**< 1 once a command seq was accepted. */
    uint32_t seq;           /**< Newest accepted command seq. */
    uint32_t reserved;      /**< Keeps the struct a whole number of words. */
    int64_t  show_us;       /**< Mole show time, Pico corrected clock. */
    int64_t  hide_us;       /**< Mole hide time, Pico corrected clock. */
    int64_t  flash_until_us; /**< Flash end, tile local clock. */
    uint64_t score_mask;    /**< SCORE_MASK bits, before the rotation. */
} game_tile_state_t;

/**
 * @brief  Clear the game state (power-up, or the video takes the wall).
 * @param  p_state  State; must not be NULL.
 */
void game_tile_init (game_tile_state_t * p_state);

/**
 * @brief  Handle one datagram from the Pico on port 4210: MOLE_ON, FEEDBACK,
 *         GAME_CONTROL or SCORE_MASK. Other types are not for this module.
 * @param  p_state     State; must not be NULL.
 * @param  node_id     This tile's Node ID, 1 to 9.
 * @param  p_buf       Datagram.
 * @param  len         Its length.
 * @param  now_local_us Tile local time, microseconds (the flash clock).
 * @param  p_acked_seq Receives the header seq to ACK when the return is true.
 * @return true when the message is for this tile and must be ACKed.
 */
bool game_tile_on_pico (game_tile_state_t * p_state, uint8_t node_id,
                        uint8_t const * p_buf, uint32_t len,
                        int64_t now_local_us, uint16_t * p_acked_seq);

/**
 * @brief  Fill the 64 LEDs of the tile for one moment, in the panel's order
 *         (R, G, B per LED, LED 0 first), channel values 0 to 8. Priority:
 *         a flash, then the mole, then the held score; otherwise dark.
 * @param  p_state        State; must not be NULL.
 * @param  now_pico_us    Pico corrected time, microseconds (the mole clock).
 * @param  now_local_us   Tile local time, microseconds (the flash clock).
 * @param  orientation    Rotation to draw, 0 to 3: the measured orientation
 *                        with the mount offset (display_frame_mounted()).
 * @param  p_wire         Receives GAME_TILE_WIRE_BYTES bytes.
 */
void game_tile_fill_wire (game_tile_state_t const * p_state,
                          int64_t now_pico_us, int64_t now_local_us,
                          uint8_t orientation, uint8_t * p_wire);

/**
 * @brief  Encode a TAP for the Pico (port 4211, acked).
 * @param  p_buf      Receives the bytes; at least PROTO_TAP_BYTES.
 * @param  session_id Session on the header.
 * @param  seq        Header seq (the tile's TAP counter).
 * @param  node_id    This tile, 1 to 9.
 * @param  peak       tap_detect peak, LSB.
 * @param  t_tap_us  Tap time, Pico corrected clock, microseconds.
 * @return Bytes written, or 0 when the fields are out of range.
 */
uint32_t game_tile_encode_tap (uint8_t * p_buf, uint8_t session_id,
                               uint16_t seq, uint8_t node_id, uint16_t peak,
                               int64_t t_tap_us);

/**
 * @brief  Encode the ACK the tile sends to the Pico (port 4211). An ACK is not
 *         acked, so it takes its own counter's seq.
 * @param  p_buf      Receives the bytes; at least PROTO_ACK_BYTES.
 * @param  session_id Session on the header.
 * @param  seq        Header seq of the ACK.
 * @param  node_id    This tile, 1 to 9.
 * @param  acked_seq  Header seq of the message acknowledged.
 * @return Bytes written.
 */
uint32_t game_tile_encode_ack (uint8_t * p_buf, uint8_t session_id,
                               uint16_t seq, uint8_t node_id,
                               uint16_t acked_seq);

/**
 * @brief  Publish the state for the Display Engine (writer: the Network Task).
 *         Seqlock: the writer is the only writer.
 * @param  p_state  State to publish; must not be NULL.
 */
void game_tile_publish (game_tile_state_t const * p_state);

/**
 * @brief  Read the published state (reader: the Display Engine). A read that
 *         overlaps a publish is retried. Before any publish every field is 0.
 * @param  p_state  Receives the state; must not be NULL.
 */
void game_tile_read (game_tile_state_t * p_state);

#endif /* GAME_TILE_H */

/*** end of file ***/
