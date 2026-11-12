/**
 * @file    test_coord.c
 * @brief   Host tests of coordinator_loop (T-B2-02 to T-B2-05). The test
 *          owns a fake clock and records every datagram the coordinator
 *          sends; simulated tiles and a simulated PC feed it datagrams
 *          encoded with common/proto.
 *
 * Owner: Benjamin (Buddy 2). Implemented by Ahsanul (B4) for Benjamin.
 * Each test cites the plan acceptance it covers. Trial counts copy the plan
 * row (10 of 10 for the heartbeat loss, 100 injected stale messages); time
 * steps of 50 us are a plan choice (fine enough that every scheduled send
 * lands on its exact time).
 */
#include "test_coord.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "coordinator_loop.h"
#include "coordinator_port.h"
#include "proto.h"
#include "test_main.h"

/** Fake clock start: 1 s after boot (plan choice). */
#define TST_T0_US UINT64_C(1000000)

/** Pass step of the fake clock. */
#define TST_STEP_US UINT64_C(50)

/** Addresses: broadcast, PC and tile n (any distinct values). */
#define TST_BCAST   UINT32_C(0xFF01A8C0)
#define TST_PC_ADDR UINT32_C(0x6401A8C0)
#define TST_PC_PORT 4213u

/** Heartbeat period of the simulated tiles, 0.5 s (VA-2.7). */
#define TST_HEARTBEAT_US UINT64_C(500000)

/** Session used by most tests, its frame_count and CRC32 values. */
#define TST_SESSION 7u
#define TST_FRAMES  600u
#define TST_CRC_1   UINT32_C(0x11111111)
#define TST_CRC_2   UINT32_C(0x22222222)

/** Positions of the row-major maps the tests are written with (step 4d
 *  turns each into cells). */
#define TST_MAP_SLOTS 9u

/** A 2x2 wall: map positions 0 to 3, row-major (re-plan step 3). */
#define TST_ROWS_2X2 2u
#define TST_COLS_2X2 2u

static uint16_t g_tile_seq[CL_MAX_NODES + 1u];
static uint64_t g_last_beat[CL_MAX_NODES + 1u];
static uint16_t g_live      = 0u; /* Tiles that send heartbeats. */
static uint16_t g_pc_seq    = 0u;
static uint8_t  g_beat_sess = 0u;

static void tst_reset (void);
static void tst_advance (uint64_t duration_us);
static void tst_rx_tile (uint8_t node_id, uint8_t const * p_buf, uint32_t len);
static void tst_register (uint8_t node_id, uint8_t session, uint32_t crc);
static void tst_heartbeat (uint8_t node_id);
static void tst_asset_ready (uint8_t node_id, uint8_t session, uint32_t crc);
static void tst_sync_ready (uint8_t node_id, uint8_t session);
static void tst_session (uint8_t session, uint8_t rows, uint8_t cols,
                         proto_policy_t policy, uint8_t const * p_map);
static void tst_pc_command (proto_type_t type, uint8_t session);
static bool tst_to_ready (proto_policy_t policy);
static bool tst_to_playing (proto_policy_t policy);
static uint32_t tst_find (proto_type_t type, uint32_t from, uint32_t * p_index);
static uint32_t tst_addr (uint8_t node_id);
static uint16_t tst_seq_of (uint32_t index);
static uint64_t tst_deadline (uint64_t origin, uint64_t tick);

/**
 * @brief  C1 (T-B2-02): every beacon on origin + k x 100 ms, t_p equal to
 *         the send time, lateness bin 0, seq k, for 10 s.
 * @return true when every check passed.
 */
bool test_coord_beacon_schedule (void)
{
    bool     b_ok  = true;
    uint32_t index = 0u;
    uint32_t count = 0u;

    tst_reset();
    tst_advance(10u * UINT64_C(1000000));

    while (0u != tst_find(PROTO_TYPE_BEACON, index, &index))
    {
        proto_beacon_t msg;

        b_ok = (test_main_check(
                   proto_decode_beacon(coordinator_port_get(index)->bytes,
                                       coordinator_port_get(index)->len, &msg),
                   __LINE__)) &&
               b_ok;
        b_ok =
            (test_main_check((msg.t_p == coordinator_port_get(index)->t_us) &&
                                 (msg.t_p == (TST_T0_US + (count * 100000u))) &&
                                 (msg.header.seq == count),
                             __LINE__)) &&
            b_ok;
        count++;
        index++;
    }

    b_ok = (test_main_check((100u == count) &&
                                (count == coordinator_loop_lateness_bin(0u)),
                            __LINE__)) &&
           b_ok;
    printf("    %" PRIu32 " beacons in 10 s, all on the 100 ms grid, "
           "lateness bin 0: %" PRIu32 "\n",
           count, coordinator_loop_lateness_bin(0u));

    return b_ok;
}

/**
 * @brief  C2 (T-B2-02, plan choice on stalls): a 350 ms stall skips the 3
 *         slots that passed entirely, sends one beacon at once, then stays
 *         on the 100 ms grid; seq has no gap.
 * @return true when every check passed.
 */
