/**
 * @file    test_wall_turn.c
 * @brief   Host tests of wall_turn (C35 layout turns, C36 the detector) on a
 *          fake clock.
 *
 * Written for D-17, 24 Nov 2026.
 */
#include "test_wall_turn.h"

#include <stdint.h>
#include <string.h>

#include "test_main.h"
#include "wall_turn.h"

/** Start time, one millisecond, and the window. */
#define TST_T0     UINT64_C(1000000)
#define TST_MS     UINT64_C(1000)
#define TST_WINDOW (WALL_TURN_WINDOW_MS * TST_MS)

/** Tiles 1 and 2, and tiles 1 to 3. */
#define TST_TWO   UINT16_C(0x0003)
#define TST_THREE UINT16_C(0x0007)

static void tst_place (wall_turn_layout_t * p_layout, uint8_t node_id,
                       uint8_t row, uint8_t col);
static bool tst_cell (wall_turn_layout_t const * p_layout, uint8_t node_id,
                      uint8_t row, uint8_t col);
static bool tst_full_turns (void);
static bool tst_offline_turn (wall_turn_t * p_turn, uint64_t * p_now);
static void tst_report_all (wall_turn_t * p_turn, uint16_t mask,
                            uint8_t orientation, uint64_t now_us);

/**
 * @brief  C35: layout turns and the match (see test_wall_turn.h).
 * @return true when every check passed.
 */
