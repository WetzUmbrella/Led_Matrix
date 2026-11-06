/**
 * @file    proto.c
 * @brief   Shared wire layout (T-B2-01): bit packing and one encode/decode
 *          pair per message row of protocol/layout_table.md.
 *
 * Implemented by Ahsanul (B4) for Benjamin, task T-B2-01. Implemented and not
 * yet implemented: see proto.h. PROVISIONAL choices: see proto.h.
 *
 * Every field is written with proto_put_bits() in the order of its row in
 * layout_table.md, starting at bit PROTO_HEADER_BITS after the common
 * header, so the offsets in the table are the running sum of the widths.
 */
#include "proto.h"

#include <stddef.h>

#define BITS_PER_BYTE  8u
#define FULL_WIDTH     64u
#define BYTE_MASK      UINT64_C(0xFF)
#define HEADER_TYPE    0u
#define HEADER_SESSION (HEADER_TYPE + PROTO_TYPE_BITS)
#define HEADER_SEQ     (HEADER_SESSION + PROTO_SESSION_BITS)
#define BEACON_T_P     PROTO_HEADER_BITS
#define BITMAP_MASK    ((1u << PROTO_BITMAP_BITS) - 1u)
#define U16_BYTES      2u
#define U32_BYTES      4u
#define U64_BYTES      8u

/** Write position while encoding one message. */
typedef struct
{
    uint8_t * p_buf;
    uint32_t  bit;
} writer_t;

/** Read position while decoding one message. */
typedef struct
{
    uint8_t const * p_buf;
    uint32_t        bit;
} reader_t;

static uint64_t width_mask (uint32_t width);
static void     put_le (uint8_t * p_buf, uint32_t bytes, uint64_t value);
static uint64_t get_le (uint8_t const * p_buf, uint32_t bytes);

static bool begin_encode (uint8_t * p_buf, uint32_t buf_len, uint32_t bytes,
                          proto_type_t type, proto_header_t const * p_header,
                          writer_t * p_writer);
static bool begin_decode (uint8_t const * p_buf, uint32_t len, uint32_t bytes,
                          proto_type_t type, proto_header_t * p_header,
                          reader_t * p_reader);
static void put_field (writer_t * p_writer, uint32_t width, uint64_t value);
static uint64_t take_u64 (reader_t * p_reader, uint32_t width);
static uint32_t take_u32 (reader_t * p_reader, uint32_t width);
static uint16_t take_u16 (reader_t * p_reader, uint32_t width);
static uint8_t  take_u8 (reader_t * p_reader, uint32_t width);
static void     put_signed (writer_t * p_writer, uint32_t width, int8_t value);
static int8_t   take_s8 (reader_t * p_reader, uint32_t width);
static bool     score_ok (int8_t score);
static bool     node_ok (uint8_t node_id);
static bool     map_ok (uint8_t const * p_map);
static bool     frame_count_ok (uint16_t frame_count);
static bool     register_ok (proto_register_t const * p_msg);
static bool     session_config_ok (proto_session_config_t const * p_msg);
static bool     manifest_ok (proto_manifest_t const * p_msg);
static bool     chunk_ack_ok (proto_chunk_ack_t const * p_msg);
static bool     nack_ok (proto_nack_t const * p_msg);
static bool     status_ok (proto_status_t const * p_msg);
static bool     telemetry_ok (proto_telemetry_t const * p_msg);
static bool     tap_ok (proto_tap_t const * p_msg);
static bool     mole_on_ok (proto_mole_on_t const * p_msg);
static bool     feedback_ok (proto_feedback_msg_t const * p_msg);
static bool     game_control_ok (proto_game_control_t const * p_msg);
static bool     score_mask_ok (proto_score_mask_t const * p_msg);
static bool     ack_ok (proto_ack_t const * p_msg);
static uint32_t encode_command (proto_command_t const * p_msg,
                                proto_type_t type, uint8_t * p_buf,
                                uint32_t buf_len);
static bool     decode_command (uint8_t const * p_buf, uint32_t len,
                                proto_type_t type, proto_command_t * p_msg);

/**
 * @brief  Write the low width bits of value at bit offset bit_off, one bit at
 *         a time from the least significant bit.
 * @param  p_buf    Buffer.
 * @param  bit_off  First bit.
 * @param  width    1 to 64.
 * @param  value    Value.
 */
void proto_put_bits (uint8_t * p_buf, uint32_t bit_off, uint32_t width,
                     uint64_t value)
{
    uint64_t const field = value & width_mask(width);

    for (uint32_t bit = 0u; bit < width; bit++)
    {
        uint32_t const pos   = bit_off + bit;
        uint32_t const index = pos / BITS_PER_BYTE;

        /* pos % 8 is 0 to 7, so the shifted one fits uint8_t. */
        uint8_t const mask = (uint8_t)(1u << (pos % BITS_PER_BYTE));

        if (0u != ((field >> bit) & 1u))
        {
            p_buf[index] |= mask;
        }
        else
        {
            /* ~mask is promoted to int; the low 8 bits are the byte mask. */
            p_buf[index] &= (uint8_t)~mask;
        }
    }
}

/**
 * @brief  Read width bits at bit offset bit_off.
 * @param  p_buf    Buffer.
 * @param  bit_off  First bit.
 * @param  width    1 to 64.
 * @return The field value.
 */
