/**
 * @file    test_coord_game.c
 * @brief   Host tests of the coordinator's game (C20 to C27): the PC's mode
 *          and round commands, the round phases (ARM, countdown, abort, tap
 *          test, STATUS counts), the acked tile messages, TAP relay and the
 *          score on the wire. The simulated tiles and the PC feed encoded
 *          datagrams to the coordinator on a fake clock.
 *
 * Owner: Benjamin (Buddy 2). Written for the whack-a-mole re-plan, build
 * step 7.
 */
#include "test_coord_game.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "coordinator_loop.h"
#include "coordinator_port.h"
#include "game_rules.h"
#include "proto.h"
#include "score_font.h"
#include "test_main.h"

/** Fake clock start, 1 s after boot (plan choice), and the pass step. */
#define GT_T0      UINT64_C(1000000)
#define GT_STEP_US UINT64_C(50)
#define GT_MS      UINT64_C(1000)

/** Heartbeat period of the simulated tiles, 0.5 s. */
#define GT_HEARTBEAT_US UINT64_C(500000)

/** Session, its frame count and the CRC32 of tiles 1 and 2. */
#define GT_SESSION 7u
#define GT_FRAMES  600u
#define GT_CRC_1   UINT32_C(0x11111111)
#define GT_CRC_2   UINT32_C(0x22222222)

/** Addresses: broadcast, PC (its source port is 4213). */
#define GT_BCAST   UINT32_C(0xFF01A8C0)
#define GT_PC_ADDR UINT32_C(0x6401A8C0)
#define GT_PC_PORT 4213u

/** Ports a tile sends from and the coordinator's tile port. */
#define GT_TILE_SRC_PORT 4210u

/** Round length, from the rules header. */
#define GT_ROUND_US (GT_MS * (uint64_t)GAME_ROUND_MS)

/** Tile state of the simulation. */
static uint16_t g_pc_seq         = 0u;
static uint16_t g_tile_seq[CL_MAX_NODES + 1u];
static uint16_t g_ack_seq        = 0u; /* Tile's ACK header seq. */
static uint16_t g_tap_seq        = 0u;
static uint64_t g_last_beat[CL_MAX_NODES + 1u];
static uint16_t g_live           = 0u;
static uint32_t g_ack_scan       = 0u; /* Sends already answered by ACK. */
static uint32_t g_score_tiles    = 0u; /* Tiles given a WALL_PICTURE. */

static void gt_reset (void);
static void gt_advance (uint64_t duration_us);
static bool gt_in_state (proto_coord_state_t state);
static void gt_rx_tile (uint8_t node_id, uint8_t const * p_buf, uint32_t len);
static void gt_rx_pc (uint8_t const * p_buf, uint32_t len);
static void gt_to_ready (void);
static void gt_pc_command (uint8_t command, uint8_t level, uint8_t threshold,
                           uint16_t seq);
static void gt_pc_control (uint8_t command, uint16_t seq);
static void gt_pc (uint8_t command, uint8_t level, uint8_t threshold);
static bool gt_status (proto_status_t * p_msg);
static uint8_t gt_round_state (void);
static bool gt_last_cmd (uint8_t node_id, uint8_t command,
                         proto_game_control_t * p_msg);
static uint32_t gt_count_to (proto_type_t type, uint8_t node_id);
static bool gt_last_target (uint8_t node_id, proto_mole_on_t * p_msg);
static bool gt_newest_mole (proto_mole_on_t * p_msg);
static bool gt_check_countdown (uint8_t node_id, uint64_t t_first);
static void gt_arm (uint8_t level, uint8_t threshold);
static bool gt_check_results (uint8_t node_id);
static void gt_tile_ack (uint8_t node_id, uint16_t acked_seq);
static void gt_ack_type (proto_type_t type);
static void gt_tap (uint8_t node_id, uint16_t peak, uint64_t t_tap_us,
                    uint16_t seq);
static uint32_t gt_addr (uint8_t node_id);
static uint32_t gt_find (proto_type_t type, uint16_t port, uint32_t from,
                         uint32_t * p_index);
static uint32_t gt_count_seq (proto_type_t type, uint16_t port, uint16_t seq);
static uint32_t gt_count_start (uint8_t node_id);
static uint32_t gt_count_cmd (uint8_t node_id, uint8_t command);
static uint32_t gt_ack_count (uint8_t node_id, uint16_t acked_seq);
static void gt_session_config (uint8_t session);
static bool gt_pc_acked (uint16_t seq);
static bool gt_node_seq (coordinator_port_send_t const * p_send,
                         proto_type_t type, uint8_t * p_node,
                         uint16_t * p_seq);
static bool gt_last_status (int32_t * p_score, uint8_t * p_state);
static uint32_t gt_popcount (uint64_t value);
static uint32_t gt_score_pixels (void);

/**
 * @brief  Run passes for a duration in 50 us steps; live tiles heartbeat
 *         every 0.5 s.
 * @param  duration_us  How long, microseconds.
 */
static void gt_advance (uint64_t duration_us)
{
    uint64_t const end = coordinator_port_now_us() + duration_us;

    while (coordinator_port_now_us() < end)
    {
        for (uint8_t node_id = 1u; node_id <= CL_MAX_NODES; node_id++)
        {
            bool const b_live =
                (0u != (g_live & (UINT16_C(1) << (node_id - 1u))));

            if ((b_live) && ((coordinator_port_now_us() -
                              g_last_beat[node_id]) >= GT_HEARTBEAT_US))
            {
                uint8_t                 buf[PROTO_HEARTBEAT_BYTES];
                proto_heartbeat_t const msg = {
                    {PROTO_TYPE_HEARTBEAT, GT_SESSION, g_tile_seq[node_id]},
                    node_id};

                g_tile_seq[node_id]++;
                g_last_beat[node_id] = coordinator_port_now_us();
                gt_rx_tile(node_id, buf,
                           proto_encode_heartbeat(&msg, buf, sizeof(buf)));
            }
        }

        coordinator_loop_test_pass();
        coordinator_port_set_now(coordinator_port_now_us() + GT_STEP_US);
    }
}

/**
 * @brief  Fresh simulation at GT_T0.
 */
static void gt_reset (void)
{
    coordinator_port_reset(GT_T0);
    g_pc_seq   = 0u;
    g_ack_seq  = 0u;
    g_tap_seq  = 0u;
    g_live     = 0u;
    g_ack_scan = 0u;
    (void)memset(g_tile_seq, 0, sizeof(g_tile_seq));   /* Returns it. */
    (void)memset(g_last_beat, 0, sizeof(g_last_beat)); /* Returns it. */
    coordinator_loop_test_reset(GT_BCAST);
}

/**
 * @brief  A datagram from tile node_id arrives now.
 * @param  node_id  1 to 9.
 * @param  p_buf    Bytes.
 * @param  len      Length.
 */
static void gt_rx_tile (uint8_t node_id, uint8_t const * p_buf, uint32_t len)
{
    coordinator_loop_test_rx(CL_PORT_FROM_TILE, gt_addr(node_id),
                             GT_TILE_SRC_PORT, p_buf, len);
}