bool test_coord_beacon_stall (void)
{
    bool     b_ok  = true;
    uint32_t index = 0u;
    uint32_t count = 0u;
    uint64_t prev  = 0u;

    tst_reset();
    tst_advance(UINT64_C(1000000)); /* Beacons at 1.0 to 2.0 s. */
    coordinator_port_set_now(coordinator_port_now_us() +
                             UINT64_C(350000)); /* Stall: no pass for 350 ms. */
    tst_advance(UINT64_C(1000000));

    while (0u != tst_find(PROTO_TYPE_BEACON, index, &index))
    {
        b_ok = (test_main_check(tst_seq_of(index) == count, __LINE__)) && b_ok;

        /* Beacons sit on the 100 ms grid, except the one sent at once
         * after the stall: 2.0 s + 350 ms = 2.35 s (slots 2.0 to 2.2 s
         * skipped, slot 2.3 s sent late); the next is on the grid at
         * 2.4 s. */
        bool const b_grid =
            (0u == ((coordinator_port_get(index)->t_us - TST_T0_US) % 100000u));

        b_ok = (test_main_check((b_grid) || (UINT64_C(2350000) ==
                                             coordinator_port_get(index)->t_us),
                                __LINE__)) &&
               b_ok;
        (void)prev; /* Kept for the printout below. */

        prev = coordinator_port_get(index)->t_us;
        count++;
        index++;
    }

    b_ok = (test_main_check(3u == coordinator_loop_test_skipped(), __LINE__)) &&
           b_ok;
    printf("    stall 350 ms: %" PRIu32 " slots skipped, %" PRIu32
           " beacons, seq contiguous\n",
           coordinator_loop_test_skipped(), count);

    return b_ok;
}

/**
 * @brief  C3 (T-B2-03 acceptance 3): 9 simulated Node IDs register: 9 of 9
 *         in the registry.
 * @return true when every check passed.
 */
bool test_coord_nine_nodes (void)
{
    tst_reset();

    for (uint8_t node_id = 1u; node_id <= CL_MAX_NODES; node_id++)
    {
        tst_register(node_id, 0u, 0u);
    }

    tst_advance(UINT64_C(200000));

    uint16_t const online = coordinator_loop_test_online();

    printf("    online bitmap 0x%03" PRIx16 "\n", online);

    return test_main_check(UINT16_C(0x01FF) == online, __LINE__);
}

/**
 * @brief  C4 (T-B2-03 acceptance 1): heartbeats of tile 2 stop; the loss is
 *         logged 2.5 to 2.6 s after its last heartbeat (worst case 2.6 s,
 *         VA-2.7) in 10 of 10 trials, and tile 1 stays online.
 * @return true when every check passed.
 */