uint64_t proto_get_bits (uint8_t const * p_buf, uint32_t bit_off,
                         uint32_t width)
{
    uint64_t field = 0u;

    for (uint32_t bit = 0u; bit < width; bit++)
    {
        uint32_t const pos   = bit_off + bit;
        uint32_t const index = pos / BITS_PER_BYTE;
        uint32_t const shift = pos % BITS_PER_BYTE;

        if (0u != ((p_buf[index] >> shift) & 1u))
        {
            field |= (UINT64_C(1) << bit);
        }
    }

    return field;
}

/**
 * @brief  Write a 16-bit value little-endian.
 * @param  p_buf  Buffer.
 * @param  value  Value.
 */
void proto_put_u16_le (uint8_t * p_buf, uint16_t value)
{
    put_le(p_buf, U16_BYTES, value);
}

/**
 * @brief  Write a 32-bit value little-endian.
 * @param  p_buf  Buffer.
 * @param  value  Value.
 */
void proto_put_u32_le (uint8_t * p_buf, uint32_t value)
{
    put_le(p_buf, U32_BYTES, value);
}

/**
 * @brief  Write a 64-bit value little-endian.
 * @param  p_buf  Buffer.
 * @param  value  Value.
 */
void proto_put_u64_le (uint8_t * p_buf, uint64_t value)
{
    put_le(p_buf, U64_BYTES, value);
}

/**
 * @brief  Read a little-endian 16-bit value.
 * @param  p_buf  Buffer.
 * @return The value.
 */
uint16_t proto_get_u16_le (uint8_t const * p_buf)
{
    /* Two bytes were read, so the value fits uint16_t. */
    return (uint16_t)get_le(p_buf, U16_BYTES);
}

/**
 * @brief  Read a little-endian 32-bit value.
 * @param  p_buf  Buffer.
 * @return The value.
 */
uint32_t proto_get_u32_le (uint8_t const * p_buf)
{
    /* Four bytes were read, so the value fits uint32_t. */
    return (uint32_t)get_le(p_buf, U32_BYTES);
}

/**
 * @brief  Read a little-endian 64-bit value.
 * @param  p_buf  Buffer.
 * @return The value.
 */
uint64_t proto_get_u64_le (uint8_t const * p_buf)
{
    return get_le(p_buf, U64_BYTES);
}

/**
 * @brief  Read the common header.
 * @param  p_buf     Received bytes.
 * @param  len       Received length, bytes.
 * @param  p_header  Receives the header.
 * @return true when the header is present and its type is known.
 */
bool proto_decode_header (uint8_t const * p_buf, uint32_t len,
                          proto_header_t * p_header)
{
    bool b_ok = false;

    if (len >= PROTO_HEADER_BYTES)
    {
        reader_t reader = {p_buf, HEADER_TYPE};

        p_header->type       = take_u8(&reader, PROTO_TYPE_BITS);
        p_header->session_id = take_u8(&reader, PROTO_SESSION_BITS);
        p_header->seq        = take_u16(&reader, PROTO_SEQ_BITS);
        b_ok = (p_header->type < (uint8_t)PROTO_TYPE_COUNT); /* Code 24. */
    }

    return b_ok;
}