/**
 * @brief  A datagram from the PC arrives now.
 * @param  p_buf  Bytes.
 * @param  len    Length.
 */
static void gt_rx_pc (uint8_t const * p_buf, uint32_t len)
{
    coordinator_loop_test_rx(CL_PORT_FROM_PC, GT_PC_ADDR, GT_PC_PORT, p_buf,
                             len);
}

/**
 * @brief  Tile address of a Node ID (any distinct value).
 * @param  node_id  1 to 9.
 * @return The address.
 */
static uint32_t gt_addr (uint8_t node_id)
{
    return UINT32_C(0x0001A8C0) + ((uint32_t)node_id << 24u); /* 1 to 9. */
}

/**
 * @brief  Two tiles (Node IDs 1 and 2) on a 2 x 2 wall, registered, live,
 *         ready and synchronised: READY.
 */
static void gt_to_ready (void)
{
    uint8_t const map[PROTO_MAP_SLOTS] = {1u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    uint8_t       buf[PROTO_SESSION_CONFIG_BYTES];
    proto_session_config_t msg = {
        {PROTO_TYPE_SESSION_CONFIG, GT_SESSION, 0u},
        2u, 2u, PROTO_POLICY_PAUSE, {0u}, GT_FRAMES, {0u}};

    gt_reset();
    msg.header.seq = g_pc_seq; /* After the reset, which zeroes it. */

    for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
    {
        msg.node_map[slot] = map[slot];
        msg.crc32[slot]    = (1u == map[slot]) ? GT_CRC_1
                             : (2u == map[slot]) ? GT_CRC_2
                                                 : 0u;
    }

    g_pc_seq++;
    gt_rx_pc(buf, proto_encode_session_config(&msg, buf, sizeof(buf)));

    for (uint8_t node_id = 1u; node_id <= 2u; node_id++)
    {
        uint8_t                 reg[PROTO_REGISTER_BYTES];
        proto_register_t const  reg_msg = {
            {PROTO_TYPE_REGISTER, 0u, 0u}, node_id, 0u, 0u};

        g_tile_seq[node_id]  = 1u;
        g_last_beat[node_id] = coordinator_port_now_us();
        gt_rx_tile(node_id, reg,
                   proto_encode_register(&reg_msg, reg, sizeof(reg)));
    }

    g_live = UINT16_C(0x0003);
    gt_advance(GT_MS * 100u);

    for (uint8_t node_id = 1u; node_id <= 2u; node_id++)
    {
        uint8_t                   ready_buf[PROTO_ASSET_READY_BYTES];
        proto_asset_ready_t const ar_msg = {
            {PROTO_TYPE_ASSET_READY, GT_SESSION, g_tile_seq[node_id]},
            node_id, (1u == node_id) ? GT_CRC_1 : GT_CRC_2};
        uint8_t                  sync_buf[PROTO_SYNC_READY_BYTES];
        proto_sync_ready_t const sr_msg = {
            {PROTO_TYPE_SYNC_READY, GT_SESSION, g_tile_seq[node_id] + 1u},
            node_id};

        g_tile_seq[node_id] += 2u;
        gt_rx_tile(node_id, ready_buf,
                   proto_encode_asset_ready(&ar_msg, ready_buf,
                                            sizeof(ready_buf)));
        gt_rx_tile(node_id, sync_buf,
                   proto_encode_sync_ready(&sr_msg, sync_buf,
                                           sizeof(sync_buf)));
    }

    gt_advance(GT_MS * 100u);
}

/**
 * @brief  GAME_CONTROL from the PC (node_id 0) with a given seq, at level
 *         and threshold given.
 * @param  command    proto_game_cmd_t value.
 * @param  level      proto_level_t value.
 * @param  threshold  Tap threshold, 20 mg steps; 0 = the tile's default.
 * @param  seq        Header seq.
 */
static void gt_pc_command (uint8_t command, uint8_t level, uint8_t threshold,
                           uint16_t seq)
{
    uint8_t                      buf[PROTO_GAME_CONTROL_BYTES];
    proto_game_control_t const   msg = {
        {PROTO_TYPE_GAME_CONTROL, GT_SESSION, seq}, 0u, command, level,
        threshold};

    gt_rx_pc(buf, proto_encode_game_control(&msg, buf, sizeof(buf)));
}

/**
 * @brief  GAME_CONTROL from the PC (node_id 0) with a given seq, at Hard
 *         (the v1 mole curve the round tests expect).
 * @param  command  proto_game_cmd_t value.
 * @param  seq      Header seq.
 */
static void gt_pc_control (uint8_t command, uint16_t seq)
{
    gt_pc_command(command, (uint8_t)PROTO_LEVEL_HARD, 0u, seq); /* Fits. */
}

/**
 * @brief  ACK from a tile for one message of the Pico.
 * @param  node_id    1 to 9.
 * @param  acked_seq  Header seq acknowledged.
 */
static void gt_tile_ack (uint8_t node_id, uint16_t acked_seq)
{
    uint8_t              buf[PROTO_ACK_BYTES];
    proto_ack_t const    msg = {
        {PROTO_TYPE_ACK, GT_SESSION, g_ack_seq}, node_id, acked_seq};

    g_ack_seq++;
    gt_rx_tile(node_id, buf, proto_encode_ack(&msg, buf, sizeof(buf)));
}

/**
 * @brief  ACK every new message of one type the Pico sent to a tile, once.
 * @param  type  Type code (GAME_CONTROL, MOLE_ON, FEEDBACK or WALL_PICTURE).
 */
static void gt_ack_type (proto_type_t type)
{
    uint32_t const count = coordinator_port_count();

    for (uint32_t index = g_ack_scan; index < count; index++)
    {
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);
        uint8_t  node_id = 0u;
        uint16_t seq     = 0u;

        if ((GT_TILE_SRC_PORT == p_send->port) &&
            (gt_node_seq(p_send, type, &node_id, &seq)))
        {
            gt_tile_ack(node_id, seq);
        }
    }

    g_ack_scan = count;
}

/**
 * @brief  TAP from a tile (the tile's own tap seq and session).
 * @param  node_id   1 to 9.
 * @param  peak      tap_detect peak.
 * @param  t_tap_us Tap time, Pico clock.
 * @param  seq       Header seq of the TAP.
 */
static void gt_tap (uint8_t node_id, uint16_t peak, uint64_t t_tap_us,
                    uint16_t seq)
{
    uint8_t              buf[PROTO_TAP_BYTES];
    proto_tap_t const    msg = {
        {PROTO_TYPE_TAP, GT_SESSION, seq}, node_id, peak, t_tap_us};

    gt_rx_tile(node_id, buf, proto_encode_tap(&msg, buf, sizeof(buf)));
}

/**
 * @brief  Find the next recorded send of a type, optionally on one port.
 * @param  type     Type code.
 * @param  port     Port wanted, or 0 for any.
 * @param  from     First index.
 * @param  p_index  Receives the index.
 * @return 1 when found, 0 otherwise.
 */
