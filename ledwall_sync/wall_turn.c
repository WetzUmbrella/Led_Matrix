/**
 * @file    wall_turn.c
 * @brief   Whole-wall rotation on the Pico (see wall_turn.h for the rules).
 *
 * Written for D-17.
 */
#include "wall_turn.h"

#include <string.h>

/** Microseconds per millisecond, and the window in microseconds. */
#define WT_US_PER_MS     UINT64_C(1000)
#define WT_WINDOW_US     (WT_US_PER_MS * WALL_TURN_WINDOW_MS)
#define WT_QUARTERS_MASK (WALL_TURN_QUARTERS - 1u)

static uint8_t wt_turned (wall_turn_t const * p_turn, uint16_t active_mask);
static void    wt_rotate_once (wall_turn_layout_t const * p_in,
                               wall_turn_layout_t *       p_out);
static bool    wt_same (wall_turn_layout_t const * p_a,
                        wall_turn_layout_t const * p_b);

/**
 * @brief  Forget every reference (see wall_turn.h).
 * @param  p_turn  Detector.
 */
void wall_turn_init (wall_turn_t * p_turn)
{
    (void)memset(p_turn, 0, sizeof(*p_turn)); /* Returns the detector. */
}

/**
 * @brief  A tile's orientation (see wall_turn.h).
 * @param  p_turn       Detector.
 * @param  node_id      1 to 9.
 * @param  orientation  0 to 3.
 * @param  now_us       Arrival time, microseconds.
 */
void wall_turn_report (wall_turn_t * p_turn, uint8_t node_id,
                       uint8_t orientation, uint64_t now_us)
{
    if ((node_id >= 1u) && (node_id <= PROTO_NODE_MAX) &&
        (orientation < WALL_TURN_QUARTERS))
    {
        uint32_t const index = node_id - 1u;

        if (!p_turn->b_known[index])
        {
            p_turn->b_known[index] = true;
            p_turn->ref[index]     = orientation;
            p_turn->now[index]     = orientation;
        }
        else if (orientation != p_turn->now[index])
        {
            p_turn->now[index] = orientation;

            if (!p_turn->b_window)
            {
                p_turn->b_window      = true;
                p_turn->window_end_us = now_us + WT_WINDOW_US;
            }
        }
        else
        {
            /* The same orientation again: nothing changes. */
        }
    }
}

/**
 * @brief  Close a window that is due and decide (see wall_turn.h).
 * @param  p_turn       Detector.
 * @param  active_mask  Node IDs that must have turned.
 * @param  now_us       Current time, microseconds.
 * @return 1 to 3, or 0.
 */
uint8_t wall_turn_poll (wall_turn_t * p_turn, uint16_t active_mask,
                        uint64_t now_us)
{
    uint8_t turn = 0u;

    if ((p_turn->b_window) && (now_us >= p_turn->window_end_us))
    {
        p_turn->b_window = false;
        turn             = wt_turned(p_turn, active_mask);

        /* Every tile on the board turned with it, also one that is offline
         * now: its reference moves on too, so when it is heard again its
         * orientation agrees with the rest (bench 24 Nov 2026). */
        for (uint32_t index = 0u; (0u != turn) && (index < PROTO_NODE_MAX);
             index++)
        {
            if (p_turn->b_known[index])
            {
                /* Both are 0 to 3, so the masked sum fits uint8_t. */
                p_turn->ref[index] =
                    (uint8_t)((p_turn->ref[index] + turn) & WT_QUARTERS_MASK);
            }
        }
    }

    return turn;
}

/**
 * @brief  Turn a layout clockwise (see wall_turn.h).
 * @param  p_in      Layout.
 * @param  quarters  Quarter turns, modulo 4.
 * @param  p_out     Turned layout.
 */
void wall_turn_rotate (wall_turn_layout_t const * p_in, uint8_t quarters,
                       wall_turn_layout_t * p_out)
{
    wall_turn_layout_t step = *p_in;

    for (uint32_t count = 0u; count < (quarters & WT_QUARTERS_MASK); count++)
    {
        wall_turn_layout_t turned;

        wt_rotate_once(&step, &turned);
        step = turned;
    }

    *p_out = step;
}