/**
 * @brief  Encode a REGISTER.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_register (proto_register_t const * p_msg, uint8_t * p_buf,
                                uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((register_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_REGISTER_BYTES, PROTO_TYPE_REGISTER,
                      &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_SESSION_BITS, p_msg->cached_session_id);
        put_field(&writer, PROTO_CRC_BITS, p_msg->cached_crc32);
        written = PROTO_REGISTER_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a REGISTER.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_register (uint8_t const * p_buf, uint32_t len,
                            proto_register_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_REGISTER_BYTES, PROTO_TYPE_REGISTER,
                     &p_msg->header, &reader))
    {
        p_msg->node_id           = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->cached_session_id = take_u8(&reader, PROTO_SESSION_BITS);
        p_msg->cached_crc32      = take_u32(&reader, PROTO_CRC_BITS);
        b_ok                     = register_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode a HEARTBEAT.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_heartbeat (proto_heartbeat_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((node_ok(p_msg->node_id)) &&
        (begin_encode(p_buf, buf_len, PROTO_HEARTBEAT_BYTES,
                      PROTO_TYPE_HEARTBEAT, &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        written = PROTO_HEARTBEAT_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a HEARTBEAT.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_heartbeat (uint8_t const * p_buf, uint32_t len,
                             proto_heartbeat_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_HEARTBEAT_BYTES, PROTO_TYPE_HEARTBEAT,
                     &p_msg->header, &reader))
    {
        p_msg->node_id = take_u8(&reader, PROTO_NODE_BITS);
        b_ok           = node_ok(p_msg->node_id);
    }

    return b_ok;
}

/**
 * @brief  Encode a SESSION_CONFIG.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_session_config (proto_session_config_t const * p_msg,
                                      uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((session_config_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_SESSION_CONFIG_BYTES,
                      PROTO_TYPE_SESSION_CONFIG, &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_GRID_BITS, p_msg->rows);
        put_field(&writer, PROTO_GRID_BITS, p_msg->cols);
        put_field(&writer, PROTO_POLICY_BITS, p_msg->policy);

        for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
        {
            put_field(&writer, PROTO_NODE_BITS, p_msg->node_map[slot]);
        }

        put_field(&writer, PROTO_FRAME_BITS, p_msg->frame_count);

        for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
        {
            put_field(&writer, PROTO_CRC_BITS, p_msg->crc32[slot]);
        }

        written = PROTO_SESSION_CONFIG_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a SESSION_CONFIG.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_session_config (uint8_t const * p_buf, uint32_t len,
                                  proto_session_config_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_SESSION_CONFIG_BYTES,
                     PROTO_TYPE_SESSION_CONFIG, &p_msg->header, &reader))
    {
        p_msg->rows   = take_u8(&reader, PROTO_GRID_BITS);
        p_msg->cols   = take_u8(&reader, PROTO_GRID_BITS);
        p_msg->policy = take_u8(&reader, PROTO_POLICY_BITS);

        for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
        {
            p_msg->node_map[slot] = take_u8(&reader, PROTO_NODE_BITS);
        }

        p_msg->frame_count = take_u16(&reader, PROTO_FRAME_BITS);

        for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
        {
            p_msg->crc32[slot] = take_u32(&reader, PROTO_CRC_BITS);
        }

        b_ok = session_config_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode a MANIFEST.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_manifest (proto_manifest_t const * p_msg, uint8_t * p_buf,
                                uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((manifest_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_MANIFEST_BYTES, PROTO_TYPE_MANIFEST,
                      &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_FORMAT_BITS, p_msg->format_version);
        put_field(&writer, PROTO_FRAME_BITS, p_msg->frame_count);
        put_field(&writer, PROTO_CRC_BITS, p_msg->crc32);
        written = PROTO_MANIFEST_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a MANIFEST. An unknown format_version is a valid wire value;
 *         the tile rejects it after decoding (VC-5).
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_manifest (uint8_t const * p_buf, uint32_t len,
                            proto_manifest_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_MANIFEST_BYTES, PROTO_TYPE_MANIFEST,
                     &p_msg->header, &reader))
    {
        p_msg->format_version = take_u8(&reader, PROTO_FORMAT_BITS);
        p_msg->frame_count    = take_u16(&reader, PROTO_FRAME_BITS);
        p_msg->crc32          = take_u32(&reader, PROTO_CRC_BITS);
        b_ok                  = manifest_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode an ASSET_CHUNK: header and chunk index, then the payload
 *         from the next byte boundary.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_asset_chunk (proto_asset_chunk_t const * p_msg,
                                   uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t       written = 0u;
    uint32_t const total   = PROTO_CHUNK_HEAD_BYTES + p_msg->payload_len;
    bool const     b_valid = (p_msg->chunk_index <= PROTO_CHUNK_INDEX_MAX) &&
                         (p_msg->payload_len >= 1u) &&
                         (p_msg->payload_len <= PROTO_CHUNK_PAYLOAD_MAX) &&
                         (NULL != p_msg->p_payload);
    writer_t writer;

    if (b_valid && (begin_encode(p_buf, buf_len, total, PROTO_TYPE_ASSET_CHUNK,
                                 &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_CHUNK_BITS, p_msg->chunk_index);

        for (uint32_t idx = 0u; idx < p_msg->payload_len; idx++)
        {
            p_buf[PROTO_CHUNK_HEAD_BYTES + idx] = p_msg->p_payload[idx];
        }

        written = total;
    }

    return written;
}

/**
 * @brief  Decode an ASSET_CHUNK. p_msg->p_payload points into p_buf.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_asset_chunk (uint8_t const * p_buf, uint32_t len,
                               proto_asset_chunk_t * p_msg)
{
    bool       b_ok = false;
    bool const b_length =
        (len > PROTO_CHUNK_HEAD_BYTES) && (len <= PROTO_ASSET_CHUNK_MAX_BYTES);
    reader_t reader;

    if (b_length && (begin_decode(p_buf, len, len, PROTO_TYPE_ASSET_CHUNK,
                                  &p_msg->header, &reader)))
    {
        p_msg->chunk_index = take_u8(&reader, PROTO_CHUNK_BITS);

        /* len is at most 1,029 here, so the payload length fits. */
        p_msg->payload_len = (uint16_t)(len - PROTO_CHUNK_HEAD_BYTES);
        p_msg->p_payload   = &p_buf[PROTO_CHUNK_HEAD_BYTES];
        b_ok               = (p_msg->chunk_index <= PROTO_CHUNK_INDEX_MAX);
    }

    return b_ok;
}