static uint32_t gt_find (proto_type_t type, uint16_t port, uint32_t from,
                         uint32_t * p_index)
{
    uint32_t found = 0u;

    for (uint32_t index = from;
         (index < coordinator_port_count()) && (0u == found); index++)
    {
        proto_header_t header;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((proto_decode_header(p_send->bytes, p_send->len, &header)) &&
            (type == header.type) &&
            ((0u == port) || (port == p_send->port)))
        {
            *p_index = index;
            found    = 1u;
        }
    }

    return found;
}

/**
 * @brief  Count the sends of a type on a port with one header seq.
 * @param  type  Type code.
 * @param  port  Port.
 * @param  seq   Header seq.
 * @return The count.
 */
static uint32_t gt_count_seq (proto_type_t type, uint16_t port, uint16_t seq)
{
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_header_t header;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((proto_decode_header(p_send->bytes, p_send->len, &header)) &&
            (type == header.type) && (port == p_send->port) &&
            (seq == header.seq))
        {
            count++;
        }
    }

    return count;
}

/**
 * @brief  Count the GAME_CONTROL START sends to one tile (any seq).
 * @param  node_id  Tile, 1 to 9.
 * @return The count.
 */
static uint32_t gt_count_start (uint8_t node_id)
{
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_game_control_t msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_TILE_SRC_PORT == p_send->port) &&
            (proto_decode_game_control(p_send->bytes, p_send->len, &msg)) &&
            (node_id == msg.node_id) && (PROTO_GAME_START == msg.command))
        {
            count++;
        }
    }

    return count;
}

/**
 * @brief  Count the GAME_CONTROL sends to one tile with one command.
 * @param  node_id  Tile, 1 to 9.
 * @param  command  proto_game_cmd_t value.
 * @return The count.
 */
static uint32_t gt_count_cmd (uint8_t node_id, uint8_t command)
{
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_game_control_t msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_TILE_SRC_PORT == p_send->port) &&
            (proto_decode_game_control(p_send->bytes, p_send->len, &msg)) &&
            (node_id == msg.node_id) && (command == msg.command))
        {
            count++;
        }
    }

    return count;
}

/**
 * @brief  Count the ACKs the Pico sent to one tile for one seq.
 * @param  node_id    Tile, 1 to 9.
 * @param  acked_seq  Seq acknowledged.
 * @return The count.
 */
static uint32_t gt_ack_count (uint8_t node_id, uint16_t acked_seq)
{
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_ack_t msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_TILE_SRC_PORT == p_send->port) &&
            (proto_decode_ack(p_send->bytes, p_send->len, &msg)) &&
            (node_id == msg.node_id) && (acked_seq == msg.acked_seq))
        {
            count++;
        }
    }

    return count;
}

/**
 * @brief  SESSION_CONFIG from the PC for the 2 x 2 wall of gt_to_ready().
 * @param  session  session_id to send.
 */
static void gt_session_config (uint8_t session)
{
    uint8_t                buf[PROTO_SESSION_CONFIG_BYTES];
    proto_session_config_t msg = {
        {PROTO_TYPE_SESSION_CONFIG, session, g_pc_seq},
        2u, 2u, PROTO_POLICY_PAUSE, {1u, 2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u},
        GT_FRAMES, {GT_CRC_1, GT_CRC_2, 0u, 0u, 0u, 0u, 0u, 0u, 0u}};

    g_pc_seq++;
    gt_rx_pc(buf, proto_encode_session_config(&msg, buf, sizeof(buf)));
}

/**
 * @brief  Whether the Pico ACKed a PC command with this seq (on port 4213).
 * @param  seq  Header seq of the PC command.
 * @return true when an ACK with node 0 names it.
 */
static bool gt_pc_acked (uint16_t seq)
{
    bool b_acked = false;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_ack_t          msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_PC_PORT == p_send->port) &&
            (proto_decode_ack(p_send->bytes, p_send->len, &msg)) &&
            (0u == msg.node_id) && (seq == msg.acked_seq))
        {
            b_acked = true;
        }
    }

    return b_acked;
}

/**
 * @brief  Node and seq of a tile message the Pico sent.
 * @param  p_send  The send.
 * @param  type    Its type.
 * @param  p_node  Receives the addressed tile.
 * @param  p_seq   Receives the header seq.
 * @return true when the send decodes as that type.
 */
static bool gt_node_seq (coordinator_port_send_t const * p_send,
                         proto_type_t type, uint8_t * p_node, uint16_t * p_seq)
{
    bool b_ok = false;
    proto_header_t header;

    if ((proto_decode_header(p_send->bytes, p_send->len, &header)) &&
        (type == header.type))
    {
        b_ok = true;
        *p_seq = header.seq;

        if (PROTO_TYPE_GAME_CONTROL == type)
        {
            proto_game_control_t msg;
            b_ok = proto_decode_game_control(p_send->bytes, p_send->len, &msg);
            *p_node = msg.node_id;
        }
        else if (PROTO_TYPE_MOLE_ON == type)
        {
            proto_mole_on_t msg;
            b_ok = proto_decode_mole_on(p_send->bytes, p_send->len, &msg);
            *p_node = msg.node_id;
        }
        else if (PROTO_TYPE_FEEDBACK == type)
        {
            proto_feedback_msg_t msg;
            b_ok = proto_decode_feedback(p_send->bytes, p_send->len, &msg);
            *p_node = msg.node_id;
        }
        else
        {
            proto_wall_picture_t msg;
            b_ok = proto_decode_wall_picture(p_send->bytes, p_send->len, &msg);
            *p_node = msg.node_id;
        }
    }

    return b_ok;
}

/**
 * @brief  The newest STATUS the Pico sent to the PC.
 * @param  p_score  Receives its score.
 * @param  p_state  Receives its coordinator state.
 * @return true when one was found.
 */
static bool gt_last_status (int32_t * p_score, uint8_t * p_state)
{
    bool b_found = false;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_status_t       msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_PC_PORT == p_send->port) &&
            (proto_decode_status(p_send->bytes, p_send->len, &msg)))
        {
            *p_score = msg.score;
            *p_state = msg.state;
            b_found  = true;
        }
    }

    return b_found;
}

/**
 * @brief  Set bits of a 64-bit mask.
 * @param  value  Mask.
 * @return The count.
 */
static uint32_t gt_popcount (uint64_t value)
{
    uint32_t count = 0u;

    for (uint32_t bit = 0u; bit < 64u; bit++)
    {
        if (0u != (value & (UINT64_C(1) << bit)))
        {
            count++;
        }
    }

    return count;
}

/**
 * @brief  Lit pixels of the score drawn across the wall: the first score
 *         entry (list slot 1) of each tile's results (retries repeat it, so
 *         only the first counts), summed.
 * @return Total lit pixels, and the number of tiles that got one in
 *         g_score_tiles.
 */