/**
 * @brief  Find the turn from one layout to another (see wall_turn.h).
 * @param  p_from      Earlier layout.
 * @param  p_to        Later layout.
 * @param  preferred   Tried first.
 * @param  p_quarters  Receives the turn.
 * @return true when a turn fits.
 */
bool wall_turn_match (wall_turn_layout_t const * p_from,
                      wall_turn_layout_t const * p_to, uint8_t preferred,
                      uint8_t * p_quarters)
{
    bool b_found = false;

    for (uint32_t step = 0u; (!b_found) && (step < WALL_TURN_QUARTERS); step++)
    {
        /* Masked to 0 to 3, so it fits uint8_t. */
        uint8_t const quarters =
            (uint8_t)((preferred + step) & WT_QUARTERS_MASK);
        wall_turn_layout_t turned;

        wall_turn_rotate(p_from, quarters, &turned);

        if (wt_same(&turned, p_to))
        {
            b_found     = true;
            *p_quarters = quarters;
        }
    }

    return b_found;
}

/**
 * @brief  The turn every active tile shares, if any.
 * @param  p_turn       Detector.
 * @param  active_mask  Node IDs that must have turned.
 * @return 1 to 3 when every active tile is known and turned by it, else 0.
 */
static uint8_t wt_turned (wall_turn_t const * p_turn, uint16_t active_mask)
{
    uint8_t shared  = 0u;
    bool    b_first = true;
    bool    b_same  = (0u != active_mask);

    for (uint32_t index = 0u; (b_same) && (index < PROTO_NODE_MAX); index++)
    {
        if (0u != (active_mask & (UINT16_C(1) << index)))
        {
            /* Both are 0 to 3: adding 4 keeps the difference positive, and
             * the masked result fits uint8_t. */
            uint8_t const delta =
                (uint8_t)((p_turn->now[index] + WALL_TURN_QUARTERS -
                           p_turn->ref[index]) &
                          WT_QUARTERS_MASK);

            if (!p_turn->b_known[index])
            {
                b_same = false;
            }
            else if (b_first)
            {
                b_first = false;
                shared  = delta;
            }
            else if (delta != shared)
            {
                b_same = false;
            }
            else
            {
                /* The same turn as the tiles before. */
            }
        }
    }

    return b_same ? shared : 0u;
}

/**
 * @brief  One quarter turn clockwise: (r, c) goes to (c, rows - 1 - r).
 * @param  p_in   Layout.
 * @param  p_out  Turned layout.
 */
static void wt_rotate_once (wall_turn_layout_t const * p_in,
                            wall_turn_layout_t *       p_out)
{
    p_out->rows = p_in->cols;
    p_out->cols = p_in->rows;

    for (uint32_t index = 0u; index < PROTO_NODE_MAX; index++)
    {
        proto_cell_t const * const p_cell = &p_in->cell[index];

        p_out->cell[index].b_placed = p_cell->b_placed;
        p_out->cell[index].row      = 0u;
        p_out->cell[index].col      = 0u;

        if ((0u != p_cell->b_placed) && (p_cell->row < p_in->rows))
        {
            p_out->cell[index].row = p_cell->col;
            /* row < rows <= 9, so the difference is 0 to 8. */
            p_out->cell[index].col = (uint8_t)((p_in->rows - 1u) - p_cell->row);
        }
    }
}

/**
 * @brief  Whether two layouts are the same: grid, and each placed tile's
 *         cell (an unplaced tile's cell does not count).
 * @param  p_a  Layout.
 * @param  p_b  Layout.
 * @return true when equal.
 */
static bool wt_same (wall_turn_layout_t const * p_a,
                     wall_turn_layout_t const * p_b)
{
    bool b_same = (p_a->rows == p_b->rows) && (p_a->cols == p_b->cols);

    for (uint32_t index = 0u; (b_same) && (index < PROTO_NODE_MAX); index++)
    {
        proto_cell_t const * const p_x = &p_a->cell[index];
        proto_cell_t const * const p_y = &p_b->cell[index];

        if (p_x->b_placed != p_y->b_placed)
        {
            b_same = false;
        }
        else if ((0u != p_x->b_placed) &&
                 ((p_x->row != p_y->row) || (p_x->col != p_y->col)))
        {
            b_same = false;
        }
        else
        {
            /* This tile matches. */
        }
    }

    return b_same;
}

/*** end of file ***/