/**
 * @brief  Encode a CHUNK_ACK.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_chunk_ack (proto_chunk_ack_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((chunk_ack_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_CHUNK_ACK_BYTES,
                      PROTO_TYPE_CHUNK_ACK, &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_CHUNK_BITS, p_msg->chunk_index);
        written = PROTO_CHUNK_ACK_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a CHUNK_ACK.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_chunk_ack (uint8_t const * p_buf, uint32_t len,
                             proto_chunk_ack_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_CHUNK_ACK_BYTES, PROTO_TYPE_CHUNK_ACK,
                     &p_msg->header, &reader))
    {
        p_msg->node_id     = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->chunk_index = take_u8(&reader, PROTO_CHUNK_BITS);
        b_ok               = chunk_ack_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode a NACK.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_nack (proto_nack_t const * p_msg, uint8_t * p_buf,
                            uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((nack_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_NACK_BYTES, PROTO_TYPE_NACK,
                      &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_REASON_BITS, p_msg->reason);
        put_field(&writer, PROTO_SEQ_BITS, p_msg->ref_seq);
        written = PROTO_NACK_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a NACK.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_nack (uint8_t const * p_buf, uint32_t len,
                        proto_nack_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_NACK_BYTES, PROTO_TYPE_NACK,
                     &p_msg->header, &reader))
    {
        p_msg->node_id = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->reason  = take_u8(&reader, PROTO_REASON_BITS);
        p_msg->ref_seq = take_u16(&reader, PROTO_SEQ_BITS);
        b_ok           = nack_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode a BUSY.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_busy (proto_busy_t const * p_msg, uint8_t * p_buf,
                            uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((node_ok(p_msg->node_id)) &&
        (begin_encode(p_buf, buf_len, PROTO_BUSY_BYTES, PROTO_TYPE_BUSY,
                      &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_SEQ_BITS, p_msg->ref_seq);
        written = PROTO_BUSY_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a BUSY.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_busy (uint8_t const * p_buf, uint32_t len,
                        proto_busy_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_BUSY_BYTES, PROTO_TYPE_BUSY,
                     &p_msg->header, &reader))
    {
        p_msg->node_id = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->ref_seq = take_u16(&reader, PROTO_SEQ_BITS);
        b_ok           = node_ok(p_msg->node_id);
    }

    return b_ok;
}

/**
 * @brief  Encode an ASSET_READY.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_asset_ready (proto_asset_ready_t const * p_msg,
                                   uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((node_ok(p_msg->node_id)) &&
        (begin_encode(p_buf, buf_len, PROTO_ASSET_READY_BYTES,
                      PROTO_TYPE_ASSET_READY, &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_CRC_BITS, p_msg->crc32);
        written = PROTO_ASSET_READY_BYTES;
    }

    return written;
}

/**
 * @brief  Decode an ASSET_READY.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_asset_ready (uint8_t const * p_buf, uint32_t len,
                               proto_asset_ready_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_ASSET_READY_BYTES,
                     PROTO_TYPE_ASSET_READY, &p_msg->header, &reader))
    {
        p_msg->node_id = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->crc32   = take_u32(&reader, PROTO_CRC_BITS);
        b_ok           = node_ok(p_msg->node_id);
    }

    return b_ok;
}

/**
 * @brief  Encode a SYNC_READY.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_sync_ready (proto_sync_ready_t const * p_msg,
                                  uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((node_ok(p_msg->node_id)) &&
        (begin_encode(p_buf, buf_len, PROTO_SYNC_READY_BYTES,
                      PROTO_TYPE_SYNC_READY, &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        written = PROTO_SYNC_READY_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a SYNC_READY.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_sync_ready (uint8_t const * p_buf, uint32_t len,
                              proto_sync_ready_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_SYNC_READY_BYTES, PROTO_TYPE_SYNC_READY,
                     &p_msg->header, &reader))
    {
        p_msg->node_id = take_u8(&reader, PROTO_NODE_BITS);
        b_ok           = node_ok(p_msg->node_id);
    }

    return b_ok;
}

/**
 * @brief  Encode a BEACON.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0 if p_buf is too small.
 */
uint32_t proto_encode_beacon (proto_beacon_t const * p_msg, uint8_t * p_buf,
                              uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if (begin_encode(p_buf, buf_len, PROTO_BEACON_BYTES, PROTO_TYPE_BEACON,
                     &p_msg->header, &writer))
    {
        put_field(&writer, PROTO_TIME_BITS, p_msg->t_p);
        written = PROTO_BEACON_BYTES;
    }

    return written;
}

/**
 * @brief  Overwrite only the t_p field of an encoded BEACON.
 * @param  p_buf  An encoded BEACON.
 * @param  t_p    Pico time, microseconds.
 */
void proto_beacon_set_t_p (uint8_t * p_buf, uint64_t t_p)
{
    proto_put_bits(p_buf, BEACON_T_P, PROTO_TIME_BITS, t_p);
}

/**
 * @brief  Decode a BEACON.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message when the result is true.
 * @return true when p_buf holds a BEACON.
 */
bool proto_decode_beacon (uint8_t const * p_buf, uint32_t len,
                          proto_beacon_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_BEACON_BYTES, PROTO_TYPE_BEACON,
                     &p_msg->header, &reader))
    {
        p_msg->t_p = take_u64(&reader, PROTO_TIME_BITS);
        b_ok       = true;
    }

    return b_ok;
}