static uint32_t gt_score_pixels (void)
{
    uint64_t seen_mask[CL_MAX_NODES + 1u];
    bool     b_seen[CL_MAX_NODES + 1u];
    uint32_t total = 0u;

    (void)memset(b_seen, 0, sizeof(b_seen)); /* Returns b_seen. */
    (void)memset(seen_mask, 0, sizeof(seen_mask)); /* Returns seen_mask. */
    g_score_tiles = 0u;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_wall_picture_t msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_TILE_SRC_PORT == p_send->port) &&
            (proto_decode_wall_picture(p_send->bytes, p_send->len, &msg)) &&
            (1u == msg.slot) && (!b_seen[msg.node_id]))
        {
            b_seen[msg.node_id]      = true;
            seen_mask[msg.node_id] = msg.mask;
            g_score_tiles++;
        }
    }

    for (uint32_t node = 1u; node <= CL_MAX_NODES; node++)
    {
        total += gt_popcount(seen_mask[node]);
    }

    return total;
}

/**
 * @brief  C20: GAME_CONTROL START is refused in video mode; the mode switch
 *         is ACKed to the PC; START then starts the round (START to each
 *         tile, ACKed by the tiles); a repeated GAME_CONTROL with the same
 *         seq is ACKed again and starts nothing.
 * @return true when every check passed.
 */
bool test_coord_game_mode_and_start (void)
{
    bool     b_ok  = true;
    uint16_t seq   = 0u;
    uint32_t index = 0u;
    uint32_t count = 0u;

    gt_to_ready();
    b_ok = (test_main_check(PROTO_COORD_READY == coordinator_loop_test_state(),
                            __LINE__)) && b_ok;

    /* START in video mode: refused, the wall stays READY, no START goes out. */
    gt_pc_control(PROTO_GAME_START, g_pc_seq);
    g_pc_seq++;
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(PROTO_COORD_READY == coordinator_loop_test_state(),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == gt_find(PROTO_TYPE_GAME_CONTROL,
                                          GT_TILE_SRC_PORT, 0u, &index),
                            __LINE__)) && b_ok;

    /* The mode switch is ACKed to the PC with the command's seq. */
    seq = g_pc_seq;
    gt_pc_control(PROTO_GAME_MODE_GAME, seq);
    g_pc_seq++;
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(gt_pc_acked(seq), __LINE__)) && b_ok;

    /* START now starts the round: COORDINATING GAME, START to tiles 1 and 2. */
    seq = g_pc_seq;
    gt_pc_control(PROTO_GAME_START, seq);
    g_pc_seq++;
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(gt_pc_acked(seq), __LINE__)) && b_ok;
    b_ok = (test_main_check(gt_in_state(PROTO_COORD_COORDINATING_GAME),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == gt_find(PROTO_TYPE_GAME_CONTROL,
                                          GT_TILE_SRC_PORT, 0u, &index),
                            __LINE__)) && b_ok;

    gt_ack_type(PROTO_TYPE_GAME_CONTROL);
    gt_advance(10u * GT_MS);
    count = gt_count_start(1u);
    b_ok = (test_main_check(1u == count, __LINE__)) && b_ok; /* ACKed: once. */
    b_ok = (test_main_check(1u == gt_count_start(2u), __LINE__)) && b_ok;

    /* The same START again (a retry of the PC): ACKed, nothing new sent. */
    gt_pc_control(PROTO_GAME_START, seq);
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(gt_pc_acked(seq), __LINE__)) && b_ok;
    b_ok = (test_main_check(count == gt_count_start(1u), __LINE__)) && b_ok;
    b_ok = (test_main_check(gt_in_state(PROTO_COORD_COORDINATING_GAME),
                            __LINE__)) && b_ok;

    return b_ok;
}

/** What the round test learns about its first mole. */
typedef struct
{
    uint64_t t_round; /**< When the Pico took START. */
    uint8_t  mole;    /**< Tile of the mole. */
    uint16_t ms_seq;  /**< Header seq of its MOLE_ON. */
    uint64_t show;    /**< Its show time, Pico clock. */
    uint16_t up_ms;   /**< Its up time. */
    uint16_t tap_seq; /**< Header seq of the tap that hits it. */
} gt_round_t;

/**
 * @brief  Whether the coordinator is in a given state.
 * @param  state  State wanted.
 * @return true when it is.
 */
static bool gt_in_state (proto_coord_state_t state)
{
    return (state == coordinator_loop_test_state());
}

/**
 * @brief  Start a round from READY on the 2 x 2 wall: mode game, START, and
 *         the tiles ACK it. Records when the round started.
 * @param  p_round  Receives t_round.
 * @return true when the round is COORDINATING GAME.
 */
static bool gt_begin_round (gt_round_t * p_round)
{
    gt_to_ready();
    gt_pc_control(PROTO_GAME_MODE_GAME, g_pc_seq);
    g_pc_seq++;
    gt_advance(10u * GT_MS);

    p_round->t_round = coordinator_port_now_us();
    gt_pc_control(PROTO_GAME_START, g_pc_seq);
    g_pc_seq++;
    gt_advance(10u * GT_MS);
    gt_ack_type(PROTO_TYPE_GAME_CONTROL);

    return gt_in_state(PROTO_COORD_COORDINATING_GAME);
}

/**
 * @brief  Find the first MOLE_ON and check its timing: planned 100 ms in,
 *         shown 300 ms in, up 1193 or 1194 ms (elapsed 300 ms).
 * @param  p_round  Receives the mole.
 * @return true when every check passed.
 */
