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
#include <stddef.h>
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

/** Encoded lengths copied from protocol/layout_table.md section 4, so a
 *  change of width in proto.h that the table does not follow fails here. */
#define LEN_REGISTER       10u
#define LEN_HEARTBEAT      5u
#define LEN_SESSION_CONFIG 9u
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
#define LEN_STATUS         7u
#define LEN_TELEMETRY      9u
#define LEN_TELEMETRY_ACK  6u

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
    }

    /* 18 rows (PAUSE and STOP in rt_commands) in 2 corners. */
    printf("    %" PRIu32 " cases (18 message rows x 2 corners), %" PRIu32
           " mismatches\n",
           (uint32_t)PROTO_TYPE_COUNT * CORNERS, g_mismatches); /* 18. */

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
    proto_session_config_t const cfg   = {hdr, 0u, 0u, {0u}};
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
    proto_status_t const         stat  = {hdr, 0u, 0u, 0u};
    proto_telemetry_t const      tel   = {hdr, 1u, 0u, 0u, 0u};
    proto_telemetry_ack_t const  tack  = {hdr, 0u};

    b_ok = test_main_check(LEN_REGISTER ==
                               proto_encode_register(&reg, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_HEARTBEAT ==
                               proto_encode_heartbeat(&beat, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_SESSION_CONFIG == proto_encode_session_config(
                                                     &cfg, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_MANIFEST ==
                               proto_encode_manifest(&man, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok =
        test_main_check((LEN_CHUNK_HEAD + 1u) ==
                            proto_encode_asset_chunk(&chunk, g_buf, BUF_BYTES),
                        __LINE__) &&
        b_ok;
    b_ok = test_main_check(LEN_CHUNK_ACK ==
                               proto_encode_chunk_ack(&cack, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok =
        test_main_check(LEN_NACK == proto_encode_nack(&nack, g_buf, BUF_BYTES),
                        __LINE__) &&
        b_ok;
    b_ok =
        test_main_check(LEN_BUSY == proto_encode_busy(&busy, g_buf, BUF_BYTES),
                        __LINE__) &&
        b_ok;
    b_ok = test_main_check(LEN_ASSET_READY == proto_encode_asset_ready(
                                                  &ready, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_SYNC_READY ==
                               proto_encode_sync_ready(&sync, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_BEACON ==
                               proto_encode_beacon(&bcn, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_START_AT ==
                               proto_encode_start_at(&start, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_RESUME_AT ==
                               proto_encode_resume_at(&res, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_COMMAND ==
                               proto_encode_pause(&cmd, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_COMMAND ==
                               proto_encode_stop(&cmd, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_STATUS ==
                               proto_encode_status(&stat, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_TELEMETRY ==
                               proto_encode_telemetry(&tel, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(LEN_TELEMETRY_ACK == proto_encode_telemetry_ack(
                                                    &tack, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    printf("    18 encoders return the layout_table.md lengths\n");

    return b_ok;
}

/**
 * @brief  Out-of-range fields, wrong types, wrong lengths and short buffers
 *         are refused by encoders and decoders.
 * @return true when every refusal happened.
 */
bool test_messages_reject (void)
{
    bool                   b_ok  = true;
    proto_header_t const   hdr   = header_at(CORNER_MIN);
    proto_heartbeat_t      beat  = {hdr, 0u};
    proto_manifest_t       man   = {hdr, 2u, 0u, 0u};
    proto_status_t         stat  = {hdr, BITMAP_MAX + 1u, 0u, 0u};
    proto_telemetry_t      tel   = {hdr, 1u, 0u, 0u, PROTO_DISPLAY_COUNT};
    proto_asset_chunk_t    chunk = {hdr, PROTO_CHUNK_INDEX_MAX + 1u, 1u,
                                    g_payload};
    proto_session_config_t cfg   = {hdr, PROTO_LAYOUT_COUNT, 0u, {0u}};
    proto_heartbeat_t      beat_out;
    proto_manifest_t       man_out;
    proto_status_t         stat_out;
    proto_telemetry_t      tel_out;
    proto_asset_chunk_t    chunk_out;
    proto_session_config_t cfg_out;
    proto_sync_ready_t     sync_out;

    /* Encoders refuse out-of-range fields. */
    b_ok =
        test_main_check(0u == proto_encode_heartbeat(&beat, g_buf, BUF_BYTES),
                        __LINE__) &&
        b_ok;
    beat.node_id = PROTO_NODE_MAX + 1u;
    b_ok =
        test_main_check(0u == proto_encode_heartbeat(&beat, g_buf, BUF_BYTES),
                        __LINE__) &&
        b_ok;
    b_ok = test_main_check(0u == proto_encode_manifest(&man, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(0u == proto_encode_status(&stat, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(0u == proto_encode_telemetry(&tel, g_buf, BUF_BYTES),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(
               0u == proto_encode_asset_chunk(&chunk, g_buf, BUF_BYTES),
               __LINE__) &&
           b_ok;
    b_ok = test_main_check(
               0u == proto_encode_session_config(&cfg, g_buf, BUF_BYTES),
               __LINE__) &&
           b_ok;

    /* Decoders refuse out-of-range fields written straight into the bits:
     * Node ID 0, frame_count 0, coordinator state 12, display state 7,
     * chunk index 96 and layout 3. */
    beat.node_id = 1u;
    (void)proto_encode_heartbeat(&beat, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_HEADER_BITS, PROTO_NODE_BITS, 0u);
    b_ok = test_main_check(
               !proto_decode_heartbeat(g_buf, LEN_HEARTBEAT, &beat_out),
               __LINE__) &&
           b_ok;

    man.frame_count = 1u;
    (void)proto_encode_manifest(&man, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_HEADER_BITS + PROTO_FORMAT_BITS,
                   PROTO_FRAME_BITS, 0u);
    b_ok =
        test_main_check(!proto_decode_manifest(g_buf, LEN_MANIFEST, &man_out),
                        __LINE__) &&
        b_ok;

    stat.readiness = 0u;
    (void)proto_encode_status(&stat, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_HEADER_BITS + (2u * PROTO_BITMAP_BITS),
                   PROTO_COORD_STATE_BITS, PROTO_COORD_COUNT);
    b_ok = test_main_check(!proto_decode_status(g_buf, LEN_STATUS, &stat_out),
                           __LINE__) &&
           b_ok;

    tel.display_state = 0u;
    (void)proto_encode_telemetry(&tel, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_TELEMETRY_BITS - PROTO_DISPLAY_STATE_BITS,
                   PROTO_DISPLAY_STATE_BITS, PROTO_DISPLAY_COUNT);
    b_ok =
        test_main_check(!proto_decode_telemetry(g_buf, LEN_TELEMETRY, &tel_out),
                        __LINE__) &&
        b_ok;

    chunk.chunk_index = 0u;
    (void)proto_encode_asset_chunk(&chunk, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_HEADER_BITS, PROTO_CHUNK_BITS,
                   PROTO_CHUNK_INDEX_MAX + 1u);
    b_ok = test_main_check(!proto_decode_asset_chunk(g_buf, LEN_CHUNK_HEAD + 1u,
                                                     &chunk_out),
                           __LINE__) &&
           b_ok;

    cfg.layout = 0u;
    (void)proto_encode_session_config(&cfg, g_buf, BUF_BYTES); /* Valid. */
    proto_put_bits(g_buf, PROTO_HEADER_BITS, PROTO_LAYOUT_BITS,
                   PROTO_LAYOUT_COUNT);
    b_ok = test_main_check(!proto_decode_session_config(
                               g_buf, LEN_SESSION_CONFIG, &cfg_out),
                           __LINE__) &&
           b_ok;

    /* Wrong type, wrong length and a payload that is too long or empty. */
    (void)proto_encode_heartbeat(&beat, g_buf, BUF_BYTES); /* Valid. */
    b_ok = test_main_check(
               !proto_decode_sync_ready(g_buf, LEN_SYNC_READY, &sync_out),
               __LINE__) &&
           b_ok;
    b_ok = test_main_check(
               !proto_decode_heartbeat(g_buf, LEN_HEARTBEAT + 1u, &beat_out),
               __LINE__) &&
           b_ok;
    b_ok = test_main_check(
               !proto_decode_asset_chunk(
                   g_buf, PROTO_ASSET_CHUNK_MAX_BYTES + 1u, &chunk_out),
               __LINE__) &&
           b_ok;
    b_ok = test_main_check(
               !proto_decode_asset_chunk(g_buf, LEN_CHUNK_HEAD, &chunk_out),
               __LINE__) &&
           b_ok;
    b_ok = test_main_check(
               0u == proto_encode_heartbeat(&beat, g_buf, LEN_HEARTBEAT - 1u),
               __LINE__) &&
           b_ok;
    printf("    7 out-of-range encodes, 6 out-of-range decodes, wrong type, "
           "wrong lengths and a short buffer refused\n");

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
        b_ok = test_main_check(ref[idx] == buf[idx], __LINE__) && b_ok;
    }

    b_ok = test_main_check(LE_U64 == proto_get_u64_le(buf), __LINE__) && b_ok;
    proto_put_u32_le(buf, LE_U32);
    b_ok = test_main_check((0x04u == buf[0]) && (0x01u == buf[3]), __LINE__) &&
           b_ok;
    b_ok = test_main_check(LE_U32 == proto_get_u32_le(buf), __LINE__) && b_ok;
    proto_put_u16_le(buf, LE_U16);
    b_ok = test_main_check((0x02u == buf[0]) && (0x01u == buf[1]), __LINE__) &&
           b_ok;
    b_ok = test_main_check(LE_U16 == proto_get_u16_le(buf), __LINE__) && b_ok;
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

    tally(proto_decode_register(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
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

    tally(proto_decode_heartbeat(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
          (msg.node_id == out.node_id));
}

/**
 * @brief  SESSION_CONFIG round trip: every map position 0 or 9.
 * @param  corner  0 or 1.
 */
static void rt_session_config (uint32_t corner)
{
    bool const             b_max = (CORNER_MIN != corner);
    proto_session_config_t msg   = {header_at(corner), 0u, 0u, {0u}};
    proto_session_config_t out;
    bool                   b_same;

    msg.layout = b_max ? (uint8_t)PROTO_LAYOUT_3X3 : 0u;   /* Code 2. */
    msg.policy = b_max ? (uint8_t)PROTO_POLICY_PAUSE : 0u; /* Code 1. */

    for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
    {
        msg.node_map[slot] = b_max ? PROTO_NODE_MAX : 0u;
    }

    uint32_t const len = proto_encode_session_config(&msg, g_buf, BUF_BYTES);

    b_same = proto_decode_session_config(g_buf, len, &out) &&
             same_header(&msg.header, &out.header) &&
             (msg.layout == out.layout) && (msg.policy == out.policy);

    for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
    {
        b_same = b_same && (msg.node_map[slot] == out.node_map[slot]);
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

    tally(proto_decode_manifest(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
          (msg.format_version == out.format_version) &&
          (msg.frame_count == out.frame_count) && (msg.crc32 == out.crc32));
}

/**
 * @brief  ASSET_CHUNK round trip: index 0 with 1 byte, index 95 with
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
    bool                b_same = proto_decode_asset_chunk(g_buf, len, &out) &&
                  same_header(&msg.header, &out.header) &&
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

    tally(proto_decode_chunk_ack(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
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

    tally(proto_decode_nack(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
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

    tally(proto_decode_busy(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
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

    tally(proto_decode_asset_ready(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
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

    tally(proto_decode_sync_ready(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
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

    tally(proto_decode_beacon(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) && (msg.t_p == out.t_p));
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

    tally(proto_decode_start_at(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
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

    tally(proto_decode_resume_at(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
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

    tally(proto_decode_pause(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
          !proto_decode_stop(g_buf, len, &out));

    len = proto_encode_stop(&msg, g_buf, BUF_BYTES);
    tally(proto_decode_stop(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
          !proto_decode_pause(g_buf, len, &out));
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
        b_max ? (uint8_t)PROTO_COORD_FAULT : 0u}; /* Code 11. */
    proto_status_t out;
    uint32_t const len = proto_encode_status(&msg, g_buf, BUF_BYTES);

    tally(proto_decode_status(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
          (msg.readiness == out.readiness) && (msg.online == out.online) &&
          (msg.state == out.state));
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

    tally(proto_decode_telemetry(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
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

    tally(proto_decode_telemetry_ack(g_buf, len, &out) &&
          same_header(&msg.header, &out.header) &&
          (msg.acked_seq == out.acked_seq));
}

/*** end of file ***/