/**
 * @brief  Encode a START_AT.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_start_at (proto_start_at_t const * p_msg, uint8_t * p_buf,
                                uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if (begin_encode(p_buf, buf_len, PROTO_START_AT_BYTES, PROTO_TYPE_START_AT,
                     &p_msg->header, &writer))
    {
        put_field(&writer, PROTO_TIME_BITS, p_msg->t_start);
        written = PROTO_START_AT_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a START_AT.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_start_at (uint8_t const * p_buf, uint32_t len,
                            proto_start_at_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_START_AT_BYTES, PROTO_TYPE_START_AT,
                     &p_msg->header, &reader))
    {
        p_msg->t_start = take_u64(&reader, PROTO_TIME_BITS);
        b_ok           = true;
    }

    return b_ok;
}

/**
 * @brief  Encode a RESUME_AT.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_resume_at (proto_resume_at_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((p_msg->frame_index <= PROTO_FRAME_MAX) &&
        (begin_encode(p_buf, buf_len, PROTO_RESUME_AT_BYTES,
                      PROTO_TYPE_RESUME_AT, &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_TIME_BITS, p_msg->t_resume);
        put_field(&writer, PROTO_FRAME_BITS, p_msg->frame_index);
        written = PROTO_RESUME_AT_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a RESUME_AT. Every 10-bit frame index is in range.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_resume_at (uint8_t const * p_buf, uint32_t len,
                             proto_resume_at_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_RESUME_AT_BYTES, PROTO_TYPE_RESUME_AT,
                     &p_msg->header, &reader))
    {
        p_msg->t_resume    = take_u64(&reader, PROTO_TIME_BITS);
        p_msg->frame_index = take_u16(&reader, PROTO_FRAME_BITS);
        b_ok               = true;
    }

    return b_ok;
}

/**
 * @brief  Encode a PAUSE.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_pause (proto_command_t const * p_msg, uint8_t * p_buf,
                             uint32_t buf_len)
{
    return encode_command(p_msg, PROTO_TYPE_PAUSE, p_buf, buf_len);
}

/**
 * @brief  Decode a PAUSE.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_pause (uint8_t const * p_buf, uint32_t len,
                         proto_command_t * p_msg)
{
    return decode_command(p_buf, len, PROTO_TYPE_PAUSE, p_msg);
}

/**
 * @brief  Encode a STOP.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_stop (proto_command_t const * p_msg, uint8_t * p_buf,
                            uint32_t buf_len)
{
    return encode_command(p_msg, PROTO_TYPE_STOP, p_buf, buf_len);
}

/**
 * @brief  Decode a STOP.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_stop (uint8_t const * p_buf, uint32_t len,
                        proto_command_t * p_msg)
{
    return decode_command(p_buf, len, PROTO_TYPE_STOP, p_msg);
}

/**
 * @brief  Encode a STATUS.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_status (proto_status_t const * p_msg, uint8_t * p_buf,
                              uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((status_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_STATUS_BYTES, PROTO_TYPE_STATUS,
                      &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_BITMAP_BITS, p_msg->readiness);
        put_field(&writer, PROTO_BITMAP_BITS, p_msg->online);
        put_field(&writer, PROTO_COORD_STATE_BITS, p_msg->state);
        put_signed(&writer, PROTO_SCORE_BITS, p_msg->score);
        written = PROTO_STATUS_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a STATUS.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_status (uint8_t const * p_buf, uint32_t len,
                          proto_status_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_STATUS_BYTES, PROTO_TYPE_STATUS,
                     &p_msg->header, &reader))
    {
        p_msg->readiness = take_u16(&reader, PROTO_BITMAP_BITS);
        p_msg->online    = take_u16(&reader, PROTO_BITMAP_BITS);
        p_msg->state     = take_u8(&reader, PROTO_COORD_STATE_BITS);
        p_msg->score     = take_s8(&reader, PROTO_SCORE_BITS);
        b_ok             = status_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode a TELEMETRY.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_telemetry (proto_telemetry_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((telemetry_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_TELEMETRY_BYTES,
                      PROTO_TYPE_TELEMETRY, &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_FAULT_BITS, p_msg->fault_word);
        put_field(&writer, PROTO_ORIENT_BITS, p_msg->orientation);
        put_field(&writer, PROTO_DISPLAY_STATE_BITS, p_msg->display_state);
        written = PROTO_TELEMETRY_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a TELEMETRY.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_telemetry (uint8_t const * p_buf, uint32_t len,
                             proto_telemetry_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_TELEMETRY_BYTES, PROTO_TYPE_TELEMETRY,
                     &p_msg->header, &reader))
    {
        p_msg->node_id       = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->fault_word    = take_u32(&reader, PROTO_FAULT_BITS);
        p_msg->orientation   = take_u8(&reader, PROTO_ORIENT_BITS);
        p_msg->display_state = take_u8(&reader, PROTO_DISPLAY_STATE_BITS);
        b_ok                 = telemetry_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode a TELEMETRY_ACK.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_telemetry_ack (proto_telemetry_ack_t const * p_msg,
                                     uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if (begin_encode(p_buf, buf_len, PROTO_TELEMETRY_ACK_BYTES,
                     PROTO_TYPE_TELEMETRY_ACK, &p_msg->header, &writer))
    {
        put_field(&writer, PROTO_SEQ_BITS, p_msg->acked_seq);
        written = PROTO_TELEMETRY_ACK_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a TELEMETRY_ACK.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_telemetry_ack (uint8_t const * p_buf, uint32_t len,
                                 proto_telemetry_ack_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_TELEMETRY_ACK_BYTES,
                     PROTO_TYPE_TELEMETRY_ACK, &p_msg->header, &reader))
    {
        p_msg->acked_seq = take_u16(&reader, PROTO_SEQ_BITS);
        b_ok             = true;
    }

    return b_ok;
}

/**
 * @brief  Encode a TAP.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_tap (proto_tap_t const * p_msg, uint8_t * p_buf,
                           uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((tap_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_TAP_BYTES, PROTO_TYPE_TAP,
                      &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_PEAK_BITS, p_msg->peak);
        put_field(&writer, PROTO_TIME_BITS, p_msg->t_tap);
        written = PROTO_TAP_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a TAP.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_tap (uint8_t const * p_buf, uint32_t len, proto_tap_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_TAP_BYTES, PROTO_TYPE_TAP,
                     &p_msg->header, &reader))
    {
        p_msg->node_id = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->peak    = take_u16(&reader, PROTO_PEAK_BITS);
        p_msg->t_tap   = take_u64(&reader, PROTO_TIME_BITS);
        b_ok           = tap_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode a MOLE_ON.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_mole_on (proto_mole_on_t const * p_msg, uint8_t * p_buf,
                               uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((mole_on_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_MOLE_ON_BYTES, PROTO_TYPE_MOLE_ON,
                      &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_TIME_BITS, p_msg->t_show);
        put_field(&writer, PROTO_UP_MS_BITS, p_msg->up_ms);
        written = PROTO_MOLE_ON_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a MOLE_ON.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_mole_on (uint8_t const * p_buf, uint32_t len,
                           proto_mole_on_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_MOLE_ON_BYTES, PROTO_TYPE_MOLE_ON,
                     &p_msg->header, &reader))
    {
        p_msg->node_id = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->t_show  = take_u64(&reader, PROTO_TIME_BITS);
        p_msg->up_ms   = take_u16(&reader, PROTO_UP_MS_BITS);
        b_ok           = mole_on_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode a FEEDBACK.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_feedback (proto_feedback_msg_t const * p_msg,
                                uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((feedback_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_FEEDBACK_BYTES, PROTO_TYPE_FEEDBACK,
                      &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_RESULT_BITS, p_msg->result);
        written = PROTO_FEEDBACK_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a FEEDBACK.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_feedback (uint8_t const * p_buf, uint32_t len,
                            proto_feedback_msg_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_FEEDBACK_BYTES, PROTO_TYPE_FEEDBACK,
                     &p_msg->header, &reader))
    {
        p_msg->node_id = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->result  = take_u8(&reader, PROTO_RESULT_BITS);
        b_ok           = feedback_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode a GAME_CONTROL.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_game_control (proto_game_control_t const * p_msg,
                                    uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((game_control_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_GAME_CONTROL_BYTES,
                      PROTO_TYPE_GAME_CONTROL, &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_GAME_CMD_BITS, p_msg->command);
        written = PROTO_GAME_CONTROL_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a GAME_CONTROL.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_game_control (uint8_t const * p_buf, uint32_t len,
                                proto_game_control_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_GAME_CONTROL_BYTES,
                     PROTO_TYPE_GAME_CONTROL, &p_msg->header, &reader))
    {
        p_msg->node_id = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->command = take_u8(&reader, PROTO_GAME_CMD_BITS);
        b_ok           = game_control_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode a SCORE_MASK.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_score_mask (proto_score_mask_t const * p_msg,
                                  uint8_t * p_buf, uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((score_mask_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_SCORE_MASK_BYTES,
                      PROTO_TYPE_SCORE_MASK, &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_MASK_BITS, p_msg->mask);
        written = PROTO_SCORE_MASK_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a SCORE_MASK.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_score_mask (uint8_t const * p_buf, uint32_t len,
                              proto_score_mask_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_SCORE_MASK_BYTES, PROTO_TYPE_SCORE_MASK,
                     &p_msg->header, &reader))
    {
        p_msg->node_id = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->mask    = take_u64(&reader, PROTO_MASK_BITS);
        b_ok           = score_mask_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Encode an ACK.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
uint32_t proto_encode_ack (proto_ack_t const * p_msg, uint8_t * p_buf,
                           uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if ((ack_ok(p_msg)) &&
        (begin_encode(p_buf, buf_len, PROTO_ACK_BYTES, PROTO_TYPE_ACK,
                      &p_msg->header, &writer)))
    {
        put_field(&writer, PROTO_NODE_BITS, p_msg->node_id);
        put_field(&writer, PROTO_SEQ_BITS, p_msg->acked_seq);
        written = PROTO_ACK_BYTES;
    }

    return written;
}

/**
 * @brief  Decode an ACK.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
bool proto_decode_ack (uint8_t const * p_buf, uint32_t len, proto_ack_t * p_msg)
{
    bool     b_ok = false;
    reader_t reader;

    if (begin_decode(p_buf, len, PROTO_ACK_BYTES, PROTO_TYPE_ACK,
                     &p_msg->header, &reader))
    {
        p_msg->node_id   = take_u8(&reader, PROTO_NODE_BITS);
        p_msg->acked_seq = take_u16(&reader, PROTO_SEQ_BITS);
        b_ok             = ack_ok(p_msg);
    }

    return b_ok;
}

/**
 * @brief  Mask of the low width bits.
 * @param  width  1 to 64.
 * @return The mask.
 */
