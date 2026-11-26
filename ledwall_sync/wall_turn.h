/**
 * @file    wall_turn.h
 * @brief   Whole-wall rotation on the Pico (D-17, 24 Nov 2026; E3 to E5):
 *          each tile's orientation from its TELEMETRY, a 1.5 s window from
 *          the first change, and the decision whether the whole wall turned;
 *          plus the quarter turn of a layout and the search for the turn
 *          that maps one layout onto another. Pure C with no hardware call
 *          and no clock of its own: every function takes the time, so the
 *          host tests run it on a fake clock.
 *
 * Written for D-17.
 *
 * Rules (layout_table.md section 10 item 17):
 * - A tile's first orientation report after wall_turn_init() is its
 *   reference. Orientation codes rise with clockwise turns (layout_table.md
 *   section 5.7; bench 9 Nov), so a tile turned k quarter turns clockwise
 *   reports its reference plus k, modulo 4. Taking each tile's own
 *   reference keeps tiles with different mount offsets comparable.
 * - A change of any tile opens a window of WALL_TURN_WINDOW_MS; changes
 *   inside it only update the tiles' orientations. When it closes, the
 *   final orientations decide (a 180 degree turn that passes through 90
 *   counts once): when every active tile is known and has turned by the
 *   same k (1 to 3) from its reference, the wall turned by k and every
 *   known tile's reference moves on by k (an offline tile turned with the
 *   board too). Otherwise nothing changes for
 *   the wall (E4); a later window can still complete a slow turn.
 * - A quarter turn clockwise moves the cell at row r, column c of a rows x
 *   cols grid to row c, column rows - 1 - r, and the grid becomes cols x
 *   rows (E5). Empty cells turn with the grid.
 */
#ifndef WALL_TURN_H
#define WALL_TURN_H

#include <stdbool.h>
#include <stdint.h>

#include "proto.h"

/** Window from a tile's first change to the decision, milliseconds (E3,
 *  1.5 s; bench 9 Nov F-07: two tiles' reports at most 0.2 s apart). */
#define WALL_TURN_WINDOW_MS 1500u

/** Quarter turns in a full turn. */
#define WALL_TURN_QUARTERS 4u

/** A layout: the grid and each Node ID's cell (SESSION_CONFIG, DS-15). */
typedef struct
{
    uint8_t      rows;                 /**< 1 to 9. */
    uint8_t      cols;                 /**< 1 to 9. */
    proto_cell_t cell[PROTO_NODE_MAX]; /**< Node ID n at cell[n - 1]. */
} wall_turn_layout_t;

/** The detector. Zero it with wall_turn_init(). */
typedef struct
{
    bool     b_known[PROTO_NODE_MAX]; /**< A report gave the reference. */
    uint8_t  ref[PROTO_NODE_MAX];     /**< Reference orientation, 0 to 3. */
    uint8_t  now[PROTO_NODE_MAX];     /**< Latest orientation, 0 to 3. */
    bool     b_window;                /**< A window is open. */
    uint64_t window_end_us;           /**< When it closes. */
} wall_turn_t;

/**
 * @brief  Forget every reference (a new layout): the next report of each
 *         tile becomes its reference.
 * @param  p_turn  Detector; must not be NULL.
 */
void wall_turn_init (wall_turn_t * p_turn);

/**
 * @brief  A tile's orientation (TELEMETRY). The first report is the
 *         reference; a change opens the window unless one is open.
 * @param  p_turn       Detector; must not be NULL.
 * @param  node_id      1 to 9; others are ignored.
 * @param  orientation  0 to 3; others are ignored.
 * @param  now_us       Arrival time, microseconds.
 */
void wall_turn_report (wall_turn_t * p_turn, uint8_t node_id,
                       uint8_t orientation, uint64_t now_us);

/**
 * @brief  Close a window that is due and decide.
 * @param  p_turn       Detector; must not be NULL.
 * @param  active_mask  Bit n - 1 for each Node ID that must have turned
 *                      (placed and online).
 * @param  now_us       Current time, microseconds.
 * @return The wall's turn, 1 to 3 quarter turns clockwise, or 0.
 */
uint8_t wall_turn_poll (wall_turn_t * p_turn, uint16_t active_mask,
                        uint64_t now_us);

/**
 * @brief  Turn a layout clockwise.
 * @param  p_in      Layout; must not be NULL.
 * @param  quarters  Quarter turns clockwise; taken modulo 4.
 * @param  p_out     Receives the turned layout; must not be NULL and may
 *                   not be p_in.
 */
void wall_turn_rotate (wall_turn_layout_t const * p_in, uint8_t quarters,
                       wall_turn_layout_t * p_out);

/**
 * @brief  Find the quarter turns that map one layout onto another.
 * @param  p_from     Earlier layout; must not be NULL.
 * @param  p_to       Later layout; must not be NULL.
 * @param  preferred  Tried first (a symmetric layout fits several turns).
 * @param  p_quarters Receives the turn, 0 to 3, when one fits.
 * @return true when some turn of p_from equals p_to.
 */
bool wall_turn_match (wall_turn_layout_t const * p_from,
                      wall_turn_layout_t const * p_to, uint8_t preferred,
                      uint8_t * p_quarters);

#endif /* WALL_TURN_H */

/*** end of file ***/
