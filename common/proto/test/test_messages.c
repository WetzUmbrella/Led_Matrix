/**
 * @file    test_messages.c
 * @brief   Host tests of every message row of common/proto (T-B2-01): the
 *          boundary set, the encoded lengths of layout_table.md section 4,
 *          the valid-range rejections and the byte-aligned helpers.
 *
 * Implemented by Ahsanul (B4) for Benjamin, task T-B2-01.
 *
 * Boundary set (T-B2-01 acceptance): frame index 0 and 1023; t_p and every
 * timestamp 0 and 2^64 - 1; fault word 0 and 0xFFFFFFFF; bitmap 0 and
 * 0x1FF. Every other field takes its smallest valid value in corner 0 and
 * its largest in corner 1. Slew -100, 0, +100 is not a wire field (it lives
 * in timesync, T-B4-02 host test 1), so no message carries it.
 */
#include "test_messages.h"

#include <inttypes.h>
#include <stdio.h>

#include "proto.h"
#include "test_main.h"

#define CORNERS       2u
#define CORNER_MIN    0u
#define BUF_BYTES     (PROTO_ASSET_CHUNK_MAX_BYTES + 1u)
#define BITMAP_MAX    0x1FFu
#define CHUNK_PATTERN 0x5Au
#define TELEMETRY_SEQ 0x0102u
#define ACK_SEQ       0xBEEFu
#define LE_U16        UINT16_C(0x0102)
#define LE_U32        UINT32_C(0x01020304)
#define LE_U64        UINT64_C(0x0102030405060708)
#define LE_BYTES      8u
#define SCORE_OVER    (PROTO_SCORE_MAX + 1)  /* 100, one above the range. */
#define SCORE_UNDER   (-PROTO_SCORE_MAX - 1) /* -100, one below. */

/** Encoded lengths copied from protocol/layout_table.md section 4, so a
 *  change of width in proto.h that the table does not follow fails here. */
#define LEN_REGISTER       10u
#define LEN_HEARTBEAT      5u
#define LEN_SESSION_CONFIG 47u
#define LEN_MANIFEST       10u
#define LEN_CHUNK_HEAD     5u
#define LEN_CHUNK_ACK      5u
#define LEN_NACK           7u
#define LEN_BUSY           7u
#define LEN_ASSET_READY    9u
#define LEN_SYNC_READY     5u
#define LEN_BEACON         12u
#define LEN_START_AT       12u
#define LEN_RESUME_AT      13u
#define LEN_COMMAND        4u
#define LEN_STATUS         13u
#define LEN_TELEMETRY      9u
#define LEN_TELEMETRY_ACK  6u
#define LEN_TAP            15u
#define LEN_MOLE_ON        14u
#define LEN_FEEDBACK       5u
#define LEN_GAME_CONTROL   6u
#define LEN_WALL_PICTURE   23u
#define LEN_ACK            7u

/** The six whack-a-mole rows, and the largest value of the 4-bit node_id
 *  field (15), for the decode-side refusals. */
#define GAME_ROWS      6u
#define NODE_FIELD_MAX ((1u << PROTO_NODE_BITS) - 1u)

static proto_type_t const g_game_rows[GAME_ROWS] = {
    PROTO_TYPE_TAP,          PROTO_TYPE_MOLE_ON,    PROTO_TYPE_FEEDBACK,
    PROTO_TYPE_GAME_CONTROL, PROTO_TYPE_WALL_PICTURE, PROTO_TYPE_ACK};

static uint8_t  g_buf[BUF_BYTES];
static uint8_t  g_payload[PROTO_CHUNK_PAYLOAD_MAX];
static uint32_t g_mismatches = 0u;

static proto_header_t header_at (uint32_t corner);
static bool           same_header (proto_header_t const * p_left,
                                   proto_header_t const * p_right);
static uint8_t        node_at (uint32_t corner);
static void           tally (bool b_same);
static void           rt_register (uint32_t corner);
static void           rt_heartbeat (uint32_t corner);
static void           rt_session_config (uint32_t corner);
static void           rt_manifest (uint32_t corner);
static void           rt_asset_chunk (uint32_t corner);
static void           rt_chunk_ack (uint32_t corner);
static void           rt_nack (uint32_t corner);
static void           rt_busy (uint32_t corner);
static void           rt_asset_ready (uint32_t corner);
static void           rt_sync_ready (uint32_t corner);
static void           rt_beacon (uint32_t corner);
static void           rt_start_at (uint32_t corner);
static void           rt_resume_at (uint32_t corner);
static void           rt_commands (uint32_t corner);
static void           rt_status (uint32_t corner);
static void           rt_telemetry (uint32_t corner);
static void           rt_telemetry_ack (uint32_t corner);
static void           rt_tap (uint32_t corner);
static void           rt_mole_on (uint32_t corner);
static void           rt_feedback (uint32_t corner);
static void           rt_game_control (uint32_t corner);
static void           rt_wall_picture (uint32_t corner);
static void           rt_ack (uint32_t corner);
static bool           lengths_game (void);
static bool           reject_encode (void);
static bool           reject_decode_range (void);
static bool           reject_length_type (void);
static bool           reject_decode_game (void);
static bool           reject_decode_grid (void);
static uint32_t       game_encode (proto_type_t type);
static bool           game_decode (proto_type_t type, uint32_t len);

/**
 * @brief  Boundary set round trip for every message row.
 * @return true when there are 0 mismatches.
 */
bool test_messages_boundary (void)
{
    g_mismatches = 0u;

    for (uint32_t corner = 0u; corner < CORNERS; corner++)
    {
        rt_register(corner);
        rt_heartbeat(corner);
        rt_session_config(corner);
        rt_manifest(corner);
        rt_asset_chunk(corner);
        rt_chunk_ack(corner);
        rt_nack(corner);
        rt_busy(corner);
        rt_asset_ready(corner);
        rt_sync_ready(corner);
        rt_beacon(corner);
        rt_start_at(corner);
        rt_resume_at(corner);
        rt_commands(corner);
        rt_status(corner);
        rt_telemetry(corner);
        rt_telemetry_ack(corner);
        rt_tap(corner);
        rt_mole_on(corner);
        rt_feedback(corner);
        rt_game_control(corner);
        rt_wall_picture(corner);
        rt_ack(corner);
    }

    /* 24 rows (PAUSE and STOP in rt_commands) in 2 corners. */
    printf("    %" PRIu32 " cases (24 message rows x 2 corners), %" PRIu32
           " mismatches\n",
           (uint32_t)PROTO_TYPE_COUNT * CORNERS, g_mismatches); /* 24. */

    return test_main_check(0u == g_mismatches, __LINE__);
}