static uint64_t width_mask (uint32_t width)
{
    uint64_t mask = UINT64_MAX;

    if (width < FULL_WIDTH)
    {
        mask = (UINT64_C(1) << width) - 1u;
    }

    return mask;
}

/**
 * @brief  Write bytes of value, least significant first.
 * @param  p_buf  Buffer.
 * @param  bytes  2, 4 or 8.
 * @param  value  Value.
 */
static void put_le (uint8_t * p_buf, uint32_t bytes, uint64_t value)
{
    for (uint32_t idx = 0u; idx < bytes; idx++)
    {
        /* Masked to one byte, so it fits uint8_t. */
        p_buf[idx] = (uint8_t)((value >> (idx * BITS_PER_BYTE)) & BYTE_MASK);
    }
}

/**
 * @brief  Read bytes, least significant first.
 * @param  p_buf  Buffer.
 * @param  bytes  2, 4 or 8.
 * @return The value.
 */
static uint64_t get_le (uint8_t const * p_buf, uint32_t bytes)
{
    uint64_t value = 0u;

    for (uint32_t idx = 0u; idx < bytes; idx++)
    {
        value |= ((uint64_t)p_buf[idx] << (idx * BITS_PER_BYTE)); /* Widen. */
    }

    return value;
}

/**
 * @brief  Zero the message bytes, write the header with the row's type code
 *         and leave the writer just after the header.
 * @param  p_buf     Output buffer.
 * @param  buf_len   Size of p_buf, bytes.
 * @param  bytes     Encoded length of this message, bytes.
 * @param  type      The row's type code.
 * @param  p_header  session_id and seq to write.
 * @param  p_writer  Receives the write position.
 * @return false when p_buf is too small.
 */