bool test_coord_heartbeat_loss (void)
{
    bool     b_ok   = true;
    uint32_t passes = 0u;

    for (uint32_t trial = 0u; trial < 10u; trial++)
    {
        tst_reset();
        /* Different phase against the 100 ms scan in every trial: the
         * tiles register 0, 37, 74 ... ms after a scan. */
        tst_advance(trial * UINT64_C(37050));
        tst_register(1u, 0u, 0u);
        tst_register(2u, 0u, 0u);
        g_live = UINT16_C(0x0003);
        tst_advance(UINT64_C(2000000));
        g_live = UINT16_C(0x0001);
        tst_advance(UINT64_C(3000000));

        uint64_t const detected = coordinator_loop_test_loss_time(2u);
        uint64_t const silence  = detected - g_last_beat[2u];
        bool const     b_trial =
            (silence > UINT64_C(2500000)) && (silence <= UINT64_C(2600000)) &&
            (UINT16_C(0x0001) == coordinator_loop_test_online());

        if (b_trial)
        {
            passes++;
        }

        printf("    trial %" PRIu32 ": loss logged %" PRIu32
               " us after the last heartbeat\n",
               trial, (uint32_t)silence); /* Below 2^32 us here. */
    }

    b_ok = (test_main_check(10u == passes, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  C5 (T-B2-03, OD-08): a message that arrives 0.5 ms before a beacon
 *         waits until the beacon has gone.
 * @return true when every check passed.
 */
bool test_coord_dispatch_guard (void)
{
    bool b_ok = true;

    tst_reset();
    tst_advance(UINT64_C(99500)); /* Next beacon at T0 + 100 ms. */
    tst_register(5u, 0u, 0u);
    coordinator_loop_test_pass(); /* Same time: 0.5 ms left, must wait. */
    b_ok = (test_main_check(0u == coordinator_loop_test_online(), __LINE__)) &&
           b_ok;
    tst_advance(UINT64_C(1000)); /* Beacon, then the dispatch. */
    b_ok = (test_main_check(UINT16_C(0x0010) == coordinator_loop_test_online(),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  C6 (T-B2-04 acceptance 1): 100 injected messages with a wrong
 *         session_id or an old seq: 0 accepted.
 * @return true when every check passed.
 */
bool test_coord_stale_rejected (void)
{
    bool          b_ok   = true;
    uint8_t const map[9] = {1u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

    tst_reset();
    tst_session(TST_SESSION, TST_ROWS_2X2, TST_COLS_2X2, PROTO_POLICY_CONTINUE,
                map);
    tst_register(1u, 0u, 0u);
    tst_register(2u, 0u, 0u);
    tst_advance(UINT64_C(200000));

    uint32_t const stale_before = coordinator_loop_test_stale();
    uint16_t const ready_before = coordinator_loop_test_readiness();
    uint16_t const old_seq      = g_tile_seq[1u];

    for (uint32_t idx = 0u; idx < 50u; idx++)
    {
        /* Wrong session: ASSET_READY for session 99 with the right CRC. */
        tst_asset_ready(1u, 99u, TST_CRC_1);
        tst_advance(UINT64_C(2000)); /* Paced: the ring holds 32. */
    }

    for (uint32_t idx = 0u; idx < 50u; idx++)
    {
        uint8_t                   buf[PROTO_ASSET_READY_BYTES];
        proto_asset_ready_t const msg = {
            {PROTO_TYPE_ASSET_READY, TST_SESSION,
             (uint16_t)(old_seq - 1u - idx)}, /* Not newer: 16-bit wrap. */
            1u,
            TST_CRC_1};

        tst_rx_tile(1u, buf, proto_encode_asset_ready(&msg, buf, sizeof(buf)));
        tst_advance(UINT64_C(2000));
    }

    tst_advance(UINT64_C(300000));

    uint32_t const dropped = coordinator_loop_test_stale() - stale_before;

    printf("    100 injected, %" PRIu32 " dropped, readiness 0x%03" PRIx16 "\n",
           dropped, coordinator_loop_test_readiness());
    b_ok = (test_main_check(
               (100u == dropped) &&
                   (ready_before == coordinator_loop_test_readiness()),
               __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  C7 (T-B2-04 acceptance 2): 2 tiles; Start refused until 2 of 2
 *         matching ASSET_READY and SYNC_READY.
 * @return true when every check passed.
 */
bool test_coord_start_gate (void)
{
    bool          b_ok   = true;
    uint32_t      index  = 0u;
    uint8_t const map[9] = {1u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

    tst_reset();
    tst_session(TST_SESSION, TST_ROWS_2X2, TST_COLS_2X2, PROTO_POLICY_CONTINUE,
                map);
    tst_register(1u, 0u, 0u);
    tst_register(2u, 0u, 0u);
    g_live      = UINT16_C(0x0003);
    g_beat_sess = TST_SESSION;
    tst_advance(UINT64_C(200000));

    proto_type_t const step_type[4] = {
        PROTO_TYPE_ASSET_READY, PROTO_TYPE_ASSET_READY, PROTO_TYPE_SYNC_READY,
        PROTO_TYPE_SYNC_READY};
    uint8_t const step_node[4] = {1u, 2u, 1u, 2u};

    for (uint32_t step = 0u; step < 4u; step++)
    {
        tst_pc_command(PROTO_TYPE_START_AT, TST_SESSION);
        tst_advance(UINT64_C(50000));
        b_ok = (test_main_check(
                   (0u == tst_find(PROTO_TYPE_START_AT, 0u, &index)) &&
                       (PROTO_COORD_READY != coordinator_loop_test_state()),
                   __LINE__)) &&
               b_ok;

        if (PROTO_TYPE_ASSET_READY == step_type[step])
        {
            tst_asset_ready(step_node[step], TST_SESSION,
                            (1u == step_node[step]) ? TST_CRC_1 : TST_CRC_2);
        }
        else
        {
            tst_sync_ready(step_node[step], TST_SESSION);
        }

        tst_advance(UINT64_C(50000));
    }

    b_ok = (test_main_check(PROTO_COORD_READY == coordinator_loop_test_state(),
                            __LINE__)) &&
           b_ok;
    tst_pc_command(PROTO_TYPE_START_AT, TST_SESSION);
    tst_advance(UINT64_C(50000));
    b_ok = (test_main_check((0u != tst_find(PROTO_TYPE_START_AT, 0u, &index)) &&
                                (PROTO_COORD_COORDINATING_PLAYBACK ==
                                 coordinator_loop_test_state()),
                            __LINE__)) &&
           b_ok;
    printf("    Start refused 4 times, accepted after 2 of 2 ASSET_READY "
           "and SYNC_READY\n");

    return b_ok;
}

/**
 * @brief  C8 (T-B2-04, W6D-7a): an ASSET_READY whose CRC32 differs from the
 *         manifest CRC32 of that Node ID is not counted.
 * @return true when every check passed.
 */
bool test_coord_crc_mismatch (void)
{
    uint8_t const map[9] = {1u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

    tst_reset();
    tst_session(TST_SESSION, TST_ROWS_2X2, TST_COLS_2X2, PROTO_POLICY_CONTINUE,
                map);
    tst_register(1u, 0u, 0u);
    tst_advance(UINT64_C(100000));
    tst_asset_ready(1u, TST_SESSION, TST_CRC_2); /* Node 2's CRC. */
    tst_advance(UINT64_C(100000));

    return test_main_check(0u == coordinator_loop_test_readiness(), __LINE__);
}

/**
 * @brief  C9 (T-B2-04, W6 statechart 2 VALIDATING SESSION): session_id 0,
 *         a Node ID outside the layout and a layout with no tile placed are
 *         rejected; a valid map with its tiles missing waits for nodes.
 *         Since step 4d a Node ID cannot appear twice (one cell per Node
 *         ID), and the codec refuses a cell outside the grid.
 * @return true when every check passed.
 */
bool test_coord_session_validation (void)
{
    bool          b_ok       = true;
    uint8_t const outside[9] = {1u, 0u, 0u, 0u, 2u, 0u, 0u, 0u, 0u};
    uint8_t const empty[9]   = {0u};
    uint8_t const good[9]    = {1u, 0u, 0u, 2u, 0u, 0u, 0u, 0u, 0u};

    tst_reset();
    tst_session(0u, TST_ROWS_2X2, TST_COLS_2X2, 0u, good);
    tst_session(3u, TST_ROWS_2X2, TST_COLS_2X2, 0u, outside);
    tst_session(4u, TST_ROWS_2X2, TST_COLS_2X2, 0u, empty);
    tst_advance(UINT64_C(100000));
    b_ok = (test_main_check(PROTO_COORD_WAITING_FOR_SESSION ==
                                coordinator_loop_test_state(),
                            __LINE__)) &&
           b_ok;
    tst_session(5u, TST_ROWS_2X2, TST_COLS_2X2, 0u, good);
    tst_advance(UINT64_C(100000));
    b_ok = (test_main_check(PROTO_COORD_WAITING_FOR_NODES ==
                                coordinator_loop_test_state(),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  C9b (whack-a-mole re-plan): a SESSION_CONFIG with a new
 *         session_id is refused while a game or playback runs. The state
 *         stays put, and the old session's readiness is kept.
 * @return true when every check passed.
 */
bool test_coord_session_while_busy (void)
{
    bool          b_ok   = true;
    uint8_t const map[9] = {1u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

    /* COORDINATING GAME has no entry path until build step 7: force it. */
    b_ok = (tst_to_ready(PROTO_POLICY_CONTINUE)) && b_ok;
    coordinator_loop_test_set_state(PROTO_COORD_COORDINATING_GAME);
    tst_session(TST_SESSION + 1u, TST_ROWS_2X2, TST_COLS_2X2, 0u, map);
    tst_advance(UINT64_C(100000));
    b_ok = (test_main_check(PROTO_COORD_COORDINATING_GAME ==
                                coordinator_loop_test_state(),
                            __LINE__)) &&
           b_ok;
    b_ok =
        (test_main_check(UINT16_C(0x0003) == coordinator_loop_test_readiness(),
                         __LINE__)) &&
        b_ok;

    b_ok = (tst_to_playing(PROTO_POLICY_CONTINUE)) && b_ok;
    tst_session(TST_SESSION + 1u, TST_ROWS_2X2, TST_COLS_2X2, 0u, map);
    tst_advance(UINT64_C(100000));
    b_ok = (test_main_check(PROTO_COORD_COORDINATING_PLAYBACK ==
                                coordinator_loop_test_state(),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  C10 (T-B2-04): STATUS every 250 ms, on an absolute schedule, to
 *         the address and port the PC sent from.
 * @return true when every check passed.
 */
bool test_coord_status (void)
{
    bool          b_ok   = true;
    uint32_t      index  = 0u;
    uint32_t      count  = 0u;
    uint64_t      prev   = 0u;
    uint8_t const map[9] = {1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

    tst_reset();
    tst_session(TST_SESSION, 1u, 1u, 0u, map);
    tst_advance(UINT64_C(2000000));

    while (0u != tst_find(PROTO_TYPE_STATUS, index, &index))
    {
        b_ok = (test_main_check(
                   (TST_PC_ADDR == coordinator_port_get(index)->addr) &&
                       (TST_PC_PORT == coordinator_port_get(index)->port),
                   __LINE__)) &&
               b_ok;

        /* Absolute schedule: each STATUS leaves within 1 ms of its slot
         * (a beacon due at the same time goes first, OD-08). */
        uint64_t const into_slot =
            (coordinator_port_get(index)->t_us - TST_T0_US) % UINT64_C(250000);

        b_ok = (test_main_check(into_slot < UINT64_C(1000), __LINE__)) && b_ok;
        (void)prev; /* Not needed with the slot check. */

        prev = coordinator_port_get(index)->t_us;
        count++;
        index++;
    }

    printf("    %" PRIu32
           " STATUS in 2 s, each within 1 ms of its 250 ms slot\n",
           count);
    b_ok = (test_main_check(count >= 7u, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  C11 (T-B2-05): 3 copies of START_AT with one seq; t_start exactly
 *         600 ms after copy 1; copies 2 and 3 at 100 and 200 ms +/- 5 ms.
 * @return true when every check passed.
 */
bool test_coord_start_copies (void)
{
    bool     b_ok     = true;
    uint32_t index    = 0u;
    uint32_t copies   = 0u;
    uint64_t first_us = 0u;

    b_ok = (test_main_check(tst_to_playing(PROTO_POLICY_CONTINUE), __LINE__)) &&
           b_ok;

    while (0u != tst_find(PROTO_TYPE_START_AT, index, &index))
    {
        proto_start_at_t msg;

        /* The result is not needed: the coordinator's own output. */
        (void)proto_decode_start_at(coordinator_port_get(index)->bytes,
                                    coordinator_port_get(index)->len,
                                    &msg); /* Coordinator output. */

        if (0u == copies)
        {
            first_us = coordinator_port_get(index)->t_us;
        }

        uint64_t const gap = coordinator_port_get(index)->t_us - first_us;

        b_ok = (test_main_check(
                   (msg.t_start == (first_us + UINT64_C(600000))) &&
                       (TST_BCAST == coordinator_port_get(index)->addr) &&
                       (gap >= ((copies * UINT64_C(100000)) - 0u)) &&
                       (gap <= ((copies * UINT64_C(100000)) + 5000u)),
                   __LINE__)) &&
               b_ok;
        copies++;
        index++;
    }

    printf("    %" PRIu32 " copies, t_start = copy 1 + 600 ms\n", copies);
    b_ok = (test_main_check(3u == copies, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  C12 (T-B2-05, W6D-2e): PAUSE 3 times; RESUME_AT carries the saved
 *         frame with t_resume = copy 1 + 600 ms; STOP accepted from PAUSED.
 * @return true when every check passed.
 */
bool test_coord_pause_resume_stop (void)
{
    bool     b_ok  = true;
    uint32_t index = 0u;
    uint32_t count = 0u;

    b_ok = (test_main_check(tst_to_playing(PROTO_POLICY_CONTINUE), __LINE__)) &&
           b_ok;
    /* The result is not needed: the coordinator's own output. */
    (void)tst_find(PROTO_TYPE_START_AT, 0u, &index);

    proto_start_at_t start;

    /* The result is not needed: the coordinator's own output. */
    (void)proto_decode_start_at(coordinator_port_get(index)->bytes,
                                coordinator_port_get(index)->len,
                                &start); /* Coordinator output. */
    tst_advance(UINT64_C(2000000));

    uint64_t const pause_us = coordinator_port_now_us() + TST_STEP_US;
    /* Ticks since origin, mod 600: what the saved frame must be. */
    uint64_t const expected =
        (((pause_us - start.t_start) * 60u) / UINT64_C(1000000)) % TST_FRAMES;

    tst_pc_command(PROTO_TYPE_PAUSE, TST_SESSION);
    tst_advance(UINT64_C(500000));
    count = 0u;
    index = 0u;

    while (0u != tst_find(PROTO_TYPE_PAUSE, index, &index))
    {
        count++;
        index++;
    }

    b_ok = (test_main_check((3u == count) && (PROTO_COORD_PAUSED ==
                                              coordinator_loop_test_state()),
                            __LINE__)) &&
           b_ok;

    tst_pc_command(PROTO_TYPE_RESUME_AT, TST_SESSION);
    tst_advance(UINT64_C(500000));
    b_ok = (test_main_check(0u != tst_find(PROTO_TYPE_RESUME_AT, 0u, &index),
                            __LINE__)) &&
           b_ok;

    proto_resume_at_t resume;

    /* The result is not needed: the coordinator's own output. */
    (void)proto_decode_resume_at(coordinator_port_get(index)->bytes,
                                 coordinator_port_get(index)->len,
                                 &resume); /* Coordinator output. */
    b_ok = (test_main_check(
               (expected == resume.frame_index) &&
                   (resume.t_resume ==
                    (coordinator_port_get(index)->t_us + UINT64_C(600000))),
               __LINE__)) &&
           b_ok;
    printf("    paused at frame %" PRIu32 ", RESUME_AT frame %" PRIu16 "\n",
           (uint32_t)expected, resume.frame_index); /* Below 600. */

    tst_pc_command(PROTO_TYPE_PAUSE, TST_SESSION);
    tst_advance(UINT64_C(500000));
    tst_pc_command(PROTO_TYPE_STOP, TST_SESSION);
    tst_advance(UINT64_C(500000));
    b_ok = (test_main_check(
               (0u != tst_find(PROTO_TYPE_STOP, 0u, &index)) &&
                   (PROTO_COORD_READY == coordinator_loop_test_state()),
               __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  C13 (T-B2-04 acceptance 4): pause policy; tile 2 stops: PAUSE goes
 *         to the wall, RECOVERING, and FAULT 5 s after the loss.
 * @return true when every check passed.
 */
bool test_coord_pause_policy (void)
{
    bool     b_ok  = true;
    uint32_t index = 0u;

    b_ok =
        (test_main_check(tst_to_playing(PROTO_POLICY_PAUSE), __LINE__)) && b_ok;
    tst_advance(UINT64_C(1000000));
    g_live = UINT16_C(0x0001);
    tst_advance(UINT64_C(3000000));

    uint64_t const lost = coordinator_loop_test_loss_time(2u);

    b_ok = (test_main_check(
               (0u != lost) && (0u != tst_find(PROTO_TYPE_PAUSE, 0u, &index)) &&
                   (PROTO_COORD_RECOVERING == coordinator_loop_test_state()),
               __LINE__)) &&
           b_ok;

    /* Run to 4.9 s after the loss: still RECOVERING; then past 5 s. */
    tst_advance((lost + UINT64_C(4900000)) - coordinator_port_now_us());
    b_ok = (test_main_check(PROTO_COORD_RECOVERING ==
                                coordinator_loop_test_state(),
                            __LINE__)) &&
           b_ok;
    tst_advance(UINT64_C(200000));
    b_ok = (test_main_check(PROTO_COORD_FAULT == coordinator_loop_test_state(),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  C14 (T-B2-04 acceptance 3 logic, OD-14 option (a)): continue
 *         policy; tile 2 lost, tile 1 keeps playing; tile 2 back with a
 *         matching cached asset gets a RESUME_AT of its own for a tick at
 *         least 600 ms ahead, carrying that tick's frame.
 * @return true when every check passed.
 */
bool test_coord_continue_rejoin (void)
{
    bool     b_ok  = true;
    uint32_t index = 0u;

    b_ok = (test_main_check(tst_to_playing(PROTO_POLICY_CONTINUE), __LINE__)) &&
           b_ok;
    /* The result is not needed: the coordinator's own output. */
    (void)tst_find(PROTO_TYPE_START_AT, 0u, &index);

    proto_start_at_t start;

    /* The result is not needed: the coordinator's own output. */
    (void)proto_decode_start_at(coordinator_port_get(index)->bytes,
                                coordinator_port_get(index)->len,
                                &start); /* Coordinator output. */
    g_live = UINT16_C(0x0001);
    tst_advance(UINT64_C(3000000));
    b_ok = (test_main_check(
               (PROTO_COORD_COORDINATING_PLAYBACK ==
                coordinator_loop_test_state()) &&
                   (UINT16_C(0x0001) == coordinator_loop_test_online()),
               __LINE__)) &&
           b_ok;

    uint32_t const sends_before = coordinator_port_count();

    tst_register(2u, TST_SESSION, TST_CRC_2);
    g_live = UINT16_C(0x0003);
    tst_advance(UINT64_C(100000));
    tst_sync_ready(2u, TST_SESSION);
    tst_advance(UINT64_C(500000));

    uint32_t copies = 0u;

    index = sends_before;

    while (0u != tst_find(PROTO_TYPE_RESUME_AT, index, &index))
    {
        proto_resume_at_t msg;
        uint64_t          tick = 0u;

        /* The result is not needed: the coordinator's own output. */
        (void)proto_decode_resume_at(coordinator_port_get(index)->bytes,
                                     coordinator_port_get(index)->len,
                                     &msg); /* Coordinator output. */

        while (tst_deadline(start.t_start, tick) < msg.t_resume)
        {
            tick++;
        }

        b_ok = (test_main_check(
                   (tst_addr(2u) == coordinator_port_get(index)->addr) &&
                       (tst_deadline(start.t_start, tick) == msg.t_resume) &&
                       ((tick % TST_FRAMES) == msg.frame_index) &&
                       ((msg.t_resume - coordinator_port_get(index)->t_us) >=
                        UINT64_C(400000)),
                   __LINE__)) &&
               b_ok;

        if (0u == copies)
        {
            b_ok = (test_main_check(
                       (msg.t_resume - coordinator_port_get(index)->t_us) >=
                           UINT64_C(600000),
                       __LINE__)) &&
                   b_ok;
        }

        copies++;
        index++;
    }

    printf("    rejoin: %" PRIu32 " RESUME_AT copies to tile 2 only\n", copies);
    b_ok = (test_main_check(3u == copies, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  C31 (bench 12 Nov 2026, run V2): a 9-tile SESSION_CONFIG through
 *         the receive path keeps every byte. A 3 x 3 wall with Node IDs 1
 *         to 9 and a different CRC32 each; every tile registers and sends
 *         ASSET_READY with its own CRC32, so all nine count ready. With the
 *         48-byte receive buffer (the message is 53 B since step 4d) the
 *         CRC32s of Nodes 8 and 9 were read from past the buffer and never
 *         matched. A datagram one byte longer than SESSION_CONFIG is
 *         dropped, not decoded.
 * @return true when every check passed.
 */
bool test_coord_full_config (void)
{
    uint8_t                buf[PROTO_SESSION_CONFIG_BYTES + 1u];
    proto_session_config_t msg = {
        {PROTO_TYPE_SESSION_CONFIG, TST_SESSION, 0u},
        3u, 3u, PROTO_POLICY_PAUSE, {{0u, 0u, 0u}}, TST_FRAMES, {0u}};
    bool                   b_ok = true;

    tst_reset();

    for (uint32_t node = 0u; node < PROTO_NODE_MAX; node++)
    {
        msg.cell[node].b_placed = 1u;
        msg.cell[node].row      = (uint8_t)(node / 3u); /* < 3: fits. */
        msg.cell[node].col      = (uint8_t)(node % 3u); /* < 3: fits. */
        /* Every byte of each CRC32 differs from the others' (node < 9). */
        msg.crc32[node] = UINT32_C(0x11111111) * (node + 1u);
    }

    /* One byte too long: dropped, the session is not applied. */
    (void)memset(buf, 0, sizeof(buf)); /* Returns buf. */
    msg.header.seq = g_pc_seq;
    /* Returns 53; the extra byte stays 0. */
    (void)proto_encode_session_config(&msg, buf, sizeof(buf));
    coordinator_loop_test_rx(CL_PORT_FROM_PC, TST_PC_ADDR, TST_PC_PORT, buf,
                             sizeof(buf));
    tst_advance(UINT64_C(100000));
    b_ok = (test_main_check(PROTO_COORD_WAITING_FOR_SESSION ==
                                coordinator_loop_test_state(),
                            __LINE__)) &&
           b_ok;

    /* The right length: applied, and every CRC32 arrives whole. */
    coordinator_loop_test_rx(CL_PORT_FROM_PC, TST_PC_ADDR, TST_PC_PORT, buf,
                             PROTO_SESSION_CONFIG_BYTES);
    g_pc_seq++;

    for (uint8_t node_id = 1u; node_id <= PROTO_NODE_MAX; node_id++)
    {
        tst_register(node_id, 0u, 0u);
    }

    tst_advance(UINT64_C(100000));

    for (uint8_t node_id = 1u; node_id <= PROTO_NODE_MAX; node_id++)
    {
        tst_asset_ready(node_id, TST_SESSION, msg.crc32[node_id - 1u]);
    }

    tst_advance(UINT64_C(100000));
    b_ok = (test_main_check(UINT16_C(0x01FF) ==
                                coordinator_loop_test_readiness(),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Fresh coordinator and recorder at T0.
 */
static void tst_reset (void)
{
    coordinator_port_reset(TST_T0_US);
    g_live      = 0u;
    g_pc_seq    = 0u;
    g_beat_sess = 0u;
    (void)memset(g_tile_seq, 0, sizeof(g_tile_seq));   /* Returns it. */
    (void)memset(g_last_beat, 0, sizeof(g_last_beat)); /* Returns it. */
    coordinator_loop_test_reset(TST_BCAST);
}

/**
 * @brief  Run passes for a duration in 50 us steps; live tiles heartbeat
 *         every 0.5 s.
 * @param  duration_us  How long, microseconds.
 */
static void tst_advance (uint64_t duration_us)
{
    uint64_t const end = coordinator_port_now_us() + duration_us;

    while (coordinator_port_now_us() < end)
    {
        for (uint8_t node_id = 1u; node_id <= CL_MAX_NODES; node_id++)
        {
            bool const b_live =
                (0u != (g_live & (UINT16_C(1) << (node_id - 1u))));

            if ((b_live) && ((coordinator_port_now_us() -
                              g_last_beat[node_id]) >= TST_HEARTBEAT_US))
            {
                tst_heartbeat(node_id);
            }
        }

        coordinator_loop_test_pass();
        coordinator_port_set_now(coordinator_port_now_us() + TST_STEP_US);
    }
}

/**
 * @brief  A datagram from tile node_id arrives now.
 * @param  node_id  1 to 9.
 * @param  p_buf    Bytes.
 * @param  len      Length.
 */
static void tst_rx_tile (uint8_t node_id, uint8_t const * p_buf, uint32_t len)
{
    coordinator_loop_test_rx(CL_PORT_FROM_TILE, tst_addr(node_id), 4210u, p_buf,
                             len);
}

/**
 * @brief  REGISTER from a tile (its seq counter restarts, as after a boot).
 * @param  node_id  1 to 9.
 * @param  session  Cached session_id.
 * @param  crc      Cached CRC32.
 */
static void tst_register (uint8_t node_id, uint8_t session, uint32_t crc)
{
    uint8_t                buf[PROTO_REGISTER_BYTES];
    proto_register_t const msg = {
        {PROTO_TYPE_REGISTER, g_beat_sess, 0u}, node_id, session, crc};

    g_tile_seq[node_id]  = 1u;
    g_last_beat[node_id] = coordinator_port_now_us();
    tst_rx_tile(node_id, buf, proto_encode_register(&msg, buf, sizeof(buf)));
}

/**
 * @brief  HEARTBEAT from a tile, in the session the tile heard last.
 * @param  node_id  1 to 9.
 */
static void tst_heartbeat (uint8_t node_id)
{
    uint8_t                 buf[PROTO_HEARTBEAT_BYTES];
    proto_heartbeat_t const msg = {
        {PROTO_TYPE_HEARTBEAT, g_beat_sess, g_tile_seq[node_id]}, node_id};

    g_tile_seq[node_id]++;
    g_last_beat[node_id] = coordinator_port_now_us();
    tst_rx_tile(node_id, buf, proto_encode_heartbeat(&msg, buf, sizeof(buf)));
}

/**
 * @brief  ASSET_READY from a tile.
 * @param  node_id  1 to 9.
 * @param  session  Session of the asset.
 * @param  crc      CRC32 of the asset.
 */
static void tst_asset_ready (uint8_t node_id, uint8_t session, uint32_t crc)
{
    uint8_t                   buf[PROTO_ASSET_READY_BYTES];
    proto_asset_ready_t const msg = {
        {PROTO_TYPE_ASSET_READY, session, g_tile_seq[node_id]}, node_id, crc};

    g_tile_seq[node_id]++;
    tst_rx_tile(node_id, buf, proto_encode_asset_ready(&msg, buf, sizeof(buf)));
}

/**
 * @brief  SYNC_READY from a tile.
 * @param  node_id  1 to 9.
 * @param  session  Session.
 */
static void tst_sync_ready (uint8_t node_id, uint8_t session)
{
    uint8_t                  buf[PROTO_SYNC_READY_BYTES];
    proto_sync_ready_t const msg = {
        {PROTO_TYPE_SYNC_READY, session, g_tile_seq[node_id]}, node_id};

    g_tile_seq[node_id]++;
    tst_rx_tile(node_id, buf, proto_encode_sync_ready(&msg, buf, sizeof(buf)));
}

/**
 * @brief  SESSION_CONFIG from the PC: 600 frames, CRC32 values TST_CRC_1
 *         and TST_CRC_2 at the positions of Node IDs 1 and 2.
 * @param  session  session_id.
 * @param  rows     Rows of the wall.
 * @param  cols     Columns of the wall.
 * @param  policy   proto_policy_t code.
 * @param  p_map    9 Node IDs, row-major over rows x cols.
 */
static void tst_session (uint8_t session, uint8_t rows, uint8_t cols,
                         proto_policy_t policy, uint8_t const * p_map)
{
    uint8_t                buf[PROTO_SESSION_CONFIG_BYTES];
    proto_session_config_t msg = {
        {PROTO_TYPE_SESSION_CONFIG, session, g_pc_seq},
        rows,
        cols,
        policy,
        {{0u, 0u, 0u}},
        TST_FRAMES,
        {0u}};

    /* The map is row-major; since step 4d each Node ID carries its cell,
     * row slot / cols and column slot % cols (cols is 1 to 9). */
    for (uint32_t slot = 0u; slot < TST_MAP_SLOTS; slot++)
    {
        uint8_t const node_id = p_map[slot];

        if ((node_id >= 1u) && (node_id <= PROTO_NODE_MAX) && (0u != cols))
        {
            proto_cell_t * const p_cell = &msg.cell[node_id - 1u];

            p_cell->b_placed = 1u;
            p_cell->row      = (uint8_t)(slot / cols); /* < 9: fits. */
            p_cell->col      = (uint8_t)(slot % cols); /* < 9: fits. */
            msg.crc32[node_id - 1u] = (1u == node_id)   ? TST_CRC_1
                                      : (2u == node_id) ? TST_CRC_2
                                                        : 0u;
        }
    }

    g_pc_seq++;
    coordinator_loop_test_rx(
        CL_PORT_FROM_PC, TST_PC_ADDR, TST_PC_PORT, buf,
        proto_encode_session_config(&msg, buf, sizeof(buf)));
}

/**
 * @brief  A dashboard command from the PC (layout_table.md section 10 item
 *         11: the START_AT, PAUSE, RESUME_AT and STOP rows, fields 0).
 * @param  type     Type code.
 * @param  session  session_id.
 */
static void tst_pc_command (proto_type_t type, uint8_t session)
{
    uint8_t                 buf[PROTO_RESUME_AT_BYTES];
    uint32_t                len    = 0u;
    proto_header_t const    header = {type, session, g_pc_seq};
    proto_start_at_t const  start  = {header, 0u};
    proto_resume_at_t const resume = {header, 0u, 0u};
    proto_command_t const   cmd    = {header};

    if (PROTO_TYPE_START_AT == type)
    {
        len = proto_encode_start_at(&start, buf, sizeof(buf));
    }
    else if (PROTO_TYPE_RESUME_AT == type)
    {
        len = proto_encode_resume_at(&resume, buf, sizeof(buf));
    }
    else if (PROTO_TYPE_PAUSE == type)
    {
        len = proto_encode_pause(&cmd, buf, sizeof(buf));
    }
    else
    {
        len = proto_encode_stop(&cmd, buf, sizeof(buf));
    }

    g_pc_seq++;
    coordinator_loop_test_rx(CL_PORT_FROM_PC, TST_PC_ADDR, TST_PC_PORT, buf,
                             len);
}

/**
 * @brief  2 tiles (Node IDs 1 and 2, 2x2 positions 0 and 1) to READY.
 * @param  policy  Failure policy.
 * @return true when READY was reached.
 */
static bool tst_to_ready (proto_policy_t policy)
{
    uint8_t const map[9] = {1u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};

    tst_reset();
    tst_session(TST_SESSION, TST_ROWS_2X2, TST_COLS_2X2, policy, map);
    tst_register(1u, 0u, 0u);
    tst_register(2u, 0u, 0u);
    g_live      = UINT16_C(0x0003);
    g_beat_sess = TST_SESSION;
    tst_advance(UINT64_C(100000));
    tst_asset_ready(1u, TST_SESSION, TST_CRC_1);
    tst_asset_ready(2u, TST_SESSION, TST_CRC_2);
    tst_sync_ready(1u, TST_SESSION);
    tst_sync_ready(2u, TST_SESSION);
    tst_advance(UINT64_C(100000));

    return (PROTO_COORD_READY == coordinator_loop_test_state());
}

/**
 * @brief  As tst_to_ready(), then Start and 1 s of playback.
 * @param  policy  Failure policy.
 * @return true when COORDINATING PLAYBACK was reached.
 */
static bool tst_to_playing (proto_policy_t policy)
{
    bool const b_ready = tst_to_ready(policy);

    tst_pc_command(PROTO_TYPE_START_AT, TST_SESSION);
    tst_advance(UINT64_C(1000000));

    return (b_ready) &&
           (PROTO_COORD_COORDINATING_PLAYBACK == coordinator_loop_test_state());
}

/**
 * @brief  Find the next recorded send of a message type from an index.
 * @param  type     Type code.
 * @param  from     First index to look at.
 * @param  p_index  Receives the index found.
 * @return 1 when found, 0 otherwise.
 */
static uint32_t tst_find (proto_type_t type, uint32_t from, uint32_t * p_index)
{
    uint32_t found = 0u;

    for (uint32_t idx = from; (idx < coordinator_port_count()) && (0u == found);
         idx++)
    {
        proto_header_t header;

        coordinator_port_send_t const * const p_send =
            coordinator_port_get(idx);

        /* A decoded header type is below 18, a valid proto_type_t. */
        if ((proto_decode_header(p_send->bytes, p_send->len, &header)) &&
            (type == (proto_type_t)header.type))
        {
            *p_index = idx;
            found    = 1u;
        }
    }

    return found;
}

/**
 * @brief  Address of simulated tile node_id.
 * @param  node_id  1 to 9.
 * @return Address.
 */
static uint32_t tst_addr (uint8_t node_id)
{
    return UINT32_C(0x0001A8C0) + ((uint32_t)node_id << 24u); /* 1 to 9. */
}

/**
 * @brief  seq of a recorded send.
 * @param  index  Recorded send.
 * @return Its header seq.
 */
static uint16_t tst_seq_of (uint32_t index)
{
    proto_header_t header = {0u, 0u, 0u};

    /* The result is not needed: the coordinator's own output. */
    (void)proto_decode_header(coordinator_port_get(index)->bytes,
                              coordinator_port_get(index)->len,
                              &header); /* Recorded sends decode. */

    return header.seq;
}

/**
 * @brief  Reference tick deadline: origin + ceil(tick x 10^6 / 60).
 * @param  origin  Origin, microseconds.
 * @param  tick    Tick.
 * @return Deadline.
 */
static uint64_t tst_deadline (uint64_t origin, uint64_t tick)
{
    return origin + (((tick * UINT64_C(1000000)) + 59u) / 60u);
}

/*** end of file ***/
