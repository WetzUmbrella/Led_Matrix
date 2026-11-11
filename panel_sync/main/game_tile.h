/**
 * @file    game_tile.h
 * @brief   Whack-a-mole on the tile (whack-a-mole re-plan, build steps 7 and
 *          3): the tile's game state, the handling of MOLE_ON (a target),
 *          FEEDBACK, GAME_CONTROL, WALL_PICTURE and the ACK of a TAP, the
 *          frame it draws (game_sprites.h), the game-mode GPIO4 event
 *          pulses (DS-13), the TAP and ACK encoders, and a seqlock that hands
 *          the state to the Display Engine. Pure C with no ESP-IDF call
 *          except the atomics of the seqlock (DEVIATION D-1, as
 *          playback_control.c), so the host test in panel_sync/test runs it
 *          on a PC.
 *
 * Owner: Cheston (Buddy 3) for the tile comms rows, Jia Jun (Buddy 5) for the
 * display, per the re-plan's report allocation. Written for build step 7;
 * round phases, sprites and the picture list for step 3.
 *
 * Rules on the tile (layout_table.md sections 3 and 5):
 * - A target (MOLE_ON) shows from t_show in the Pico's corrected clock. A
 *   mole rises out of its hole (160 ms), stays until t_show + up_ms and
 *   sinks back (160 ms). The start sprite (kind START, up_ms 0) stays until
 *   tapped or cleared.
 * - FEEDBACK hit: the target ends and the hit burst plays (250 ms); miss:
 *   the red X blinks over the tile (300 ms); both on the tile's own clock.
 * - GAME_CONTROL: ARM (the round is armed: holes, the tap threshold), START
 *   (the round plays), END (the round is over: the holes and the target go,
 *   the picture list stays), TAP_TEST_ON and OFF (holes, every hit lights).
 *   ARM and START clear the picture list and the target.
 * - WALL_PICTURE fills one entry of a list of 4; the entry showing is the
 *   latest started one that has not ended (dur_ms 0: until replaced). An
 *   entry with repeat starts the list again from entry 1 when it ends.
 *   Effect CLEAR empties the list.
 * - Every MOLE_ON, FEEDBACK, GAME_CONTROL and WALL_PICTURE addressed to this
 *   tile is ACKed, a repeat too. Order (layout_table.md section 10 item 13,
 *   one Pico seq counter for these): MOLE_ON and FEEDBACK act only when
 *   newer than every message accepted; GAME_CONTROL when newer than the last
 *   GAME_CONTROL (a lost and resent ARM still arms), and it clears only a
 *   target or picture entry older than itself; WALL_PICTURE when newer than
 *   its entry and than the last clearing GAME_CONTROL (entries may arrive in
 *   any order).
 */
#ifndef GAME_TILE_H
#define GAME_TILE_H

#include <stdbool.h>
#include <stdint.h>

#include "proto.h"

/** LEDs on one tile and wire bytes per tile (R, G, B, 3 per LED). */
#define GAME_TILE_LEDS 64u
#define GAME_TILE_WIRE_BYTES 192u

/** Entries of the picture list (WALL_PICTURE slot, 2 bits). */
#define GAME_TILE_PICS 4u

/** Game-mode GPIO4 pulse widths, microseconds (DS-13): tap detected, the
 *  hit burst's first frame, the mole's first frame. */
#define GAME_TILE_PULSE_TAP_US  2000u
#define GAME_TILE_PULSE_HIT_US  6000u
#define GAME_TILE_PULSE_MOLE_US 10000u

/** One entry of the picture list. Words only, for the seqlock. */
typedef struct
{
    uint32_t b_valid;  /**< 1 when the entry holds a picture. */
    uint32_t seq;      /**< Header seq it came with. */
    uint32_t effect;   /**< proto_pic_effect_t. */
    uint32_t colour;   /**< proto_colour_t. */
    uint32_t b_repeat; /**< 1 = start again from entry 1 after this one. */
    uint32_t dur_ms;   /**< 0 = until replaced. */
    int64_t  t_show;   /**< Show time, Pico corrected clock, us. */
    uint64_t mask;     /**< The tile's piece, before the rotation. */
} game_tile_pic_t;

/** The game state of one tile. Every field is a 32-bit or 64-bit integer,
 *  32-bit fields first and an even count of them, so the struct is a whole
 *  number of 32-bit words for the seqlock. */