static bool gt_find_first_mole (gt_round_t * p_round)
{
    uint32_t          index = 0u;
    proto_mole_on_t   msg;
    proto_header_t    header;
    bool              b_ok  = true;

    gt_advance(150u * GT_MS);
    b_ok = (test_main_check(1u == gt_find(PROTO_TYPE_MOLE_ON,
                                          GT_TILE_SRC_PORT, 0u, &index),
                            __LINE__)) && b_ok;
    if (1u == gt_find(PROTO_TYPE_MOLE_ON, GT_TILE_SRC_PORT, 0u, &index))
    {
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        b_ok = (test_main_check(proto_decode_mole_on(p_send->bytes,
                                                     p_send->len, &msg),
                                __LINE__)) && b_ok;
        b_ok = (test_main_check(proto_decode_header(p_send->bytes,
                                                    p_send->len, &header),
                                __LINE__)) && b_ok;
        p_round->mole   = msg.node_id;
        p_round->show   = msg.t_show;
        p_round->up_ms  = msg.up_ms;
        p_round->ms_seq = header.seq;
    }

    b_ok = (test_main_check((p_round->show >=
                                 (p_round->t_round + (300u * GT_MS))) &&
                                (p_round->show <=
                                 (p_round->t_round + (301u * GT_MS))),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check((p_round->up_ms >= 1193u) &&
                                (p_round->up_ms <= 1194u), __LINE__)) && b_ok;
    b_ok = (test_main_check((1u == p_round->mole) || (2u == p_round->mole),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  The MOLE_ON is resent until its ACK, and not after it: the tile
 *         ACKs 50 ms before the show time.
 * @param  p_round  The mole.
 * @return true when every check passed.
 */
static bool gt_check_mole_resend (gt_round_t const * p_round)
{
    uint32_t sends = 0u;
    bool     b_ok  = true;

    if (coordinator_port_now_us() + (50u * GT_MS) < p_round->show)
    {
        gt_advance(p_round->show - (50u * GT_MS) - coordinator_port_now_us());
    }

    sends = gt_count_seq(PROTO_TYPE_MOLE_ON, GT_TILE_SRC_PORT,
                         p_round->ms_seq);
    b_ok = (test_main_check(sends >= 3u, __LINE__)) && b_ok;

    gt_tile_ack(p_round->mole, p_round->ms_seq);
    gt_advance(5u * GT_MS);
    sends = gt_count_seq(PROTO_TYPE_MOLE_ON, GT_TILE_SRC_PORT,
                         p_round->ms_seq);
    gt_advance(200u * GT_MS);
    b_ok = (test_main_check(sends == gt_count_seq(PROTO_TYPE_MOLE_ON,
                                                  GT_TILE_SRC_PORT,
                                                  p_round->ms_seq),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  A tap 20 ms into the mole: ACKed, relayed to the PC, and a green
 *         FEEDBACK to the mole's tile; STATUS then shows score 1.
 * @param  p_round  Receives tap_seq.
 * @return true when every check passed.
 */
static bool gt_check_hit (gt_round_t * p_round)
{
    uint32_t         index = 0u;
    proto_feedback_msg_t feedback;
    int32_t          score = 0;
    uint8_t          state = 0u;
    bool             b_ok  = true;

    p_round->tap_seq = ++g_tap_seq;
    gt_tap(p_round->mole, 1000u, p_round->show + (20u * GT_MS),
           p_round->tap_seq);
    gt_advance(200u * GT_MS);
    b_ok = (test_main_check(gt_ack_count(p_round->mole, p_round->tap_seq) >= 1u,
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == gt_count_seq(PROTO_TYPE_TAP, GT_PC_PORT,
                                               p_round->tap_seq),
                            __LINE__)) && b_ok;

    b_ok = (test_main_check(1u == gt_find(PROTO_TYPE_FEEDBACK,
                                          GT_TILE_SRC_PORT, 0u, &index),
                            __LINE__)) && b_ok;
    if (1u == gt_find(PROTO_TYPE_FEEDBACK, GT_TILE_SRC_PORT, 0u, &index))
    {
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        b_ok = (test_main_check(proto_decode_feedback(p_send->bytes,
                                                      p_send->len, &feedback),
                                __LINE__)) && b_ok;
        b_ok = (test_main_check((p_round->mole == feedback.node_id) &&
                                    (PROTO_FEEDBACK_HIT == feedback.result),
                                __LINE__)) && b_ok;
    }

    gt_ack_type(PROTO_TYPE_FEEDBACK);
    gt_advance(300u * GT_MS);
    b_ok = (test_main_check(gt_last_status(&score, &state), __LINE__)) && b_ok;
    b_ok = (test_main_check((1 == score) &&
                                (PROTO_COORD_COORDINATING_GAME == state),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  The round end at 30 s: END to both tiles, the score across the
 *         wall as 8 lit pixels (a 1), then READY with the score on STATUS.
 * @param  p_round  The round.
 * @return true when every check passed.
 */
static bool gt_check_round_end (gt_round_t const * p_round)
{
    uint64_t const target = p_round->t_round + (GT_ROUND_US + (100u * GT_MS));
    uint32_t       lit    = 0u;
    int32_t        score  = 0;
    uint8_t        state  = 0u;
    bool           b_ok   = true;

    if (coordinator_port_now_us() < target)
    {
        gt_advance(target - coordinator_port_now_us());
    }

    /* END is resent until ACKed; the tiles here never ACK, so it repeats. */
    b_ok = (test_main_check(gt_count_cmd(1u, PROTO_GAME_END) >= 1u,
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(gt_count_cmd(2u, PROTO_GAME_END) >= 1u,
                            __LINE__)) && b_ok;

    lit = gt_score_pixels();
    b_ok = (test_main_check(2u == g_score_tiles, __LINE__)) && b_ok;
    b_ok = (test_main_check(8u == lit, __LINE__)) && b_ok;
    b_ok = (test_main_check(gt_in_state(PROTO_COORD_READY), __LINE__)) && b_ok;

    gt_advance(300u * GT_MS);
    b_ok = (test_main_check(gt_last_status(&score, &state), __LINE__)) && b_ok;
    /* RESULTS: READY, and the final score stays on STATUS (DS-08). */
    b_ok = (test_main_check((1 == score) && (PROTO_COORD_READY == state),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  C21: a full round on a 2 x 2 wall: MOLE_ON resent until ACKed and
 *         then not again, a tap on the mole is ACKed, relayed to the PC and
 *         answered with a green FEEDBACK, STATUS carries the live score, and
 *         the round end sends END and the score across the wall (8 lit
 *         pixels for a 1), then the Pico is READY again.
 * @return true when every check passed.
 */
bool test_coord_game_round (void)
{
    gt_round_t round;
    bool       b_ok = false;

    (void)memset(&round, 0, sizeof(round)); /* Returns the round. */
    b_ok = gt_begin_round(&round);
    b_ok = (gt_find_first_mole(&round)) && b_ok;
    b_ok = (gt_check_mole_resend(&round)) && b_ok;
    b_ok = (gt_check_hit(&round)) && b_ok;
    b_ok = (gt_check_round_end(&round)) && b_ok;

    return b_ok;
}

/**
 * @brief  C22: a TAP outside a round is ACKed and relayed once (a repeat is
 *         ACKed but not relayed again); SESSION_CONFIG is refused during a
 *         round.
 * @return true when every check passed.
 */
bool test_coord_game_taps (void)
{
    bool     b_ok = true;
    uint16_t seq  = 0u;
    uint16_t tap  = 0u;
    int32_t  score = 0;
    uint8_t  state = 0u;

    gt_to_ready();

    /* Tap-to-place (video, READY): ACKed and relayed to the PC once. */
    tap = ++g_tap_seq;
    gt_tap(1u, 900u, coordinator_port_now_us(), tap);
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(1u == gt_ack_count(1u, tap), __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == gt_count_seq(PROTO_TYPE_TAP, GT_PC_PORT, tap),
                            __LINE__)) && b_ok;

    /* A repeat (the tile did not see the ACK): ACKed again, not relayed. */
    gt_tap(1u, 900u, coordinator_port_now_us(), tap);
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(2u == gt_ack_count(1u, tap), __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == gt_count_seq(PROTO_TYPE_TAP, GT_PC_PORT, tap),
                            __LINE__)) && b_ok;

    /* SESSION_CONFIG during a round is refused: the state stays COORDINATING
     * GAME and the new session is not applied (no ACK path to check). */
    gt_pc_control(PROTO_GAME_MODE_GAME, g_pc_seq);
    g_pc_seq++;
    gt_advance(10u * GT_MS);
    seq = g_pc_seq;
    gt_pc_control(PROTO_GAME_START, seq);
    g_pc_seq++;
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(gt_in_state(PROTO_COORD_COORDINATING_GAME),
                            __LINE__)) && b_ok;
    gt_session_config(9u);
    gt_advance(200u * GT_MS);
    b_ok = (test_main_check(gt_in_state(PROTO_COORD_COORDINATING_GAME),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(gt_last_status(&score, &state), __LINE__)) && b_ok;
    b_ok = (test_main_check(PROTO_COORD_COORDINATING_GAME == state,
                            __LINE__)) && b_ok;

    /* A TAP during the round is ACKed, and relayed once like any other. */
    tap = ++g_tap_seq;
    gt_tap(2u, 900u, coordinator_port_now_us(), tap);
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(1u == gt_ack_count(2u, tap), __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == gt_count_seq(PROTO_TYPE_TAP, GT_PC_PORT, tap),
                            __LINE__)) && b_ok;

    return b_ok;
}


/**
 * @brief  C23: a tile that restarts (REGISTER) starts its TAP seq again at a
 *         low value; the Pico takes those TAPs as new (layout_table.md section
 *         10 item 13), not as stale repeats of the TAPs before the restart.
 *         Bench 11 Nov 2026: node 4's taps were dropped as stale after a
 *         reflash, so its moles never turned green.
 * @return true when every check passed.
 */
bool test_coord_game_tap_restart (void)
{
    bool                   b_ok   = true;
    uint16_t const         before = 1000u; /* Seq the tile reached before. */
    uint16_t const         after  = 3u;    /* Its seq after the restart. */
    uint8_t                reg[PROTO_REGISTER_BYTES];
    proto_register_t const reg_msg = {
        {PROTO_TYPE_REGISTER, 0u, 0u}, 1u, 0u, 0u};

    gt_to_ready();

    gt_tap(1u, 900u, coordinator_port_now_us(), before);
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(1u == gt_count_seq(PROTO_TYPE_TAP, GT_PC_PORT,
                                               before),
                            __LINE__)) && b_ok;

    /* The tile restarts: REGISTER, then TAPs from a low seq. */
    gt_rx_tile(1u, reg, proto_encode_register(&reg_msg, reg, sizeof(reg)));
    gt_advance(10u * GT_MS);
    gt_tap(1u, 900u, coordinator_port_now_us(), after);
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(1u == gt_ack_count(1u, after), __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == gt_count_seq(PROTO_TYPE_TAP, GT_PC_PORT,
                                               after),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  A PC command with the next PC seq, then 10 ms of passes.
 * @param  command    proto_game_cmd_t value.
 * @param  level      proto_level_t value.
 * @param  threshold  Tap threshold, 20 mg steps.
 */
static void gt_pc (uint8_t command, uint8_t level, uint8_t threshold)
{
    gt_pc_command(command, level, threshold, g_pc_seq);
    g_pc_seq++;
    gt_advance(10u * GT_MS);
}

/**
 * @brief  The newest STATUS the Pico sent to the PC, all fields.
 * @param  p_msg  Receives it.
 * @return true when one was found.
 */
static bool gt_status (proto_status_t * p_msg)
{
    bool b_found = false;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_status_t                        msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_PC_PORT == p_send->port) &&
            (proto_decode_status(p_send->bytes, p_send->len, &msg)))
        {
            *p_msg  = msg;
            b_found = true;
        }
    }

    return b_found;
}

/**
 * @brief  The round state on the newest STATUS, after a STATUS period.
 * @return proto_round_state_t value, or PROTO_ROUND_STATE_COUNT when none.
 */
static uint8_t gt_round_state (void)
{
    proto_status_t msg;
    uint8_t        state = (uint8_t)PROTO_ROUND_STATE_COUNT; /* 5: fits. */

    gt_advance(300u * GT_MS);

    if (gt_status(&msg))
    {
        state = msg.round_state;
    }
    else
    {
        /* No STATUS yet. */
    }

    return state;
}

/**
 * @brief  The newest GAME_CONTROL with one command sent to one tile.
 * @param  node_id  Tile, 1 to 9.
 * @param  command  proto_game_cmd_t value.
 * @param  p_msg    Receives it.
 * @return true when one was found.
 */
static bool gt_last_cmd (uint8_t node_id, uint8_t command,
                         proto_game_control_t * p_msg)
{
    bool b_found = false;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_game_control_t                  msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_TILE_SRC_PORT == p_send->port) &&
            (proto_decode_game_control(p_send->bytes, p_send->len, &msg)) &&
            (node_id == msg.node_id) && (command == msg.command))
        {
            *p_msg  = msg;
            b_found = true;
        }
    }

    return b_found;
}

/**
 * @brief  Count the sends of one type to one tile (any seq, repeats too).
 * @param  type     MOLE_ON, FEEDBACK or WALL_PICTURE.
 * @param  node_id  Tile, 1 to 9.
 * @return The count.
 */
static uint32_t gt_count_to (proto_type_t type, uint8_t node_id)
{
    uint32_t count = 0u;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);
        uint8_t  node = 0u;
        uint16_t seq  = 0u;

        if ((GT_TILE_SRC_PORT == p_send->port) &&
            (gt_node_seq(p_send, type, &node, &seq)) && (node_id == node))
        {
            count++;
        }
    }

    return count;
}

/**
 * @brief  The newest MOLE_ON sent to one tile.
 * @param  node_id  Tile, 1 to 9.
 * @param  p_msg    Receives it.
 * @return true when one was found.
 */
static bool gt_last_target (uint8_t node_id, proto_mole_on_t * p_msg)
{
    bool b_found = false;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_mole_on_t                       msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_TILE_SRC_PORT == p_send->port) &&
            (proto_decode_mole_on(p_send->bytes, p_send->len, &msg)) &&
            (node_id == msg.node_id))
        {
            *p_msg  = msg;
            b_found = true;
        }
    }

    return b_found;
}

/**
 * @brief  The newest MOLE_ON of kind MOLE sent to any tile.
 * @param  p_msg  Receives it.
 * @return true when one was found.
 */
static bool gt_newest_mole (proto_mole_on_t * p_msg)
{
    bool b_found = false;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_mole_on_t                       msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_TILE_SRC_PORT == p_send->port) &&
            (proto_decode_mole_on(p_send->bytes, p_send->len, &msg)) &&
            (PROTO_TARGET_MOLE == msg.kind))
        {
            *p_msg  = msg;
            b_found = true;
        }
    }

    return b_found;
}