/**
 * @brief  Each encoder returns the length in layout_table.md section 4.
 * @return true when every length matches.
 */
bool test_messages_lengths (void)
{
    bool                         b_ok  = true;
    proto_header_t const         hdr   = header_at(CORNER_MIN);
    proto_register_t const       reg   = {hdr, 1u, 0u, 0u};
    proto_heartbeat_t const      beat  = {hdr, 1u};
    proto_session_config_t const cfg   = {hdr, 1u, 1u, 0u, {0u}, 1u, {0u}};
    proto_manifest_t const       man   = {hdr, 2u, 1u, 0u};
    proto_asset_chunk_t const    chunk = {hdr, 0u, 1u, g_payload};
    proto_chunk_ack_t const      cack  = {hdr, 1u, 0u};
    proto_nack_t const           nack  = {hdr, 1u, 0u, 0u};
    proto_busy_t const           busy  = {hdr, 1u, 0u};
    proto_asset_ready_t const    ready = {hdr, 1u, 0u};
    proto_sync_ready_t const     sync  = {hdr, 1u};
    proto_beacon_t const         bcn   = {hdr, 0u};
    proto_start_at_t const       start = {hdr, 0u};
    proto_resume_at_t const      res   = {hdr, 0u, 0u};
    proto_command_t const        cmd   = {hdr};
    proto_status_t const         stat  = {hdr, 0u, 0u, 0u, 0u,
                                          0u, 0u, 0u, 0u, 0u, 0u, 0u};
    proto_telemetry_t const      tel   = {hdr, 1u, 0u, 0u, 0u};
    proto_telemetry_ack_t const  tack  = {hdr, 0u};

    b_ok = (test_main_check(LEN_REGISTER ==
                                proto_encode_register(&reg, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_HEARTBEAT ==
                                proto_encode_heartbeat(&beat, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_SESSION_CONFIG == proto_encode_session_config(
                                                      &cfg, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_MANIFEST ==
                                proto_encode_manifest(&man, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok =
        (test_main_check((LEN_CHUNK_HEAD + 1u) ==
                             proto_encode_asset_chunk(&chunk, g_buf, BUF_BYTES),
                         __LINE__)) &&
        b_ok;
    b_ok = (test_main_check(LEN_CHUNK_ACK ==
                                proto_encode_chunk_ack(&cack, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok =
        (test_main_check(LEN_NACK == proto_encode_nack(&nack, g_buf, BUF_BYTES),
                         __LINE__)) &&
        b_ok;
    b_ok =
        (test_main_check(LEN_BUSY == proto_encode_busy(&busy, g_buf, BUF_BYTES),
                         __LINE__)) &&
        b_ok;
    b_ok = (test_main_check(LEN_ASSET_READY == proto_encode_asset_ready(
                                                   &ready, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_SYNC_READY == proto_encode_sync_ready(
                                                  &sync, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_BEACON ==
                                proto_encode_beacon(&bcn, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_START_AT ==
                                proto_encode_start_at(&start, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_RESUME_AT ==
                                proto_encode_resume_at(&res, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_COMMAND ==
                                proto_encode_pause(&cmd, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_COMMAND ==
                                proto_encode_stop(&cmd, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_STATUS ==
                                proto_encode_status(&stat, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_TELEMETRY ==
                                proto_encode_telemetry(&tel, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_TELEMETRY_ACK == proto_encode_telemetry_ack(
                                                     &tack, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (lengths_game()) && b_ok;
    printf("    24 encoders return the layout_table.md lengths\n");

    return b_ok;
}

/**
 * @brief  Lengths of the six whack-a-mole rows (re-plan step 3).
 * @return true when every length matches.
 */
static bool lengths_game (void)
{
    bool                       b_ok = true;
    proto_header_t const       hdr  = header_at(CORNER_MIN);
    proto_tap_t const          tap  = {hdr, 1u, 0u, 0u};
    proto_mole_on_t const      mole = {hdr, 1u, 0u, 1u, 0u};
    proto_feedback_msg_t const feed = {hdr, 1u, 0u};
    proto_game_control_t const ctrl = {hdr, 0u, 0u, 0u, 0u};
    proto_wall_picture_t const mask = {hdr, 1u, 0u, 0u, 0u, 0u, 0u, 0u, 0u};
    proto_ack_t const          ack  = {hdr, 0u, 0u};

    b_ok = (test_main_check(LEN_TAP == proto_encode_tap(&tap, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_MOLE_ON ==
                                proto_encode_mole_on(&mole, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_FEEDBACK ==
                                proto_encode_feedback(&feed, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_GAME_CONTROL == proto_encode_game_control(
                                                    &ctrl, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_WALL_PICTURE == proto_encode_wall_picture(
                                                  &mask, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(LEN_ACK == proto_encode_ack(&ack, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Out-of-range fields, wrong types, wrong lengths and short buffers
 *         are refused by encoders and decoders.
 * @return true when every refusal happened.
 */
bool test_messages_reject (void)
{
    bool const b_encode = reject_encode();
    bool const b_range  = reject_decode_range();
    bool const b_length = reject_length_type();
    bool const b_game   = reject_decode_game();
    bool const b_grid   = reject_decode_grid();

    printf("    15 out-of-range encodes, 8 out-of-range decodes, wrong type, "
           "wrong lengths and a short buffer refused\n");
    printf("    6 game rows: 1 byte short, node_id 10 to 15, and node_id 0 "
           "where a tile is required, refused; rows 0 and cols 0 refused\n");

    return b_encode && b_range && b_length && b_game && b_grid;
}

/**
 * @brief  Decoders refuse each game row one byte short, and with node_id 10
 *         to 15 written into its bits. node_id 0 is refused where a tile is
 *         required (TAP, MOLE_ON, FEEDBACK, WALL_PICTURE) and accepted where
 *         it means the PC-Pico link (GAME_CONTROL, ACK).
 * @return true when every refusal (and acceptance) happened.
 */
static bool reject_decode_game (void)
{
    bool b_ok = true;

    for (uint32_t row = 0u; row < GAME_ROWS; row++)
    {
        proto_type_t const type = g_game_rows[row];
        bool const         b_pc_ok =
            (PROTO_TYPE_GAME_CONTROL == type) || (PROTO_TYPE_ACK == type);
        uint32_t const len = game_encode(type);

        /* Valid as encoded, refused one byte short. */
        b_ok = (test_main_check((0u != len) && (game_decode(type, len)),
                                __LINE__)) &&
               b_ok;
        b_ok =
            (test_main_check(!game_decode(type, len - 1u), __LINE__)) && b_ok;

        for (uint32_t node = 0u; node <= NODE_FIELD_MAX; node++)
        {
            if ((0u == node) || (node > PROTO_NODE_MAX))
            {
                bool const b_expect = (0u == node) && (b_pc_ok);

                (void)game_encode(type); /* A fresh valid message. */
                proto_put_bits(g_buf, PROTO_HEADER_BITS, PROTO_NODE_BITS, node);
                b_ok = (test_main_check(b_expect == game_decode(type, len),
                                        __LINE__)) &&
                       b_ok;
            }
        }
    }

    return b_ok;
}

/**
 * @brief  The SESSION_CONFIG decoder refuses rows 0 and cols 0 written into
 *         the bits of a valid 1 x 1 message.
 * @return true when both refusals happened.
 */
static bool reject_decode_grid (void)
{
    bool                         b_ok = true;
    proto_header_t const         hdr  = header_at(CORNER_MIN);
    proto_session_config_t const cfg  = {hdr, 1u, 1u, 0u, {0u}, 1u, {0u}};
    proto_session_config_t       cfg_out;

    for (uint32_t field = 0u; field < 2u; field++)
    {
        /* field 0 is rows (bit 29), field 1 is cols (bit 33). */
        (void)proto_encode_session_config(&cfg, g_buf, BUF_BYTES); /* Valid. */
        proto_put_bits(g_buf, PROTO_HEADER_BITS + (field * PROTO_GRID_BITS),
                       PROTO_GRID_BITS, 0u);
        b_ok = (test_main_check(!proto_decode_session_config(
                                    g_buf, LEN_SESSION_CONFIG, &cfg_out),
                                __LINE__)) &&
               b_ok;
    }

    return b_ok;
}

/**
 * @brief  Encode a valid message of one game row into g_buf.
 * @param  type  One of g_game_rows.
 * @return Its length in bytes, 0 for another type.
 */
static uint32_t game_encode (proto_type_t type)
{
    proto_header_t const hdr = header_at(CORNER_MIN);
    uint32_t             len = 0u;

    if (PROTO_TYPE_TAP == type)
    {
        proto_tap_t const msg = {hdr, 5u, 1u, 1u};

        len = proto_encode_tap(&msg, g_buf, BUF_BYTES);
    }
    else if (PROTO_TYPE_MOLE_ON == type)
    {
        proto_mole_on_t const msg = {hdr, 3u, 1u, 1u, 0u};

        len = proto_encode_mole_on(&msg, g_buf, BUF_BYTES);
    }
    else if (PROTO_TYPE_FEEDBACK == type)
    {
        proto_feedback_msg_t const msg = {hdr, 6u, 1u};

        len = proto_encode_feedback(&msg, g_buf, BUF_BYTES);
    }
    else if (PROTO_TYPE_GAME_CONTROL == type)
    {
        proto_game_control_t const msg = {hdr, 2u, 2u, 0u, 0u};

        len = proto_encode_game_control(&msg, g_buf, BUF_BYTES);
    }
    else if (PROTO_TYPE_WALL_PICTURE == type)
    {
        proto_wall_picture_t const msg = {hdr, 7u, 0u, 0u, 0u, 0u, 0u, 0u, 1u};

        len = proto_encode_wall_picture(&msg, g_buf, BUF_BYTES);
    }
    else if (PROTO_TYPE_ACK == type)
    {
        proto_ack_t const msg = {hdr, 4u, 1u};

        len = proto_encode_ack(&msg, g_buf, BUF_BYTES);
    }
    else
    {
        len = 0u; /* Not a game row. */
    }

    return len;
}

/**
 * @brief  Decode g_buf as one game row.
 * @param  type  One of g_game_rows.
 * @param  len   Bytes to decode.
 * @return The decoder's result; false for another type.
 */
static bool game_decode (proto_type_t type, uint32_t len)
{
    bool b_ok = false;

    if (PROTO_TYPE_TAP == type)
    {
        proto_tap_t out;

        b_ok = proto_decode_tap(g_buf, len, &out);
    }
    else if (PROTO_TYPE_MOLE_ON == type)
    {
        proto_mole_on_t out;

        b_ok = proto_decode_mole_on(g_buf, len, &out);
    }
    else if (PROTO_TYPE_FEEDBACK == type)
    {
        proto_feedback_msg_t out;

        b_ok = proto_decode_feedback(g_buf, len, &out);
    }
    else if (PROTO_TYPE_GAME_CONTROL == type)
    {
        proto_game_control_t out;

        b_ok = proto_decode_game_control(g_buf, len, &out);
    }
    else if (PROTO_TYPE_WALL_PICTURE == type)
    {
        proto_wall_picture_t out;

        b_ok = proto_decode_wall_picture(g_buf, len, &out);
    }
    else if (PROTO_TYPE_ACK == type)
    {
        proto_ack_t out;

        b_ok = proto_decode_ack(g_buf, len, &out);
    }
    else
    {
        b_ok = false; /* Not a game row. */
    }

    return b_ok;
}

/**
 * @brief  Encoders refuse out-of-range fields.
 * @return true when every refusal happened.
 */
static bool reject_encode (void)
{
    bool                   b_ok  = true;
    proto_header_t const   hdr   = header_at(CORNER_MIN);
    proto_heartbeat_t      beat  = {hdr, 0u};
    proto_manifest_t       man   = {hdr, 2u, 0u, 0u};
    proto_status_t         stat  = {hdr, BITMAP_MAX + 1u, 0u, 0u, 0u,
                                    0u, 0u, 0u, 0u, 0u, 0u, 0u};
    proto_status_t         hi    = {hdr, 0u, 0u, 0u, SCORE_OVER,
                                    0u, 0u, 0u, 0u, 0u, 0u, 0u};
    proto_status_t         lo    = {hdr, 0u, 0u, 0u, SCORE_UNDER,
                                    0u, 0u, 0u, 0u, 0u, 0u, 0u};
    proto_telemetry_t      tel   = {hdr, 1u, 0u, 0u, PROTO_DISPLAY_COUNT};
    proto_asset_chunk_t    chunk = {hdr, PROTO_CHUNK_INDEX_MAX + 1u, 1u,
                                    g_payload};
    proto_session_config_t cfg   = {hdr, 2u, 5u, 0u, {0u}, 1u, {0u}};
    proto_mole_on_t        mole  = {hdr, 1u, 0u, 0u, 0u};
    /* Command 8 is past the last (TAP_TEST_OFF); 4 is ARM since 11 Nov. */
    proto_game_control_t   ctrl  = {hdr, 1u, (uint8_t)PROTO_GAME_CMD_COUNT,
                                    0u, 0u};
    proto_feedback_msg_t   feed  = {hdr, 1u, 3u}; /* 3 = FEEDBACK_COUNT. */
    proto_ack_t            ack   = {hdr, PROTO_NODE_MAX + 1u, 0u};
    proto_tap_t            tap   = {hdr, 0u, 0u, 0u};

    /* Encoders refuse out-of-range fields. */
    b_ok =
        (test_main_check(0u == proto_encode_heartbeat(&beat, g_buf, BUF_BYTES),
                         __LINE__)) &&
        b_ok;
    beat.node_id = PROTO_NODE_MAX + 1u;
    b_ok =
        (test_main_check(0u == proto_encode_heartbeat(&beat, g_buf, BUF_BYTES),
                         __LINE__)) &&
        b_ok;
    b_ok = (test_main_check(0u == proto_encode_manifest(&man, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(0u == proto_encode_status(&stat, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok =
        (test_main_check(0u == proto_encode_telemetry(&tel, g_buf, BUF_BYTES),
                         __LINE__)) &&
        b_ok;
    b_ok = (test_main_check(
               0u == proto_encode_asset_chunk(&chunk, g_buf, BUF_BYTES),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               0u == proto_encode_session_config(&cfg, g_buf, BUF_BYTES),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(0u == proto_encode_status(&hi, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(0u == proto_encode_status(&lo, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    /* up_ms 0 is valid since 11 Nov (the start sprite); kind 5 is not. */
    mole.kind = (uint8_t)PROTO_TARGET_COUNT;
    b_ok = (test_main_check(0u == proto_encode_mole_on(&mole, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    mole.kind  = 0u;
    mole.up_ms = PROTO_UP_MS_MAX + 1u;
    b_ok = (test_main_check(0u == proto_encode_mole_on(&mole, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               0u == proto_encode_game_control(&ctrl, g_buf, BUF_BYTES),
               __LINE__)) &&
           b_ok;
    b_ok =
        (test_main_check(0u == proto_encode_feedback(&feed, g_buf, BUF_BYTES),
                         __LINE__)) &&
        b_ok;
    b_ok = (test_main_check(0u == proto_encode_ack(&ack, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(0u == proto_encode_tap(&tap, g_buf, BUF_BYTES),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Decoders refuse out-of-range fields written straight into the
 *         bits of a valid message.
 * @return true when every refusal happened.
 */
static bool reject_decode_range (void)
{
    bool                         b_ok  = true;
    proto_header_t const         hdr   = header_at(CORNER_MIN);
    proto_heartbeat_t const      beat  = {hdr, 1u};
    proto_manifest_t const       man   = {hdr, 2u, 1u, 0u};
    proto_status_t const         stat  = {hdr, 0u, 0u, 0u, 0u,
                                          0u, 0u, 0u, 0u, 0u, 0u, 0u};
    proto_telemetry_t const      tel   = {hdr, 1u, 0u, 0u, 0u};
    proto_asset_chunk_t const    chunk = {hdr, 0u, 1u, g_payload};
    proto_session_config_t const cfg   = {hdr, 1u, 1u, 0u, {0u}, 1u, {0u}};
    proto_mole_on_t const        mole  = {hdr, 1u, 0u, 1u, 0u};
    proto_heartbeat_t            beat_out;
    proto_manifest_t             man_out;
    proto_status_t               stat_out;
    proto_telemetry_t            tel_out;
    proto_asset_chunk_t          chunk_out;
    proto_session_config_t       cfg_out;
    proto_mole_on_t              mole_out;

    /* Decoders refuse out-of-range fields written straight into the bits:
     * Node ID 0, frame_count 0, coordinator state 13, display state 7,
     * chunk index 113, rows x cols > 9, score 100 and target kind 5. */
    (void)proto_encode_heartbeat(&beat, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_HEADER_BITS, PROTO_NODE_BITS, 0u);
    b_ok = (test_main_check(
               !proto_decode_heartbeat(g_buf, LEN_HEARTBEAT, &beat_out),
               __LINE__)) &&
           b_ok;

    (void)proto_encode_manifest(&man, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_HEADER_BITS + PROTO_FORMAT_BITS,
                   PROTO_FRAME_BITS, 0u);
    b_ok =
        (test_main_check(!proto_decode_manifest(g_buf, LEN_MANIFEST, &man_out),
                         __LINE__)) &&
        b_ok;

    (void)proto_encode_status(&stat, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_HEADER_BITS + (2u * PROTO_BITMAP_BITS),
                   PROTO_COORD_STATE_BITS, PROTO_COORD_COUNT);
    b_ok = (test_main_check(!proto_decode_status(g_buf, LEN_STATUS, &stat_out),
                            __LINE__)) &&
           b_ok;

    (void)proto_encode_telemetry(&tel, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_TELEMETRY_BITS - PROTO_DISPLAY_STATE_BITS,
                   PROTO_DISPLAY_STATE_BITS, PROTO_DISPLAY_COUNT);
    b_ok = (test_main_check(
               !proto_decode_telemetry(g_buf, LEN_TELEMETRY, &tel_out),
               __LINE__)) &&
           b_ok;

    (void)proto_encode_asset_chunk(&chunk, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_HEADER_BITS, PROTO_CHUNK_BITS,
                   PROTO_CHUNK_INDEX_MAX + 1u);
    b_ok = (test_main_check(!proto_decode_asset_chunk(
                                g_buf, LEN_CHUNK_HEAD + 1u, &chunk_out),
                            __LINE__)) &&
           b_ok;

    (void)proto_encode_session_config(&cfg, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_HEADER_BITS, PROTO_GRID_BITS, 2u);
    proto_put_bits(g_buf, PROTO_HEADER_BITS + PROTO_GRID_BITS, PROTO_GRID_BITS,
                   5u);
    b_ok = (test_main_check(!proto_decode_session_config(
                                g_buf, LEN_SESSION_CONFIG, &cfg_out),
                            __LINE__)) &&
           b_ok;

    (void)proto_encode_status(&stat, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf,
                   PROTO_HEADER_BITS + (2u * PROTO_BITMAP_BITS) +
                       PROTO_COORD_STATE_BITS,
                   PROTO_SCORE_BITS, SCORE_OVER);
    b_ok = (test_main_check(!proto_decode_status(g_buf, LEN_STATUS, &stat_out),
                            __LINE__)) &&
           b_ok;

    (void)proto_encode_mole_on(&mole, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_MOLE_ON_BITS - PROTO_TARGET_KIND_BITS,
                   PROTO_TARGET_KIND_BITS, PROTO_TARGET_COUNT);
    b_ok =
        (test_main_check(!proto_decode_mole_on(g_buf, LEN_MOLE_ON, &mole_out),
                         __LINE__)) &&
        b_ok;

    return b_ok;
}

/**
 * @brief  Wrong type, wrong length, a chunk too long or empty, and a short
 *         output buffer are refused.
 * @return true when every refusal happened.
 */
static bool reject_length_type (void)
{
    bool                      b_ok  = true;
    proto_header_t const      hdr   = header_at(CORNER_MIN);
    proto_heartbeat_t const   beat  = {hdr, 1u};
    proto_asset_chunk_t const chunk = {hdr, 0u, PROTO_CHUNK_PAYLOAD_MAX,
                                       g_payload};
    proto_heartbeat_t         beat_out;
    proto_sync_ready_t        sync_out;
    proto_asset_chunk_t       chunk_out;

    (void)proto_encode_heartbeat(&beat, g_buf, BUF_BYTES); /* Valid. */
    b_ok = (test_main_check(
               !proto_decode_sync_ready(g_buf, LEN_SYNC_READY, &sync_out),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               !proto_decode_heartbeat(g_buf, LEN_HEARTBEAT + 1u, &beat_out),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               0u == proto_encode_heartbeat(&beat, g_buf, LEN_HEARTBEAT - 1u),
               __LINE__)) &&
           b_ok;

    /* A valid full chunk in g_buf, so only the length decides. */
    (void)proto_encode_asset_chunk(&chunk, g_buf, BUF_BYTES); /* Valid. */
    b_ok = (test_main_check(proto_decode_asset_chunk(
                                g_buf, PROTO_ASSET_CHUNK_MAX_BYTES, &chunk_out),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               !proto_decode_asset_chunk(
                   g_buf, PROTO_ASSET_CHUNK_MAX_BYTES + 1u, &chunk_out),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               !proto_decode_asset_chunk(g_buf, LEN_CHUNK_HEAD, &chunk_out),
               __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Byte-aligned little-endian helpers.
 * @return true when the bytes and values match.
 */
bool test_messages_le (void)
{
    static uint8_t const ref[LE_BYTES] = {0x08u, 0x07u, 0x06u, 0x05u,
                                          0x04u, 0x03u, 0x02u, 0x01u};
    uint8_t              buf[LE_BYTES];
    bool                 b_ok = true;

    proto_put_u64_le(buf, LE_U64);

    for (uint32_t idx = 0u; idx < LE_BYTES; idx++)
    {
        b_ok = (test_main_check(ref[idx] == buf[idx], __LINE__)) && b_ok;
    }

    b_ok = (test_main_check(LE_U64 == proto_get_u64_le(buf), __LINE__)) && b_ok;
    proto_put_u32_le(buf, LE_U32);
    b_ok =
        (test_main_check((0x04u == buf[0]) && (0x01u == buf[3]), __LINE__)) &&
        b_ok;
    b_ok = (test_main_check(LE_U32 == proto_get_u32_le(buf), __LINE__)) && b_ok;
    proto_put_u16_le(buf, LE_U16);
    b_ok =
        (test_main_check((0x02u == buf[0]) && (0x01u == buf[1]), __LINE__)) &&
        b_ok;
    b_ok = (test_main_check(LE_U16 == proto_get_u16_le(buf), __LINE__)) && b_ok;
    printf("    u16, u32, u64: low byte first, values round-trip\n");

    return b_ok;
}

/**
 * @brief  Header at a corner: session_id and seq at 0 or their maximum.
 * @param  corner  0 or 1.
 * @return The header (type is set by each encoder).
 */
static proto_header_t header_at (uint32_t corner)
{
    proto_header_t header = {0u, 0u, 0u};

    if (CORNER_MIN != corner)
    {
        header.session_id = UINT8_MAX;
        header.seq        = UINT16_MAX;
    }

    return header;
}

/**
 * @brief  Compare session_id and seq.
 * @param  p_left   Sent header.
 * @param  p_right  Decoded header.
 * @return true when equal.
 */
static bool same_header (proto_header_t const * p_left,
                         proto_header_t const * p_right)
{
    return (p_left->session_id == p_right->session_id) &&
           (p_left->seq == p_right->seq);
}

/**
 * @brief  Node ID at a corner: 1 or 9.
 * @param  corner  0 or 1.
 * @return The Node ID.
 */
static uint8_t node_at (uint32_t corner)
{
    uint8_t node_id = 1u;

    if (CORNER_MIN != corner)
    {
        node_id = PROTO_NODE_MAX;
    }

    return node_id;
}

/**
 * @brief  Count one round trip.
 * @param  b_same  true when the decoded message equals the sent one.
 */
static void tally (bool b_same)
{
    (void)test_main_check(b_same, __LINE__); /* Prints the failing line. */

    if (!b_same)
    {
        g_mismatches++;
    }
}

/**
 * @brief  REGISTER round trip.
 * @param  corner  0 or 1.
 */
static void rt_register (uint32_t corner)
{
    bool const             b_max = (CORNER_MIN != corner);
    proto_register_t const msg   = {header_at(corner), node_at(corner),
                                  b_max ? UINT8_MAX : 0u,
                                  b_max ? UINT32_MAX : 0u};
    proto_register_t       out;
    uint32_t const         len = proto_encode_register(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_register(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) &&
          (msg.cached_session_id == out.cached_session_id) &&
          (msg.cached_crc32 == out.cached_crc32));
}

/**
 * @brief  HEARTBEAT round trip.
 * @param  corner  0 or 1.
 */
static void rt_heartbeat (uint32_t corner)
{
    proto_heartbeat_t const msg = {header_at(corner), node_at(corner)};
    proto_heartbeat_t       out;
    uint32_t const len = proto_encode_heartbeat(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_heartbeat(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id));
}

/**
 * @brief  SESSION_CONFIG round trip: every map position 0 or 9.
 * @param  corner  0 or 1.
 */
static void rt_session_config (uint32_t corner)
{
    bool const             b_max = (CORNER_MIN != corner);
    proto_session_config_t msg   = {
        header_at(corner), 1u, 1u, 0u, {0u}, 1u, {0u}};
    proto_session_config_t out;
    bool                   b_same;

    /* rows 9 is the largest; 9 x 9 would break rows x cols <= 9, so the
     * max corner is a 9 x 1 column. */
    msg.rows   = b_max ? (uint8_t)PROTO_GRID_MAX : 1u;
    msg.policy = b_max ? (uint8_t)PROTO_POLICY_PAUSE : 0u; /* Code 1. */

    msg.frame_count = b_max ? (uint16_t)PROTO_FRAME_MAX : 1u; /* 1023. */

    for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
    {
        msg.node_map[slot] = b_max ? PROTO_NODE_MAX : 0u;
        msg.crc32[slot]    = b_max ? UINT32_MAX : 0u;
    }

    uint32_t const len = proto_encode_session_config(&msg, g_buf, BUF_BYTES);

    b_same = (proto_decode_session_config(g_buf, len, &out)) &&
             (same_header(&msg.header, &out.header)) &&
             (msg.rows == out.rows) && (msg.cols == out.cols) &&
             (msg.policy == out.policy);

    b_same = b_same && (msg.frame_count == out.frame_count);

    for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
    {
        b_same = b_same && (msg.node_map[slot] == out.node_map[slot]) &&
                 (msg.crc32[slot] == out.crc32[slot]);
    }

    tally(b_same);
}

/**
 * @brief  MANIFEST round trip: frame_count 1 and 1023.
 * @param  corner  0 or 1.
 */
static void rt_manifest (uint32_t corner)
{
    bool const             b_max = (CORNER_MIN != corner);
    proto_manifest_t const msg   = {
        header_at(corner),
        b_max ? (uint8_t)PROTO_FORMAT_5BIT : 0u, /* Code 3. */
        b_max ? PROTO_FRAME_MAX : 1u, b_max ? UINT32_MAX : 0u};
    proto_manifest_t out;
    uint32_t const   len = proto_encode_manifest(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_manifest(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.format_version == out.format_version) &&
          (msg.frame_count == out.frame_count) && (msg.crc32 == out.crc32));
}

/**
 * @brief  ASSET_CHUNK round trip: index 0 with 1 byte, index 112 with
 *         1,024 bytes.
 * @param  corner  0 or 1.
 */
static void rt_asset_chunk (uint32_t corner)
{
    bool const b_max = (CORNER_MIN != corner);

    for (uint32_t idx = 0u; idx < PROTO_CHUNK_PAYLOAD_MAX; idx++)
    {
        /* idx & 0xFF fits uint8_t. */
        g_payload[idx] = (uint8_t)((idx ^ CHUNK_PATTERN) & UINT8_MAX);
    }

    proto_asset_chunk_t const msg = {
        header_at(corner), b_max ? PROTO_CHUNK_INDEX_MAX : 0u,
        b_max ? PROTO_CHUNK_PAYLOAD_MAX : 1u, g_payload};
    proto_asset_chunk_t out;
    uint32_t const      len = proto_encode_asset_chunk(&msg, g_buf, BUF_BYTES);
    bool                b_same = (proto_decode_asset_chunk(g_buf, len, &out)) &&
                  (same_header(&msg.header, &out.header)) &&
                  (msg.chunk_index == out.chunk_index) &&
                  (msg.payload_len == out.payload_len);

    for (uint32_t idx = 0u; b_same && (idx < msg.payload_len); idx++)
    {
        b_same = (g_payload[idx] == out.p_payload[idx]);
    }

    tally(b_same);
}

/**
 * @brief  CHUNK_ACK round trip.
 * @param  corner  0 or 1.
 */
static void rt_chunk_ack (uint32_t corner)
{
    bool const              b_max = (CORNER_MIN != corner);
    proto_chunk_ack_t const msg   = {header_at(corner), node_at(corner),
                                   b_max ? PROTO_CHUNK_INDEX_MAX : 0u};
    proto_chunk_ack_t       out;
    uint32_t const len = proto_encode_chunk_ack(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_chunk_ack(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) && (msg.chunk_index == out.chunk_index));
}

/**
 * @brief  NACK round trip: reason 0 and 3.
 * @param  corner  0 or 1.
 */
static void rt_nack (uint32_t corner)
{
    bool const b_max = (CORNER_MIN != corner);
    /* PROTO_NACK_UPLOAD_QUEUE_FULL is code 3, which fits uint8_t. */
    uint8_t const      reason = (uint8_t)PROTO_NACK_UPLOAD_QUEUE_FULL;
    proto_nack_t const msg    = {header_at(corner), node_at(corner),
                              b_max ? reason : 0u, b_max ? UINT16_MAX : 0u};
    proto_nack_t       out;
    uint32_t const     len = proto_encode_nack(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_nack(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) && (msg.reason == out.reason) &&
          (msg.ref_seq == out.ref_seq));
}

/**
 * @brief  BUSY round trip.
 * @param  corner  0 or 1.
 */
static void rt_busy (uint32_t corner)
{
    proto_busy_t const msg = {header_at(corner), node_at(corner),
                              (CORNER_MIN != corner) ? UINT16_MAX : 0u};
    proto_busy_t       out;
    uint32_t const     len = proto_encode_busy(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_busy(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) && (msg.ref_seq == out.ref_seq));
}

/**
 * @brief  ASSET_READY round trip.
 * @param  corner  0 or 1.
 */
static void rt_asset_ready (uint32_t corner)
{
    proto_asset_ready_t const msg = {header_at(corner), node_at(corner),
                                     (CORNER_MIN != corner) ? UINT32_MAX : 0u};
    proto_asset_ready_t       out;
    uint32_t const len = proto_encode_asset_ready(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_asset_ready(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) && (msg.crc32 == out.crc32));
}

/**
 * @brief  SYNC_READY round trip.
 * @param  corner  0 or 1.
 */
static void rt_sync_ready (uint32_t corner)
{
    proto_sync_ready_t const msg = {header_at(corner), node_at(corner)};
    proto_sync_ready_t       out;
    uint32_t const len = proto_encode_sync_ready(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_sync_ready(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id));
}

/**
 * @brief  BEACON round trip: t_p 0 and 2^64 - 1.
 * @param  corner  0 or 1.
 */
static void rt_beacon (uint32_t corner)
{
    proto_beacon_t const msg = {header_at(corner),
                                (CORNER_MIN != corner) ? UINT64_MAX : 0u};
    proto_beacon_t       out;
    uint32_t const       len = proto_encode_beacon(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_beacon(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) && (msg.t_p == out.t_p));
}

/**
 * @brief  START_AT round trip: t_start 0 and 2^64 - 1.
 * @param  corner  0 or 1.
 */
static void rt_start_at (uint32_t corner)
{
    proto_start_at_t const msg = {header_at(corner),
                                  (CORNER_MIN != corner) ? UINT64_MAX : 0u};
    proto_start_at_t       out;
    uint32_t const         len = proto_encode_start_at(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_start_at(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.t_start == out.t_start));
}

/**
 * @brief  RESUME_AT round trip: frame index 0 and 1023.
 * @param  corner  0 or 1.
 */
static void rt_resume_at (uint32_t corner)
{
    bool const              b_max = (CORNER_MIN != corner);
    proto_resume_at_t const msg   = {header_at(corner), b_max ? UINT64_MAX : 0u,
                                   b_max ? PROTO_FRAME_MAX : 0u};
    proto_resume_at_t       out;
    uint32_t const len = proto_encode_resume_at(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_resume_at(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.t_resume == out.t_resume) &&
          (msg.frame_index == out.frame_index));
}

/**
 * @brief  PAUSE and STOP round trips; each is refused as the other.
 * @param  corner  0 or 1.
 */
static void rt_commands (uint32_t corner)
{
    proto_command_t const msg = {header_at(corner)};
    proto_command_t       out;
    uint32_t              len = proto_encode_pause(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_pause(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (!proto_decode_stop(g_buf, len, &out)));

    len = proto_encode_stop(&msg, g_buf, BUF_BYTES);
    tally((proto_decode_stop(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (!proto_decode_pause(g_buf, len, &out)));
}

/**
 * @brief  STATUS round trip: bitmaps 0 and 0x1FF, state 0 and 11.
 * @param  corner  0 or 1.
 */
static void rt_status (uint32_t corner)
{
    bool const           b_max = (CORNER_MIN != corner);
    proto_status_t const msg   = {
        header_at(corner), b_max ? BITMAP_MAX : 0u, b_max ? BITMAP_MAX : 0u,
        b_max ? (uint8_t)PROTO_COORD_COORDINATING_GAME : 0u, /* Code 12. */
        b_max ? (int8_t)PROTO_SCORE_MAX : (int8_t)-PROTO_SCORE_MAX,
        b_max ? (uint8_t)PROTO_ROUND_RESULTS : 0u,
        b_max ? (uint8_t)PROTO_LEVEL_HARD : 0u,
        b_max ? PROTO_COUNT_MAX : 0u, b_max ? PROTO_COUNT_MAX : 0u,
        b_max ? PROTO_COUNT_MAX : 0u, b_max ? PROTO_TIME_LEFT_MAX : 0u,
        b_max ? PROTO_REACTION_MAX : 0u};
    proto_status_t out;
    uint32_t const len = proto_encode_status(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_status(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.readiness == out.readiness) && (msg.online == out.online) &&
          (msg.state == out.state) && (msg.score == out.score) &&
          (msg.round_state == out.round_state) && (msg.level == out.level) &&
          (msg.missed == out.missed) && (msg.wrong == out.wrong) &&
          (msg.shown == out.shown) && (msg.time_left_s == out.time_left_s) &&
          (msg.reaction_ms == out.reaction_ms));
}

/**
 * @brief  TELEMETRY round trip: fault word 0 and 0xFFFFFFFF.
 * @param  corner  0 or 1.
 */
static void rt_telemetry (uint32_t corner)
{
    bool const              b_max = (CORNER_MIN != corner);
    proto_telemetry_t const msg   = {
        header_at(corner), node_at(corner), b_max ? UINT32_MAX : 0u,
        b_max ? (uint8_t)PROTO_ORIENT_270 : 0u,     /* Code 3. */
        b_max ? (uint8_t)PROTO_DISPLAY_FAULT : 0u}; /* Code 6. */
    proto_telemetry_t out;
    uint32_t const    len = proto_encode_telemetry(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_telemetry(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) && (msg.fault_word == out.fault_word) &&
          (msg.orientation == out.orientation) &&
          (msg.display_state == out.display_state));
}

/**
 * @brief  TELEMETRY_ACK round trip.
 * @param  corner  0 or 1.
 */
static void rt_telemetry_ack (uint32_t corner)
{
    proto_telemetry_ack_t const msg = {
        header_at(corner), (CORNER_MIN != corner) ? UINT16_MAX : 0u};
    proto_telemetry_ack_t out;
    uint32_t const len = proto_encode_telemetry_ack(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_telemetry_ack(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.acked_seq == out.acked_seq));
}

/**
 * @brief  TAP round trip: node_id 1 or 9, peak and t_tap min or max.
 * @param  corner  0 or 1.
 */
static void rt_tap (uint32_t corner)
{
    bool const        b_max = (CORNER_MIN != corner);
    proto_tap_t const msg   = {header_at(corner), b_max ? PROTO_NODE_MAX : 1u,
                             b_max ? UINT16_MAX : 0u, b_max ? UINT64_MAX : 0u};
    proto_tap_t       out;
    uint32_t const    len = proto_encode_tap(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_tap(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) && (msg.peak == out.peak) &&
          (msg.t_tap == out.t_tap));
}

/**
 * @brief  MOLE_ON round trip: node_id 1 or 9, t_show 0 or max, up_ms 1 or
 *         PROTO_UP_MS_MAX.
 * @param  corner  0 or 1.
 */
static void rt_mole_on (uint32_t corner)
{
    bool const            b_max = (CORNER_MIN != corner);
    proto_mole_on_t const msg = {header_at(corner), b_max ? PROTO_NODE_MAX : 1u,
                                 b_max ? UINT64_MAX : 0u,
                                 b_max ? PROTO_UP_MS_MAX : 0u,
                                 /* Enum code 4: fits uint8_t. */
                                 b_max ? (uint8_t)PROTO_TARGET_DECOY : 0u};
    proto_mole_on_t       out;
    uint32_t const        len = proto_encode_mole_on(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_mole_on(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) && (msg.t_show == out.t_show) &&
          (msg.up_ms == out.up_ms) && (msg.kind == out.kind));
}

/**
 * @brief  FEEDBACK round trip: node_id 1 or 9, result 0 or 2 (the reserved
 *         decoy code, the largest valid).
 * @param  corner  0 or 1.
 */
static void rt_feedback (uint32_t corner)
{
    bool const b_max = (CORNER_MIN != corner);
    /* Result 2 is PROTO_FEEDBACK_DECOY, 0 is PROTO_FEEDBACK_MISS. */
    proto_feedback_msg_t const msg = {
        header_at(corner), b_max ? PROTO_NODE_MAX : 1u, b_max ? 2u : 0u};
    proto_feedback_msg_t out;
    uint32_t const       len = proto_encode_feedback(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_feedback(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) && (msg.result == out.result));
}

/**
 * @brief  GAME_CONTROL round trip: node_id 0 or 9, command 0 or 7, level 0
 *         or 2, threshold 0 or 250.
 * @param  corner  0 or 1.
 */
static void rt_game_control (uint32_t corner)
{
    bool const                 b_max = (CORNER_MIN != corner);
    proto_game_control_t const msg   = {
        header_at(corner), b_max ? PROTO_NODE_MAX : 0u,
        /* Enum codes 7 and 2: both fit uint8_t. */
        b_max ? (uint8_t)PROTO_GAME_TAP_TEST_OFF : 0u,
        b_max ? (uint8_t)PROTO_LEVEL_HARD : 0u,
        b_max ? PROTO_THRESHOLD_MAX : 0u};
    proto_game_control_t out;
    uint32_t const len = proto_encode_game_control(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_game_control(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) && (msg.command == out.command) &&
          (msg.level == out.level) && (msg.threshold == out.threshold));
}

/**
 * @brief  WALL_PICTURE round trip: every field at its smallest or largest.
 * @param  corner  0 or 1.
 */
static void rt_wall_picture (uint32_t corner)
{
    bool const               b_max = (CORNER_MIN != corner);
    proto_wall_picture_t const msg   = {
        header_at(corner),
        b_max ? PROTO_NODE_MAX : 1u,
        b_max ? 3u : 0u,
        /* Enum codes 2 and 7: both fit uint8_t. */
        b_max ? (uint8_t)PROTO_PIC_CLEAR : 0u,
        b_max ? (uint8_t)PROTO_COLOUR_CYAN : 0u,
        b_max ? 1u : 0u,
        b_max ? UINT64_MAX : 0u,
        b_max ? 8191u : 0u,
        b_max ? UINT64_MAX : 0u};
    proto_wall_picture_t       out;
    uint32_t const len = proto_encode_wall_picture(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_wall_picture(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) && (msg.slot == out.slot) &&
          (msg.effect == out.effect) && (msg.colour == out.colour) &&
          (msg.b_repeat == out.b_repeat) && (msg.t_show == out.t_show) &&
          (msg.dur_ms == out.dur_ms) && (msg.mask == out.mask));
}

/**
 * @brief  ACK round trip: node_id 0 or 9, acked_seq 0 or max.
 * @param  corner  0 or 1.
 */
static void rt_ack (uint32_t corner)
{
    bool const        b_max = (CORNER_MIN != corner);
    proto_ack_t const msg   = {header_at(corner), b_max ? PROTO_NODE_MAX : 0u,
                             b_max ? UINT16_MAX : 0u};
    proto_ack_t       out;
    uint32_t const    len = proto_encode_ack(&msg, g_buf, BUF_BYTES);

    tally((proto_decode_ack(g_buf, len, &out)) &&
          (same_header(&msg.header, &out.header)) &&
          (msg.node_id == out.node_id) && (msg.acked_seq == out.acked_seq));
}

/*** end of file ***/