static bool begin_encode (uint8_t * p_buf, uint32_t buf_len, uint32_t bytes,
                          proto_type_t type, proto_header_t const * p_header,
                          writer_t * p_writer)
{
    bool b_ok = false;

    if (buf_len >= bytes)
    {
        for (uint32_t idx = 0u; idx < bytes; idx++)
        {
            p_buf[idx] = 0u;
        }

        p_writer->p_buf = p_buf;
        p_writer->bit   = HEADER_TYPE;
        put_field(p_writer, PROTO_TYPE_BITS, (uint64_t)type); /* Code < 24. */
        put_field(p_writer, PROTO_SESSION_BITS, p_header->session_id);
        put_field(p_writer, PROTO_SEQ_BITS, p_header->seq);
        b_ok = true;
    }

    return b_ok;
}

/**
 * @brief  Check the length and type and read the header.
 * @param  p_buf     Received bytes.
 * @param  len       Received length, bytes.
 * @param  bytes     Required length, bytes.
 * @param  type      Required type code.
 * @param  p_header  Receives the header.
 * @param  p_reader  Receives the read position after the header.
 * @return true when the length and type match.
 */
static bool begin_decode (uint8_t const * p_buf, uint32_t len, uint32_t bytes,
                          proto_type_t type, proto_header_t * p_header,
                          reader_t * p_reader)
{
    bool b_ok = false;

    if ((bytes == len) && (proto_decode_header(p_buf, len, p_header)))
    {
        p_reader->p_buf = p_buf;
        p_reader->bit   = PROTO_HEADER_BITS;
        b_ok            = ((uint8_t)type == p_header->type); /* Code < 24. */
    }

    return b_ok;
}

/**
 * @brief  Write one field at the writer position and move past it.
 * @param  p_writer  Write position.
 * @param  width     Field width, bits.
 * @param  value     Field value.
 */
static void put_field (writer_t * p_writer, uint32_t width, uint64_t value)
{
    proto_put_bits(p_writer->p_buf, p_writer->bit, width, value);
    p_writer->bit += width;
}

/**
 * @brief  Read one field of up to 64 bits and move past it.
 * @param  p_reader  Read position.
 * @param  width     Field width, bits.
 * @return The field.
 */
static uint64_t take_u64 (reader_t * p_reader, uint32_t width)
{
    uint64_t const value =
        proto_get_bits(p_reader->p_buf, p_reader->bit, width);

    p_reader->bit += width;

    return value;
}

/**
 * @brief  Read one field of at most 32 bits.
 * @param  p_reader  Read position.
 * @param  width     Field width, at most 32 bits.
 * @return The field.
 */
static uint32_t take_u32 (reader_t * p_reader, uint32_t width)
{
    /* Callers pass at most 32 bits, so the field fits uint32_t. */
    return (uint32_t)take_u64(p_reader, width);
}

/**
 * @brief  Read one field of at most 16 bits.
 * @param  p_reader  Read position.
 * @param  width     Field width, at most 16 bits.
 * @return The field.
 */
static uint16_t take_u16 (reader_t * p_reader, uint32_t width)
{
    /* Callers pass at most 16 bits, so the field fits uint16_t. */
    return (uint16_t)take_u64(p_reader, width);
}

/**
 * @brief  Read one field of at most 8 bits.
 * @param  p_reader  Read position.
 * @param  width     Field width, at most 8 bits.
 * @return The field.
 */
static uint8_t take_u8 (reader_t * p_reader, uint32_t width)
{
    /* Callers pass at most 8 bits, so the field fits uint8_t. */
    return (uint8_t)take_u64(p_reader, width);
}

/**
 * @brief  Write a signed field of at most 8 bits in two's complement.
 * @param  p_writer  Write position.
 * @param  width     Field width, at most 8 bits.
 * @param  value     Field value; put_field keeps its low width bits.
 */
static void put_signed (writer_t * p_writer, uint32_t width, int8_t value)
{
    /* Conversion to uint8_t is modulo 256, which is two's complement. */
    put_field(p_writer, width, (uint8_t)value);
}

/**
 * @brief  Read an 8-bit two's complement field (inverse of put_signed).
 * @param  p_reader  Read position.
 * @param  width     Field width, 8 bits.
 * @return The field, -128 to 127.
 */
static int8_t take_s8 (reader_t * p_reader, uint32_t width)
{
    int16_t value = (int16_t)take_u8(p_reader, width); /* 0 to 255. */

    if (value > INT8_MAX)
    {
        value -= (int16_t)(UINT8_MAX + 1); /* 128 to 255 -> -128 to -1. */
    }

    /* value is -128 to 127 here, so it fits int8_t. */
    return (int8_t)value;
}

/**
 * @brief  A game score is -99 to 99.
 * @param  score  Score.
 * @return true when in range.
 */
static bool score_ok (int8_t score)
{
    return (score >= -PROTO_SCORE_MAX) && (score <= PROTO_SCORE_MAX);
}

/**
 * @brief  A sender's Node ID is 1 to 9.
 * @param  node_id  Node ID.
 * @return true when in range.
 */
static bool node_ok (uint8_t node_id)
{
    return (node_id >= 1u) && (node_id <= PROTO_NODE_MAX);
}

/**
 * @brief  Every map position holds 0 (blank) or a Node ID 1 to 9.
 * @param  p_map  PROTO_MAP_SLOTS entries.
 * @return true when every entry is in range.
 */
static bool map_ok (uint8_t const * p_map)
{
    bool b_ok = true;

    for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
    {
        b_ok = b_ok && (p_map[slot] <= PROTO_NODE_MAX);
    }

    return b_ok;
}