/**
 * @brief  The countdown on one tile: WALL_PICTURE entries 0, 1 and 2, yellow
 *         pictures of 1 s each, shown 1 s apart from t_first.
 * @param  node_id  Tile, 1 to 9.
 * @param  t_first  Show time of entry 0.
 * @return true when every check passed.
 */
static bool gt_check_countdown (uint8_t node_id, uint64_t t_first)
{
    uint32_t seen = 0u; /* Bit per entry found. */
    bool     b_ok = true;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_wall_picture_t                  msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_TILE_SRC_PORT == p_send->port) &&
            (proto_decode_wall_picture(p_send->bytes, p_send->len, &msg)) &&
            (node_id == msg.node_id))
        {
            uint64_t const t_want = t_first + (msg.slot * (1000u * GT_MS));

            seen |= (UINT32_C(1) << msg.slot); /* slot < 4. */
            b_ok = (test_main_check((PROTO_PIC_PICTURE == msg.effect) &&
                                        (PROTO_COLOUR_YELLOW == msg.colour) &&
                                        (0u == msg.b_repeat) &&
                                        (1000u == msg.dur_ms) &&
                                        (t_want == msg.t_show) &&
                                        (0u != msg.mask),
                                    __LINE__)) && b_ok;
        }
    }

    b_ok = (test_main_check(UINT32_C(0x7) == seen, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  The results list on one tile (DS-09): a sparkle (entry 0), the
 *         white score (1) and the yellow stars (2, looping back to 1), the
 *         score 1.5 s and the stars 4 s after the sparkle.
 * @param  node_id  Tile, 1 to 9.
 * @return true when every check passed.
 */
static bool gt_check_results (uint8_t node_id)
{
    uint64_t t_show[3] = {0u, 0u, 0u};
    uint32_t seen      = 0u; /* Bit per entry found. */
    bool     b_ok      = true;

    for (uint32_t index = 0u; index < coordinator_port_count(); index++)
    {
        proto_wall_picture_t                  msg;
        coordinator_port_send_t const * const p_send =
            coordinator_port_get(index);

        if ((GT_TILE_SRC_PORT == p_send->port) &&
            (proto_decode_wall_picture(p_send->bytes, p_send->len, &msg)) &&
            (node_id == msg.node_id) && (msg.slot < 3u))
        {
            bool const b_good =
                (0u == msg.slot)
                    ? ((PROTO_PIC_SPARKLE == msg.effect) &&
                       (0u == msg.b_repeat))
                : (1u == msg.slot)
                    ? ((PROTO_PIC_PICTURE == msg.effect) &&
                       (PROTO_COLOUR_WHITE == msg.colour) &&
                       (0u == msg.b_repeat) && (0u != msg.mask))
                    : ((PROTO_PIC_PICTURE == msg.effect) &&
                       (PROTO_COLOUR_YELLOW == msg.colour) &&
                       (1u == msg.b_repeat) && (0u != msg.mask));

            seen |= (UINT32_C(1) << msg.slot); /* slot < 3. */
            t_show[msg.slot] = msg.t_show;
            b_ok = (test_main_check(b_good, __LINE__)) && b_ok;
        }
    }

    b_ok = (test_main_check(UINT32_C(0x7) == seen, __LINE__)) && b_ok;
    b_ok = (test_main_check(((t_show[0] + (1500u * GT_MS)) == t_show[1]) &&
                                ((t_show[1] + (2500u * GT_MS)) == t_show[2]),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  To READY in game mode, then ARM at a level and threshold.
 * @param  level      proto_level_t value.
 * @param  threshold  Tap threshold, 20 mg steps.
 */
static void gt_arm (uint8_t level, uint8_t threshold)
{
    gt_to_ready();
    gt_pc(PROTO_GAME_MODE_GAME, 0u, 0u);
    gt_pc(PROTO_GAME_ARM, level, threshold);
}

/**
 * @brief  C24: ARM (DS-08): ARM with the level and threshold to both tiles,
 *         the start sprite (kind START, up until tapped) on the tile nearest
 *         the centre only; a tap elsewhere does nothing; a tap on the sprite
 *         turns it green and starts the 3 s countdown (3, 2, 1 on every
 *         tile); then START and the moles at the armed level.
 * @return true when every check passed.
 */
bool test_coord_game_arm (void)
{
    proto_game_control_t ctl;
    proto_mole_on_t      target;
    proto_status_t       status;
    uint64_t             t_tap = 0u;
    bool                 b_ok  = true;

    (void)memset(&ctl, 0, sizeof(ctl));       /* Returns it. */
    (void)memset(&target, 0, sizeof(target)); /* Returns it. */
    gt_arm((uint8_t)PROTO_LEVEL_NORMAL, 15u); /* 3 levels fit uint8_t. */
    b_ok = (test_main_check(gt_in_state(PROTO_COORD_COORDINATING_GAME),
                            __LINE__)) && b_ok;

    for (uint8_t node_id = 1u; node_id <= 2u; node_id++)
    {
        b_ok = (test_main_check(gt_last_cmd(node_id, PROTO_GAME_ARM, &ctl),
                                __LINE__)) && b_ok;
        b_ok = (test_main_check((PROTO_LEVEL_NORMAL == ctl.level) &&
                                    (15u == ctl.threshold),
                                __LINE__)) && b_ok;
    }

    /* A 2 x 2 wall: both tiles are as near the centre; the lower slot wins. */
    b_ok = (test_main_check(gt_last_target(1u, &target), __LINE__)) && b_ok;
    b_ok = (test_main_check((PROTO_TARGET_START == target.kind) &&
                                (0u == target.up_ms),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == gt_count_to(PROTO_TYPE_MOLE_ON, 2u),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(PROTO_ROUND_ARMED == gt_round_state(), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check((gt_status(&status)) &&
                                (PROTO_LEVEL_NORMAL == status.level),
                            __LINE__)) && b_ok;

    /* A tap on the other tile: no feedback, still armed. */
    gt_tap(2u, 900u, coordinator_port_now_us(), ++g_tap_seq);
    b_ok = (test_main_check(PROTO_ROUND_ARMED == gt_round_state(), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(0u == gt_count_to(PROTO_TYPE_FEEDBACK, 2u),
                            __LINE__)) && b_ok;

    /* The tap on the start sprite: green, then the countdown. */
    t_tap = coordinator_port_now_us();
    gt_tap(1u, 900u, t_tap, ++g_tap_seq);
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(1u <= gt_count_to(PROTO_TYPE_FEEDBACK, 1u),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(gt_check_countdown(1u, t_tap + (500u * GT_MS)),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(gt_check_countdown(2u, t_tap + (500u * GT_MS)),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(PROTO_ROUND_COUNTDOWN == gt_round_state(),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == gt_count_start(1u), __LINE__)) && b_ok;

    /* 3.5 s after the tap (0.5 s lead, 3 s of digits): the round plays. */
    gt_advance((t_tap + (3510u * GT_MS)) - coordinator_port_now_us());
    b_ok = (test_main_check((1u <= gt_count_start(1u)) &&
                                (1u <= gt_count_start(2u)),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(PROTO_ROUND_PLAYING == gt_round_state(), __LINE__))
           && b_ok;

    /* Normal: 1500 ms at the start, so about 1493 ms 300 ms in. */
    if ((!gt_last_target(2u, &target)) ||
        (PROTO_TARGET_MOLE != target.kind))
    {
        (void)gt_last_target(1u, &target); /* The mole is on tile 1. */
    }
    else
    {
        /* The mole is on tile 2. */
    }

    b_ok = (test_main_check((PROTO_TARGET_MOLE == target.kind) &&
                                (target.up_ms >= 1480u) &&
                                (target.up_ms <= 1500u),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  C25: START_NOW skips the sprite; END in the countdown aborts (END
 *         to the tiles, no round, READY); an armed sprite with no tap for
 *         60 s returns to GAME IDLE; START_NOW outside ARMED is refused.
 * @return true when every check passed.
 */
bool test_coord_game_abort (void)
{
    uint64_t t_arm = 0u;
    bool     b_ok  = true;

    gt_arm((uint8_t)PROTO_LEVEL_EASY, 0u); /* 3 levels fit uint8_t. */
    gt_pc(PROTO_GAME_START_NOW, 0u, 0u);
    b_ok = (test_main_check(PROTO_ROUND_COUNTDOWN == gt_round_state(),
                            __LINE__)) && b_ok;
    gt_pc(PROTO_GAME_END, 0u, 0u);
    b_ok = (test_main_check((1u <= gt_count_cmd(1u, PROTO_GAME_END)) &&
                                (1u <= gt_count_cmd(2u, PROTO_GAME_END)),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(gt_in_state(PROTO_COORD_READY), __LINE__)) && b_ok;
    b_ok = (test_main_check(PROTO_ROUND_IDLE == gt_round_state(), __LINE__)) &&
           b_ok;
    gt_advance(4000u * GT_MS);
    b_ok = (test_main_check(0u == gt_count_start(1u), __LINE__)) && b_ok;

    /* START_NOW with nothing armed: refused, still READY. */
    gt_pc(PROTO_GAME_START_NOW, 0u, 0u);
    b_ok = (test_main_check(gt_in_state(PROTO_COORD_READY), __LINE__)) && b_ok;

    /* Armed and never tapped: back to GAME IDLE after 60 s. */
    gt_pc(PROTO_GAME_ARM, (uint8_t)PROTO_LEVEL_EASY, 0u); /* Fits. */
    t_arm = coordinator_port_now_us() - (10u * GT_MS);
    gt_advance((t_arm + (59900u * GT_MS)) - coordinator_port_now_us());
    b_ok = (test_main_check(gt_in_state(PROTO_COORD_COORDINATING_GAME),
                            __LINE__)) && b_ok;
    gt_advance(200u * GT_MS);
    b_ok = (test_main_check(gt_in_state(PROTO_COORD_READY), __LINE__)) && b_ok;
    b_ok = (test_main_check(PROTO_ROUND_IDLE == gt_round_state(), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(0u == gt_count_start(1u), __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  C26: the tap test (A11): TAP_TEST_ON goes to both tiles, and every
 *         tap then lights its own tile green; after TAP_TEST_OFF a tap lights
 *         nothing; the tap test is refused while a round is armed.
 * @return true when every check passed.
 */
bool test_coord_game_tap_test (void)
{
    uint32_t sends = 0u;
    bool     b_ok  = true;

    gt_to_ready();
    gt_pc(PROTO_GAME_MODE_GAME, 0u, 0u);
    gt_pc(PROTO_GAME_TAP_TEST_ON, 0u, 0u);
    b_ok = (test_main_check((1u <= gt_count_cmd(1u, PROTO_GAME_TAP_TEST_ON)) &&
                                (1u <= gt_count_cmd(2u,
                                                    PROTO_GAME_TAP_TEST_ON)),
                            __LINE__)) && b_ok;

    gt_tap(2u, 900u, coordinator_port_now_us(), ++g_tap_seq);
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(1u <= gt_count_to(PROTO_TYPE_FEEDBACK, 2u),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == gt_count_to(PROTO_TYPE_FEEDBACK, 1u),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(gt_in_state(PROTO_COORD_READY), __LINE__)) && b_ok;

    gt_pc(PROTO_GAME_TAP_TEST_OFF, 0u, 0u);
    b_ok = (test_main_check(1u <= gt_count_cmd(1u, PROTO_GAME_TAP_TEST_OFF),
                            __LINE__)) && b_ok;
    gt_tap(1u, 900u, coordinator_port_now_us(), ++g_tap_seq);
    gt_advance(10u * GT_MS);
    b_ok = (test_main_check(0u == gt_count_to(PROTO_TYPE_FEEDBACK, 1u),
                            __LINE__)) && b_ok;

    /* Armed: refused (after the earlier TAP_TEST_ON stops its resends). */
    gt_advance(1100u * GT_MS);
    gt_pc(PROTO_GAME_ARM, (uint8_t)PROTO_LEVEL_EASY, 0u); /* Fits. */
    sends = gt_count_cmd(1u, PROTO_GAME_TAP_TEST_ON);
    gt_pc(PROTO_GAME_TAP_TEST_ON, 0u, 0u);
    b_ok = (test_main_check(sends == gt_count_cmd(1u, PROTO_GAME_TAP_TEST_ON),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  C27: the STATUS counts (DS-08): in a v1 round at Hard with one
 *         wrong tap and one hit 20 ms into a mole, STATUS shows the level,
 *         the time left, then in RESULTS: score 1, wrong 1, missed equal to
 *         the moles shown less the hit, and the hit's reaction time.
 * @return true when every check passed.
 */
bool test_coord_game_status (void)
{
    gt_round_t      round;
    proto_status_t  status;
    proto_mole_on_t mole;
    bool            b_ok = false;

    (void)memset(&mole, 0, sizeof(mole));     /* Returns it. */
    (void)memset(&round, 0, sizeof(round));   /* Returns the round. */
    (void)memset(&status, 0, sizeof(status)); /* Returns it. */
    b_ok = gt_begin_round(&round);
    b_ok = (gt_find_first_mole(&round)) && b_ok;

    /* The hit, then a tap on the tile without the newest mole: wrong. */
    b_ok = (gt_check_hit(&round)) && b_ok;
    b_ok = (test_main_check(gt_newest_mole(&mole), __LINE__)) && b_ok;
    /* The mole is tile 1 or 2, so the other tile fits uint8_t. */
    gt_tap((uint8_t)(3u - mole.node_id), 1000u, coordinator_port_now_us(),
           ++g_tap_seq);
    gt_advance(300u * GT_MS);
    b_ok = (test_main_check(gt_status(&status), __LINE__)) && b_ok;
    b_ok = (test_main_check((PROTO_ROUND_PLAYING == status.round_state) &&
                                (PROTO_LEVEL_HARD == status.level) &&
                                (1u == status.wrong) &&
                                (status.time_left_s >= 28u) &&
                                (status.time_left_s <= 30u),
                            __LINE__)) && b_ok;

    gt_advance((round.t_round + GT_ROUND_US + (400u * GT_MS)) -
               coordinator_port_now_us());
    b_ok = (test_main_check(gt_status(&status), __LINE__)) && b_ok;
    b_ok = (test_main_check((PROTO_ROUND_RESULTS == status.round_state) &&
                                (PROTO_COORD_READY == status.state) &&
                                (1 == status.score) && (1u == status.wrong) &&
                                (status.shown > 10u) &&
                                ((status.shown - 1u) == status.missed) &&
                                (0u == status.time_left_s) &&
                                (20u == status.reaction_ms),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(gt_check_results(1u), __LINE__)) && b_ok;
    b_ok = (test_main_check(gt_check_results(2u), __LINE__)) && b_ok;

    return b_ok;
}

/*** end of file ***/