typedef struct
{
    uint32_t b_game;        /**< 1 while a round plays (START, no END). */
    uint32_t b_round;       /**< 1 from ARM or START to END: holes shown. */
    uint32_t b_tap_test;    /**< 1 while the tap test is on (A11). */
    uint32_t b_mole;        /**< 1 when this tile has a target. */
    uint32_t kind;          /**< proto_target_kind_t of the target. */
    uint32_t target_seq;    /**< Header seq of the target. */
    uint32_t b_flash;       /**< 1 once a FEEDBACK was shown. */
    uint32_t flash_result;  /**< proto_feedback_t of the flash. */
    uint32_t b_squash;      /**< 1 when the hit was on an up mole. */
    uint32_t b_seq_known;   /**< 1 once a message seq was accepted. */
    uint32_t seq;           /**< Newest accepted message seq. */
    uint32_t b_ctl_known;   /**< 1 once a GAME_CONTROL was accepted. */
    uint32_t ctl_seq;       /**< Newest accepted GAME_CONTROL seq. */
    uint32_t b_clear_known; /**< 1 once ARM or START cleared the list. */
    uint32_t clear_seq;     /**< Seq of that ARM or START. */
    uint32_t threshold;     /**< ARM's tap threshold, 20 mg steps; 0 = the
                                 tile's default. */
    int64_t  show_us;       /**< Target show time, Pico corrected clock. */
    int64_t  hide_us;       /**< Target hide time, Pico corrected clock;
                                 INT64_MAX until tapped (start sprite). */
    int64_t  flash_start_us; /**< FEEDBACK shown from, tile local clock. */
    game_tile_pic_t pic[GAME_TILE_PICS]; /**< The picture list. */
} game_tile_state_t;

/**
 * @brief  Clear the game state (power-up, or the video takes the wall).
 * @param  p_state  State; must not be NULL.
 */
void game_tile_init (game_tile_state_t * p_state);

/**
 * @brief  Handle one datagram from the Pico on port 4210: MOLE_ON, FEEDBACK,
 *         GAME_CONTROL or WALL_PICTURE. Other types are not for this module.
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
 * @brief  Whether the game owns the tile's display: a round armed or
 *         playing, the tap test, a picture list, or a FEEDBACK still
 *         showing.
 * @param  p_state       State; must not be NULL.
 * @param  now_local_us  Tile local time, microseconds (the flash clock).
 * @return true when game_tile_fill_wire() should draw the tile.
 */
bool game_tile_active (game_tile_state_t const * p_state,
                       int64_t now_local_us);

/**
 * @brief  Fill the 64 LEDs of the tile for one moment, in the panel's order
 *         (R, G, B per LED, LED 0 first), channel values 0 to 8. Layers,
 *         bottom up: the hole (round or tap test; not under the start
 *         sprite), the target, the picture showing (it covers the tile),
 *         then the hit burst or the red X.
 * @param  p_state        State; must not be NULL.
 * @param  now_pico_us    Pico corrected time, microseconds (targets and
 *                        pictures).
 * @param  now_local_us   Tile local time, microseconds (the flash clock).
 * @param  orientation    Rotation to draw, 0 to 3: the measured orientation
 *                        with the mount offset (display_frame_mounted()).
 * @param  p_wire         Receives GAME_TILE_WIRE_BYTES bytes.
 */
void game_tile_fill_wire (game_tile_state_t const * p_state,
                          int64_t now_pico_us, int64_t now_local_us,
                          uint8_t orientation, uint8_t * p_wire);

/**
 * @brief  The game-mode GPIO4 pulse for a frame (DS-13): the mole's first
 *         frame (its show time fell after the previous frame) or the hit
 *         burst's first frame. The tap pulse is the Orientation Task's.
 * @param  p_state        State; must not be NULL.
 * @param  prev_pico_us   Pico time of the previous frame.
 * @param  now_pico_us    Pico time of this frame.
 * @param  prev_local_us  Local time of the previous frame.
 * @param  now_local_us   Local time of this frame.
 * @return Pulse width, microseconds (the longer when both), or 0 for none.
 */
uint32_t game_tile_pulse_us (game_tile_state_t const * p_state,
                             int64_t prev_pico_us, int64_t now_pico_us,
                             int64_t prev_local_us, int64_t now_local_us);

/**
 * @brief  The tap threshold ARM carried, in mg.
 * @param  p_state  State; must not be NULL.
 * @return Threshold, mg; 0 when the tile keeps its default.
 */
uint32_t game_tile_threshold_mg (game_tile_state_t const * p_state);

/**
 * @brief  Encode a TAP for the Pico (port 4211, acked).
 * @param  p_buf      Receives the bytes; at least PROTO_TAP_BYTES.
 * @param  session_id Session on the header.
 * @param  seq        Header seq (the tile's TAP counter).
 * @param  node_id    This tile, 1 to 9.
 * @param  peak       tap_detect peak, mg.
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
 * @brief  Read the published state (readers: the Display Engine and the
 *         Orientation Task). A read that overlaps a publish is retried.
 *         Before any publish every field is 0.
 * @param  p_state  Receives the state; must not be NULL.
 */
void game_tile_read (game_tile_state_t * p_state);

#endif /* GAME_TILE_H */

/*** end of file ***/