/**
 * @brief  frame_count is 1 to 1023 (VC-3).
 * @param  frame_count  Frames.
 * @return true when in range.
 */
static bool frame_count_ok (uint16_t frame_count)
{
    return (frame_count >= 1u) && (frame_count <= PROTO_FRAME_MAX);
}

/**
 * @brief  REGISTER valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool register_ok (proto_register_t const * p_msg)
{
    return node_ok(p_msg->node_id);
}

/**
 * @brief  SESSION_CONFIG valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool session_config_ok (proto_session_config_t const * p_msg)
{
    bool const b_rows = (p_msg->rows >= 1u) && (p_msg->rows <= PROTO_GRID_MAX);
    bool const b_cols = (p_msg->cols >= 1u) && (p_msg->cols <= PROTO_GRID_MAX);

    /* Both are at most 9 when tested, so the product is at most 81. */
    return (b_rows) && (b_cols) &&
           (((uint32_t)p_msg->rows * p_msg->cols) <= PROTO_GRID_MAX) &&
           (p_msg->policy < (uint8_t)PROTO_POLICY_COUNT) && /* Code < 2. */
           (map_ok(p_msg->node_map)) && (frame_count_ok(p_msg->frame_count));
}

/**
 * @brief  MANIFEST valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool manifest_ok (proto_manifest_t const * p_msg)
{
    return (p_msg->format_version < (uint8_t)PROTO_FORMAT_COUNT) && /* < 4 */
           (frame_count_ok(p_msg->frame_count));
}

/**
 * @brief  CHUNK_ACK valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool chunk_ack_ok (proto_chunk_ack_t const * p_msg)
{
    return (node_ok(p_msg->node_id)) &&
           (p_msg->chunk_index <= PROTO_CHUNK_INDEX_MAX);
}

/**
 * @brief  NACK valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool nack_ok (proto_nack_t const * p_msg)
{
    return (node_ok(p_msg->node_id)) &&
           (p_msg->reason < (uint8_t)PROTO_NACK_COUNT); /* Code < 4. */
}

/**
 * @brief  STATUS valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool status_ok (proto_status_t const * p_msg)
{
    return (p_msg->readiness <= BITMAP_MASK) &&
           (p_msg->online <= BITMAP_MASK) &&
           (p_msg->state < (uint8_t)PROTO_COORD_COUNT) && /* Code < 13. */
           (score_ok(p_msg->score));
}

/**
 * @brief  TELEMETRY valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool telemetry_ok (proto_telemetry_t const * p_msg)
{
    return (node_ok(p_msg->node_id)) &&
           (p_msg->orientation < (uint8_t)PROTO_ORIENT_COUNT) &&  /* < 4 */
           (p_msg->display_state < (uint8_t)PROTO_DISPLAY_COUNT); /* < 7 */
}

/**
 * @brief  TAP valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool tap_ok (proto_tap_t const * p_msg)
{
    return node_ok(p_msg->node_id);
}

/**
 * @brief  MOLE_ON valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool mole_on_ok (proto_mole_on_t const * p_msg)
{
    return (node_ok(p_msg->node_id)) && (p_msg->up_ms >= 1u) &&
           (p_msg->up_ms <= PROTO_UP_MS_MAX);
}

/**
 * @brief  FEEDBACK valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool feedback_ok (proto_feedback_msg_t const * p_msg)
{
    return (node_ok(p_msg->node_id)) &&
           (p_msg->result < (uint8_t)PROTO_FEEDBACK_COUNT); /* Code < 3. */
}

/**
 * @brief  GAME_CONTROL valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool game_control_ok (proto_game_control_t const * p_msg)
{
    return (p_msg->node_id <= PROTO_NODE_MAX) &&             /* 0 allowed */
           (p_msg->command < (uint8_t)PROTO_GAME_CMD_COUNT); /* Code < 4. */
}

/**
 * @brief  SCORE_MASK valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool score_mask_ok (proto_score_mask_t const * p_msg)
{
    return node_ok(p_msg->node_id);
}

/**
 * @brief  ACK valid-range check.
 * @param  p_msg  Message.
 * @return true when valid.
 */
static bool ack_ok (proto_ack_t const * p_msg)
{
    return p_msg->node_id <= PROTO_NODE_MAX; /* 0 allowed */
}

/**
 * @brief  Encode a header-only command (PAUSE, STOP).
 * @param  p_msg    Message.
 * @param  type     PROTO_TYPE_PAUSE or PROTO_TYPE_STOP.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written, or 0.
 */
static uint32_t encode_command (proto_command_t const * p_msg,
                                proto_type_t type, uint8_t * p_buf,
                                uint32_t buf_len)
{
    uint32_t written = 0u;
    writer_t writer;

    if (begin_encode(p_buf, buf_len, PROTO_PAUSE_BYTES, type, &p_msg->header,
                     &writer))
    {
        written = PROTO_PAUSE_BYTES;
    }

    return written;
}

/**
 * @brief  Decode a header-only command (PAUSE, STOP).
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  type   PROTO_TYPE_PAUSE or PROTO_TYPE_STOP.
 * @param  p_msg  Receives the message.
 * @return true when valid.
 */
static bool decode_command (uint8_t const * p_buf, uint32_t len,
                            proto_type_t type, proto_command_t * p_msg)
{
    reader_t reader;

    return begin_decode(p_buf, len, PROTO_PAUSE_BYTES, type, &p_msg->header,
                        &reader);
}

/*** end of file ***/