bool test_wall_turn_layout (void)
{
    wall_turn_layout_t wide;
    wall_turn_layout_t turned;
    wall_turn_layout_t line;
    uint8_t            quarters = 0u;
    bool               b_ok     = true;

    /* 2x3: tile 1 top left, tile 2 top right, tile 3 bottom middle. */
    (void)memset(&wide, 0, sizeof(wide)); /* Returns it. */
    wide.rows = 2u;
    wide.cols = 3u;
    tst_place(&wide, 1u, 0u, 0u);
    tst_place(&wide, 2u, 0u, 2u);
    tst_place(&wide, 3u, 1u, 1u);

    /* A quarter turn clockwise: 3x2, top left to top right, top right to
     * bottom right, bottom middle to middle left. */
    wall_turn_rotate(&wide, 1u, &turned);
    b_ok = (test_main_check((3u == turned.rows) && (2u == turned.cols),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check((tst_cell(&turned, 1u, 0u, 1u)) &&
                                (tst_cell(&turned, 2u, 2u, 1u)) &&
                                (tst_cell(&turned, 3u, 1u, 0u)) &&
                                (0u == turned.cell[3].b_placed),
                            __LINE__)) &&
           b_ok;

    /* A half turn: 2x3 again, every cell mirrored through the centre. */
    wall_turn_rotate(&wide, 2u, &turned);
    b_ok = (test_main_check((2u == turned.rows) && (3u == turned.cols) &&
                                (tst_cell(&turned, 1u, 1u, 2u)) &&
                                (tst_cell(&turned, 2u, 1u, 0u)) &&
                                (tst_cell(&turned, 3u, 0u, 1u)),
                            __LINE__)) &&
           b_ok;

    /* 1x9 becomes 9x1 (E5). */
    (void)memset(&line, 0, sizeof(line)); /* Returns it. */
    line.rows = 1u;
    line.cols = 9u;
    tst_place(&line, 4u, 0u, 8u);
    wall_turn_rotate(&line, 1u, &turned);
    b_ok = (test_main_check((9u == turned.rows) && (1u == turned.cols) &&
                                (tst_cell(&turned, 4u, 8u, 0u)),
                            __LINE__)) &&
           b_ok;

    b_ok = (test_main_check(tst_full_turns(), __LINE__)) && b_ok;

    /* The match: the turn from wide to its anticlockwise turn is 3. */
    wall_turn_rotate(&wide, 3u, &turned);
    b_ok = (test_main_check((wall_turn_match(&wide, &turned, 0u, &quarters)) &&
                                (3u == quarters),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check((wall_turn_match(&wide, &wide, 0u, &quarters)) &&
                                (0u == quarters),
                            __LINE__)) &&
           b_ok;

    /* A single tile in the middle of a 3x3 fits every turn: the one asked
     * for wins. */
    (void)memset(&line, 0, sizeof(line)); /* Returns it. */
    line.rows = 3u;
    line.cols = 3u;
    tst_place(&line, 5u, 1u, 1u);
    b_ok = (test_main_check((wall_turn_match(&line, &line, 2u, &quarters)) &&
                                (2u == quarters),
                            __LINE__)) &&
           b_ok;

    /* A tile moved by hand is not a turn. */
    turned = wide;
    tst_place(&turned, 3u, 1u, 2u);
    b_ok = (test_main_check(!wall_turn_match(&wide, &turned, 0u, &quarters),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  C36: the detector (see test_wall_turn.h).
 * @return true when every check passed.
 */
bool test_wall_turn_detect (void)
{
    static wall_turn_t turn;
    uint64_t           now  = TST_T0;
    bool               b_ok = true;

    /* References: tiles 1 and 2 at 0, tile 3 mounted at 1 (an offset). */
    wall_turn_init(&turn);
    tst_report_all(&turn, TST_TWO, 0u, now);
    wall_turn_report(&turn, 3u, 1u, now);
    b_ok = (test_main_check(0u == wall_turn_poll(&turn, TST_THREE, now),
                            __LINE__)) &&
           b_ok;

    /* The whole wall a quarter turn clockwise; the tiles report 0.2 s
     * apart. Nothing before the window closes, then a turn of 1. */
    now += 100u * TST_MS;
    tst_report_all(&turn, TST_TWO, 1u, now);
    wall_turn_report(&turn, 3u, 2u, now + (200u * TST_MS));
    b_ok = (test_main_check(
               0u == wall_turn_poll(&turn, TST_THREE, now + TST_WINDOW - 1u),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               1u == wall_turn_poll(&turn, TST_THREE, now + TST_WINDOW),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               0u == wall_turn_poll(&turn, TST_THREE, now + (2u * TST_WINDOW)),
               __LINE__)) &&
           b_ok;

    /* Turning back is the opposite turn: 3 quarter turns clockwise. */
    now += 5000u * TST_MS;
    tst_report_all(&turn, TST_TWO, 0u, now);
    wall_turn_report(&turn, 3u, 1u, now);
    b_ok = (test_main_check(
               3u == wall_turn_poll(&turn, TST_THREE, now + TST_WINDOW),
               __LINE__)) &&
           b_ok;

    /* A half turn through 90: the final orientation decides, once. */
    now += 5000u * TST_MS;
    tst_report_all(&turn, TST_TWO, 1u, now);
    tst_report_all(&turn, TST_TWO, 2u, now + (300u * TST_MS));
    wall_turn_report(&turn, 3u, 3u, now + (400u * TST_MS));
    b_ok = (test_main_check(
               2u == wall_turn_poll(&turn, TST_THREE, now + TST_WINDOW),
               __LINE__)) &&
           b_ok;

    /* Partial: only tile 1 turns. Then mixed: tile 2 by 2, tile 3 by 1. */
    now += 5000u * TST_MS;
    wall_turn_report(&turn, 1u, 3u, now);
    b_ok = (test_main_check(
               0u == wall_turn_poll(&turn, TST_THREE, now + TST_WINDOW),
               __LINE__)) &&
           b_ok;
    now += 5000u * TST_MS;
    wall_turn_report(&turn, 2u, 0u, now);
    wall_turn_report(&turn, 3u, 0u, now);
    b_ok = (test_main_check(
               0u == wall_turn_poll(&turn, TST_THREE, now + TST_WINDOW),
               __LINE__)) &&
           b_ok;

    /* A slow turn: tiles 1 and 3 are already 1 past their references (2
     * and 3); tile 2 catches up later, and that window completes the turn. */
    now += 5000u * TST_MS;
    wall_turn_report(&turn, 2u, 3u, now);
    b_ok = (test_main_check(
               1u == wall_turn_poll(&turn, TST_THREE, now + TST_WINDOW),
               __LINE__)) &&
           b_ok;

    b_ok = (test_main_check(tst_offline_turn(&turn, &now), __LINE__)) && b_ok;

    /* A tile never heard blocks the turn. */
    wall_turn_init(&turn);
    tst_report_all(&turn, TST_TWO, 0u, now);
    tst_report_all(&turn, TST_TWO, 1u, now + TST_MS);
    b_ok = (test_main_check(0u == wall_turn_poll(&turn, TST_THREE,
                                                 now + TST_WINDOW + TST_MS),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  An offline tile during a wall turn (bench 24 Nov 2026; see the
 *         comment inside).
 * @param  p_turn  Detector.
 * @param  p_now   Time, advanced.
 * @return true when every check passed.
 */
static bool tst_offline_turn (wall_turn_t * p_turn, uint64_t * p_now)
{
    bool b_ok = true;

    /* Bench 24 Nov 2026: tile 3 is offline (not active) while tiles 1 and
     * 2 turn the wall by 1; its reference moves on too, so when it is heard
     * again at its turned orientation, and the wall is turned back, all
     * three agree (a turn of 3). */
    *p_now += 5000u * TST_MS;
    wall_turn_init(p_turn);
    tst_report_all(p_turn, TST_THREE, 0u, *p_now);
    tst_report_all(p_turn, TST_TWO, 1u, *p_now + TST_MS);
    b_ok = (test_main_check(1u == wall_turn_poll(p_turn, TST_TWO,
                                                 *p_now + TST_WINDOW + TST_MS),
                            __LINE__)) &&
           b_ok;
    *p_now += 5000u * TST_MS;
    wall_turn_report(p_turn, 3u, 1u, *p_now); /* Back online, turned. */
    b_ok = (test_main_check(
               0u == wall_turn_poll(p_turn, TST_THREE, *p_now + TST_WINDOW),
               __LINE__)) &&
           b_ok;
    *p_now += 5000u * TST_MS;
    tst_report_all(p_turn, TST_THREE, 0u, *p_now);
    b_ok = (test_main_check(
               3u == wall_turn_poll(p_turn, TST_THREE, *p_now + TST_WINDOW),
               __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Place a tile.
 * @param  p_layout  Layout.
 * @param  node_id   1 to 9.
 * @param  row       Row.
 * @param  col       Column.
 */
static void tst_place (wall_turn_layout_t * p_layout, uint8_t node_id,
                       uint8_t row, uint8_t col)
{
    p_layout->cell[node_id - 1u].b_placed = 1u;
    p_layout->cell[node_id - 1u].row      = row;
    p_layout->cell[node_id - 1u].col      = col;
}

/**
 * @brief  Whether a tile is placed at a cell.
 * @param  p_layout  Layout.
 * @param  node_id   1 to 9.
 * @param  row       Row.
 * @param  col       Column.
 * @return true when it is.
 */
static bool tst_cell (wall_turn_layout_t const * p_layout, uint8_t node_id,
                      uint8_t row, uint8_t col)
{
    proto_cell_t const * const p_cell = &p_layout->cell[node_id - 1u];

    return (1u == p_cell->b_placed) && (row == p_cell->row) &&
           (col == p_cell->col);
}

/**
 * @brief  Every grid from 1x1 to 9x9 with up to 9 tiles on a diagonal-ish
 *         spread: four quarter turns give it back, one turn swaps the shape
 *         and keeps every tile inside the grid.
 * @return true when all hold.
 */
static bool tst_full_turns (void)
{
    bool b_ok = true;

    for (uint8_t rows = 1u; rows <= 9u; rows++)
    {
        for (uint8_t cols = 1u; cols <= 9u; cols++)
        {
            wall_turn_layout_t layout;
            wall_turn_layout_t once;
            wall_turn_layout_t four;
            uint8_t            quarters = 0u;

            (void)memset(&layout, 0, sizeof(layout)); /* Returns it. */
            layout.rows = rows;
            layout.cols = cols;

            for (uint32_t index = 0u; index < PROTO_NODE_MAX; index++)
            {
                /* Distinct cells: slot (index x 5) of a grid of at least
                 * 9 cells, or the first slots of a smaller one. */
                uint32_t const cells = (uint32_t)rows * cols;
                uint32_t const slot =
                    (cells >= PROTO_NODE_MAX) ? ((index * 5u) % cells) : index;

                if (slot < cells)
                {
                    /* Both are under 9, so they fit uint8_t. */
                    tst_place(&layout, (uint8_t)(index + 1u),
                              (uint8_t)(slot / cols), (uint8_t)(slot % cols));
                }
            }

            wall_turn_rotate(&layout, 1u, &once);
            wall_turn_rotate(&layout, 4u, &four);

            for (uint32_t index = 0u; index < PROTO_NODE_MAX; index++)
            {
                b_ok = b_ok && ((0u == once.cell[index].b_placed) ||
                                ((once.cell[index].row < once.rows) &&
                                 (once.cell[index].col < once.cols)));
            }

            b_ok = b_ok && (once.rows == cols) && (once.cols == rows) &&
                   (wall_turn_match(&layout, &four, 0u, &quarters)) &&
                   (0u == quarters);
        }
    }

    return b_ok;
}

/**
 * @brief  The same orientation from several tiles at once.
 * @param  p_turn       Detector.
 * @param  mask         Node IDs, bit n - 1.
 * @param  orientation  0 to 3.
 * @param  now_us       Time.
 */
static void tst_report_all (wall_turn_t * p_turn, uint16_t mask,
                            uint8_t orientation, uint64_t now_us)
{
    for (uint8_t node_id = 1u; node_id <= PROTO_NODE_MAX; node_id++)
    {
        if (0u != (mask & (UINT16_C(1) << (node_id - 1u))))
        {
            wall_turn_report(p_turn, node_id, orientation, now_us);
        }
    }
}

/*** end of file ***/
