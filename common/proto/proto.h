/**
 * @file    proto.h
 * @brief   Shared wire layout (T-B2-01): one encode/decode pair per message,
 *          explicit shifts and masks, bit 0 is the least significant bit of
 *          byte 0, every multi-byte field little-endian, no wire struct is
 *          ever sent.
 *
 * Implemented by Ahsanul (B4) for Benjamin, task T-B2-01. The field list,
 * offsets and widths are protocol/layout_table.md; this file and
 * pc/proto_layout.py (T-B1-01) follow it, and the cross-language vectors in
 * protocol/vectors/ prove they agree.
 * Implemented: proto_put_bits(), proto_get_bits(), the byte-aligned
 * proto_put_u16_le/u32_le/u64_le and proto_get_*_le helpers, the common
 * header, and an encode/decode pair for each of the 18 message rows, each
 * with a valid-range check; host tests with the T-B2-01 boundary set.
 * Not yet implemented: the OD-01 citation sentence on where poll-mode
 * callbacks run (layout_table.md section 9 records the gap).
 *
 * PROVISIONAL (OD-07, owner Benjamin): every width not fixed by PostTri
 * (type 5, session_id 8, seq 16, Node ID 4, format_version 2, chunk index 7,
 * NACK reason 2, layout 2, failure policy 1; coordinator state 4 bits is
 * derived, ceil(log2 12)), the
 * type codes in T-B2-01 row order, contiguous packing from bit 0, a node_id
 * in every message a tile sends, ref_seq in NACK and BUSY, acked_seq in
 * TELEMETRY_ACK, frame_rate left off the wire (one valid value, 0 bits), and
 * the ASSET_CHUNK payload starting on the next byte boundary.
 * PROVISIONAL (OD-05, owners Jolyn and Jia Jun): nibble order (a), so packed
 * nibble k of a frame is the 4-bit field at bit 4k.
 * PROVISIONAL (plan file names): the plan names the files proto_layout.c/.h;
 * they are proto.c/.h so that the plan's function names (proto_put_bits(),
 * proto_encode_beacon()) carry their module prefix (Barr-C 6.1.i). See
 * docs/proto_DEFENCE.md.
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdbool.h>
#include <stdint.h>

/** Bytes needed for a number of bits. */
#define PROTO_BYTES(bits) (((bits) + 7u) / 8u)

/** Width of the message type field, bits (PROVISIONAL, OD-07). */
#define PROTO_TYPE_BITS 5u

/** Width of the session_id field, bits (PROVISIONAL, OD-07). */
#define PROTO_SESSION_BITS 8u

/** Width of the seq field, bits (PROVISIONAL, OD-07). */
#define PROTO_SEQ_BITS 16u

/** Width of every timestamp, bits (LDC-6: 64-bit time). */
#define PROTO_TIME_BITS 64u

/** Width of a Node ID, bits: 1 to 9, 0 = no node (PROVISIONAL, OD-07). */
#define PROTO_NODE_BITS 4u

/** Width of a CRC32, bits (T-B2-01). */
#define PROTO_CRC_BITS 32u

/** Width of format_version, bits (PROVISIONAL, OD-07). */
#define PROTO_FORMAT_BITS 2u

/** Width of frame_count and every frame index, bits (VB-4b, VC-3). */
#define PROTO_FRAME_BITS 10u

/** Width of the chunk index, bits (PROVISIONAL, OD-07). */
#define PROTO_CHUNK_BITS 7u

/** Width of the NACK reason code, bits (PROVISIONAL, OD-07). */
#define PROTO_REASON_BITS 2u

/** Width of the SESSION_CONFIG layout, bits (PROVISIONAL, OD-07). */
#define PROTO_LAYOUT_BITS 2u

/** Width of the failure policy, bits (PROVISIONAL, OD-07). */
#define PROTO_POLICY_BITS 1u

/** Width of the readiness and online bitmaps, bits (VC-11b). */
#define PROTO_BITMAP_BITS 9u

/** Width of the coordinator state, bits (PROVISIONAL, OD-07). */
#define PROTO_COORD_STATE_BITS 4u

/** Width of the fault word, bits (LDC-2). */
#define PROTO_FAULT_BITS 32u

/** Width of the orientation, bits (VB-5c). */
#define PROTO_ORIENT_BITS 2u

/** Width of the display state, bits (VB-5c). */
#define PROTO_DISPLAY_STATE_BITS 3u

/** Largest Node ID (CL_MAX_NODES 9, T-B2-03). */
#define PROTO_NODE_MAX 9u

/** Positions in a SESSION_CONFIG Node ID map (3x3, the largest layout). */
#define PROTO_MAP_SLOTS 9u

/** Largest frame_count and frame index (VC-3: 1023 frames). */
#define PROTO_FRAME_MAX 1023u

/** Frame rate of every asset (NFR1, W6D-1e). Not on the wire. */
#define PROTO_FRAME_RATE 60u

/** Packed frame bytes: 64 pixels x 12 bits (LDC-1, T-B1-03). */
#define PROTO_FRAME_BYTES 96u

/** ASSET_CHUNK payload bytes, except the last chunk (T-B2-01). */
#define PROTO_CHUNK_PAYLOAD_MAX 1024u

/** Largest chunk index: the 600-frame 8-bit DE_PROFILE asset (OD-15) is
 *  115,200 B = 113 chunks of 1,024 B; 1023 packed frames need 96. */
#define PROTO_CHUNK_INDEX_MAX 112u

/** Bits of the common header: type, session_id, seq (29). */
#define PROTO_HEADER_BITS                                                      \
    (PROTO_TYPE_BITS + PROTO_SESSION_BITS + PROTO_SEQ_BITS)

/** Bytes needed to read a header (4). */
#define PROTO_HEADER_BYTES PROTO_BYTES(PROTO_HEADER_BITS)

/* Encoded length of each message, bits and bytes (layout_table.md
 * section 4). */
#define PROTO_REGISTER_BITS                                                    \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_SESSION_BITS + PROTO_CRC_BITS)
#define PROTO_REGISTER_BYTES  PROTO_BYTES(PROTO_REGISTER_BITS)
#define PROTO_HEARTBEAT_BITS  (PROTO_HEADER_BITS + PROTO_NODE_BITS)
#define PROTO_HEARTBEAT_BYTES PROTO_BYTES(PROTO_HEARTBEAT_BITS)
#define PROTO_SESSION_CONFIG_BITS                                              \
    (PROTO_HEADER_BITS + PROTO_LAYOUT_BITS + PROTO_POLICY_BITS +               \
     (PROTO_MAP_SLOTS * PROTO_NODE_BITS))
#define PROTO_SESSION_CONFIG_BYTES PROTO_BYTES(PROTO_SESSION_CONFIG_BITS)
#define PROTO_MANIFEST_BITS                                                    \
    (PROTO_HEADER_BITS + PROTO_FORMAT_BITS + PROTO_FRAME_BITS + PROTO_CRC_BITS)
#define PROTO_MANIFEST_BYTES   PROTO_BYTES(PROTO_MANIFEST_BITS)
#define PROTO_CHUNK_HEAD_BYTES PROTO_BYTES(PROTO_HEADER_BITS + PROTO_CHUNK_BITS)
#define PROTO_ASSET_CHUNK_MAX_BYTES                                            \
    (PROTO_CHUNK_HEAD_BYTES + PROTO_CHUNK_PAYLOAD_MAX)
#define PROTO_CHUNK_ACK_BITS                                                   \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_CHUNK_BITS)
#define PROTO_CHUNK_ACK_BYTES PROTO_BYTES(PROTO_CHUNK_ACK_BITS)
#define PROTO_NACK_BITS                                                        \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_REASON_BITS + PROTO_SEQ_BITS)
#define PROTO_NACK_BYTES PROTO_BYTES(PROTO_NACK_BITS)
#define PROTO_BUSY_BITS  (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_SEQ_BITS)
#define PROTO_BUSY_BYTES PROTO_BYTES(PROTO_BUSY_BITS)
#define PROTO_ASSET_READY_BITS                                                 \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_CRC_BITS)
#define PROTO_ASSET_READY_BYTES PROTO_BYTES(PROTO_ASSET_READY_BITS)
#define PROTO_SYNC_READY_BITS   (PROTO_HEADER_BITS + PROTO_NODE_BITS)
#define PROTO_SYNC_READY_BYTES  PROTO_BYTES(PROTO_SYNC_READY_BITS)
#define PROTO_BEACON_BITS       (PROTO_HEADER_BITS + PROTO_TIME_BITS)
#define PROTO_BEACON_BYTES      PROTO_BYTES(PROTO_BEACON_BITS)
#define PROTO_START_AT_BITS     (PROTO_HEADER_BITS + PROTO_TIME_BITS)
#define PROTO_START_AT_BYTES    PROTO_BYTES(PROTO_START_AT_BITS)
#define PROTO_RESUME_AT_BITS                                                   \
    (PROTO_HEADER_BITS + PROTO_TIME_BITS + PROTO_FRAME_BITS)
#define PROTO_RESUME_AT_BYTES PROTO_BYTES(PROTO_RESUME_AT_BITS)
#define PROTO_PAUSE_BYTES     PROTO_BYTES(PROTO_HEADER_BITS)
#define PROTO_STOP_BYTES      PROTO_BYTES(PROTO_HEADER_BITS)
#define PROTO_STATUS_BITS                                                      \
    (PROTO_HEADER_BITS + PROTO_BITMAP_BITS + PROTO_BITMAP_BITS +               \
     PROTO_COORD_STATE_BITS)
#define PROTO_STATUS_BYTES PROTO_BYTES(PROTO_STATUS_BITS)
#define PROTO_TELEMETRY_BITS                                                   \
    (PROTO_HEADER_BITS + PROTO_NODE_BITS + PROTO_FAULT_BITS +                  \
     PROTO_ORIENT_BITS + PROTO_DISPLAY_STATE_BITS)
#define PROTO_TELEMETRY_BYTES     PROTO_BYTES(PROTO_TELEMETRY_BITS)
#define PROTO_TELEMETRY_ACK_BITS  (PROTO_HEADER_BITS + PROTO_SEQ_BITS)
#define PROTO_TELEMETRY_ACK_BYTES PROTO_BYTES(PROTO_TELEMETRY_ACK_BITS)

/** Message type codes, in the order of the T-B2-01 message rows
 *  (PROVISIONAL, OD-07). */
typedef enum
{
    PROTO_TYPE_REGISTER = 0,
    PROTO_TYPE_HEARTBEAT,
    PROTO_TYPE_SESSION_CONFIG,
    PROTO_TYPE_MANIFEST,
    PROTO_TYPE_ASSET_CHUNK,
    PROTO_TYPE_CHUNK_ACK,
    PROTO_TYPE_NACK,
    PROTO_TYPE_BUSY,
    PROTO_TYPE_ASSET_READY,
    PROTO_TYPE_SYNC_READY,
    PROTO_TYPE_BEACON,
    PROTO_TYPE_START_AT,
    PROTO_TYPE_RESUME_AT,
    PROTO_TYPE_PAUSE,
    PROTO_TYPE_STOP,
    PROTO_TYPE_STATUS,
    PROTO_TYPE_TELEMETRY,
    PROTO_TYPE_TELEMETRY_ACK,
    PROTO_TYPE_COUNT
} proto_type_t;

/** SESSION_CONFIG layout (FR3; OD-21 may add a 1x2 strip as code 3). */
typedef enum
{
    PROTO_LAYOUT_1X1 = 0,
    PROTO_LAYOUT_2X2,
    PROTO_LAYOUT_3X3,
    PROTO_LAYOUT_COUNT
} proto_layout_t;

/** Failure policy (FR18, T-B2-04). */
typedef enum
{
    PROTO_POLICY_CONTINUE = 0,
    PROTO_POLICY_PAUSE,
    PROTO_POLICY_COUNT
} proto_policy_t;

/** MANIFEST format_version (VC-5; PROVISIONAL numbering, OD-07 brief). */
typedef enum
{
    PROTO_FORMAT_UNKNOWN_TEST = 0, /**< Known to no firmware (test vector). */
    PROTO_FORMAT_8BIT,             /**< 192 B frames, DE_PROFILE only. */
    PROTO_FORMAT_PACKED,           /**< 96 B frames, 4 bits per channel. */
    PROTO_FORMAT_5BIT,             /**< Reserved (VC-12, deferred). */
    PROTO_FORMAT_COUNT
} proto_format_t;

/** NACK reason codes (PROVISIONAL, OD-07 brief: one per cited reason). */
typedef enum
{
    PROTO_NACK_UNKNOWN_FORMAT = 0, /**< VC-5. */
    PROTO_NACK_WRONG_LENGTH,       /**< VC-5. */
    PROTO_NACK_COMMAND_QUEUE_FULL, /**< VA-3.6, T-B3-03. */
    PROTO_NACK_UPLOAD_QUEUE_FULL,  /**< VA-3.6, T-B3-05. */
    PROTO_NACK_COUNT
} proto_nack_reason_t;

/** Coordinator states, W6 statechart 2 (T-B2-04), as STATUS carries them. */
typedef enum
{
    PROTO_COORD_BOOTING = 0,
    PROTO_COORD_CONNECTING_TO_ROUTER,
    PROTO_COORD_WAITING_FOR_SESSION,
    PROTO_COORD_VALIDATING_SESSION,
    PROTO_COORD_WAITING_FOR_NODES,
    PROTO_COORD_WAITING_FOR_TILE_READINESS,
    PROTO_COORD_SYNCHRONISING,
    PROTO_COORD_READY,
    PROTO_COORD_COORDINATING_PLAYBACK,
    PROTO_COORD_PAUSED,
    PROTO_COORD_RECOVERING,
    PROTO_COORD_FAULT,
    PROTO_COORD_COUNT
} proto_coord_state_t;

/** Display states, W6 statechart 5 (T-B5-03), as TELEMETRY carries them. */
typedef enum
{
    PROTO_DISPLAY_INITIALISING = 0,
    PROTO_DISPLAY_IDLE,
    PROTO_DISPLAY_PREPARING_FRAME,
    PROTO_DISPLAY_TRANSMITTING_PIXELS,
    PROTO_DISPLAY_WAITING_FOR_NEXT_TICK,
    PROTO_DISPLAY_PAUSED,
    PROTO_DISPLAY_FAULT,
    PROTO_DISPLAY_COUNT
} proto_display_state_t;

/** Orientation quadrants (IFM-6a). */
typedef enum
{
    PROTO_ORIENT_0 = 0,
    PROTO_ORIENT_90,
    PROTO_ORIENT_180,
    PROTO_ORIENT_270,
    PROTO_ORIENT_COUNT
} proto_orientation_t;

/** Common header on every message (IFM-2a, W6D-2f). */
typedef struct
{
    uint8_t  type;       /**< proto_type_t code, 5 bits. */
    uint8_t  session_id; /**< 8 bits. */
    uint16_t seq;        /**< 16 bits. */
} proto_header_t;

/** REGISTER, tile to Pico (T-B2-01, T-B3-05). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id;           /**< 1 to 9. */
    uint8_t        cached_session_id; /**< Session of the cached asset. */
    uint32_t       cached_crc32;      /**< CRC32 of the cached asset. */
} proto_register_t;

/** HEARTBEAT, tile to Pico (T-B3-03). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9. */
} proto_heartbeat_t;

/** SESSION_CONFIG, PC to Pico (T-B2-04, T-B1-05). */
typedef struct
{
    proto_header_t header;
    uint8_t        layout;                    /**< proto_layout_t. */
    uint8_t        policy;                    /**< proto_policy_t. */
    uint8_t        node_map[PROTO_MAP_SLOTS]; /**< Row-major, 0 = blank. */
} proto_session_config_t;

/** MANIFEST, PC to tile (VC-5, IFM-1a). Session is header.session_id. */
typedef struct
{
    proto_header_t header;
    uint8_t        format_version; /**< proto_format_t. */
    uint16_t       frame_count;    /**< 1 to 1023. */
    uint32_t       crc32;          /**< zlib CRC32 of this tile's payload. */
} proto_manifest_t;

/** ASSET_CHUNK, PC to tile (T-B2-01, LDC-8). */
typedef struct
{
    proto_header_t  header;
    uint8_t         chunk_index; /**< 0 to 112. */
    uint16_t        payload_len; /**< 1 to 1,024 bytes. */
    uint8_t const * p_payload;   /**< Encode: source; decode: points into
                                      the received buffer. */
} proto_asset_chunk_t;

/** CHUNK_ACK, tile to PC (T-B3-05). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id;     /**< 1 to 9. */
    uint8_t        chunk_index; /**< 0 to 112. */
} proto_chunk_ack_t;

/** NACK, tile to sender (VC-5, VA-3.6). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9. */
    uint8_t        reason;  /**< proto_nack_reason_t. */
    uint16_t       ref_seq; /**< seq of the rejected message. */
} proto_nack_t;

/** BUSY, tile to PC (SRM-9, T-B3-04). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9. */
    uint16_t       ref_seq; /**< seq of the refused chunk or manifest. */
} proto_busy_t;

/** ASSET_READY {session_id, CRC32}, tile to Pico (W6D-7a). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9. */
    uint32_t       crc32;   /**< CRC32 of the validated payload. */
} proto_asset_ready_t;

/** SYNC_READY, tile to Pico (OD-10 rule on the tile). */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id; /**< 1 to 9. */
} proto_sync_ready_t;

/** BEACON {session_id, seq, t_p} (T-B2-01, T-B2-02). */
typedef struct
{
    proto_header_t header; /**< header.type is PROTO_TYPE_BEACON. */
    uint64_t       t_p;    /**< Pico time_us_64() read just before sending. */
} proto_beacon_t;

/** START_AT {t_start}, Pico to tiles (T-B2-05). */
typedef struct
{
    proto_header_t header;
    uint64_t       t_start; /**< Timeline origin, Pico time, microseconds. */
} proto_start_at_t;

/** RESUME_AT {t_resume, saved frame index}, Pico to tiles (T-B2-05). */
typedef struct
{
    proto_header_t header;
    uint64_t       t_resume;    /**< Pico time, microseconds. */
    uint16_t       frame_index; /**< 0 to 1023, shown at t_resume. */
} proto_resume_at_t;

/** PAUSE or STOP, Pico to tiles (T-B2-05): header only. */
typedef struct
{
    proto_header_t header;
} proto_command_t;

/** STATUS, Pico to PC (T-B2-04). */
typedef struct
{
    proto_header_t header;
    uint16_t       readiness; /**< Bit n-1 = Node ID n ready, 9 bits. */
    uint16_t       online;    /**< Bit n-1 = Node ID n online, 9 bits. */
    uint8_t        state;     /**< proto_coord_state_t. */
} proto_status_t;

/** TELEMETRY {seq, fault word, orientation, display state}, tile to PC
 *  (VC-4). The telemetry seq is header.seq. */
typedef struct
{
    proto_header_t header;
    uint8_t        node_id;       /**< 1 to 9. */
    uint32_t       fault_word;    /**< LDC-2. */
    uint8_t        orientation;   /**< proto_orientation_t. */
    uint8_t        display_state; /**< proto_display_state_t. */
} proto_telemetry_t;

/** TELEMETRY_ACK {seq}, PC to tile (VC-4). */
typedef struct
{
    proto_header_t header;
    uint16_t       acked_seq; /**< header.seq of the acknowledged TELEMETRY. */
} proto_telemetry_ack_t;

/**
 * @brief  Write the low width bits of value at bit offset bit_off. Bit 0 is
 *         the least significant bit of p_buf[0], so a field wider than 8 bits
 *         is little-endian. Other bits of the touched bytes are kept.
 * @param  p_buf    Buffer of at least (bit_off + width + 7) / 8 bytes.
 * @param  bit_off  First bit.
 * @param  width    1 to 64.
 * @param  value    Value; bits above width are ignored.
 */
void proto_put_bits (uint8_t * p_buf, uint32_t bit_off, uint32_t width,
                     uint64_t value);

/**
 * @brief  Read width bits at bit offset bit_off (inverse of proto_put_bits).
 * @param  p_buf    Buffer of at least (bit_off + width + 7) / 8 bytes.
 * @param  bit_off  First bit.
 * @param  width    1 to 64.
 * @return The field value.
 */
uint64_t proto_get_bits (uint8_t const * p_buf, uint32_t bit_off,
                         uint32_t width);

/**
 * @brief  Write a 16-bit value little-endian at a byte position.
 * @param  p_buf  Buffer; p_buf[0] receives the low byte.
 * @param  value  Value.
 */
void proto_put_u16_le (uint8_t * p_buf, uint16_t value);

/**
 * @brief  Write a 32-bit value little-endian at a byte position.
 * @param  p_buf  Buffer; p_buf[0] receives the low byte.
 * @param  value  Value.
 */
void proto_put_u32_le (uint8_t * p_buf, uint32_t value);

/**
 * @brief  Write a 64-bit value little-endian at a byte position.
 * @param  p_buf  Buffer; p_buf[0] receives the low byte.
 * @param  value  Value.
 */
void proto_put_u64_le (uint8_t * p_buf, uint64_t value);

/**
 * @brief  Read a little-endian 16-bit value at a byte position.
 * @param  p_buf  Buffer.
 * @return The value.
 */
uint16_t proto_get_u16_le (uint8_t const * p_buf);

/**
 * @brief  Read a little-endian 32-bit value at a byte position.
 * @param  p_buf  Buffer.
 * @return The value.
 */
uint32_t proto_get_u32_le (uint8_t const * p_buf);

/**
 * @brief  Read a little-endian 64-bit value at a byte position.
 * @param  p_buf  Buffer.
 * @return The value.
 */
uint64_t proto_get_u64_le (uint8_t const * p_buf);

/**
 * @brief  Read the common header, so a receiver can dispatch on type before
 *         calling the matching proto_decode_ function.
 * @param  p_buf     Received bytes.
 * @param  len       Received length, bytes.
 * @param  p_header  Receives the header when the result is true.
 * @return true when len holds a header and the type is a known code.
 */
bool proto_decode_header (uint8_t const * p_buf, uint32_t len,
                          proto_header_t * p_header);

/*
 * Encode and decode, one pair per message row. Every encoder writes the type
 * code of its row whatever p_msg->header.type holds, zeroes the padding bits
 * and returns the bytes written, or 0 when p_buf is too small or a field is
 * outside its valid range. Every decoder returns true only when the length
 * is exactly the row's length (ASSET_CHUNK: header plus 1 to 1,024 payload
 * bytes), the type matches and every field is inside its valid range; the
 * session_id and seq checks belong to the receiver.
 */
uint32_t proto_encode_register (proto_register_t const * p_msg, uint8_t * p_buf,
                                uint32_t buf_len);
bool     proto_decode_register (uint8_t const * p_buf, uint32_t len,
                                proto_register_t * p_msg);
uint32_t proto_encode_heartbeat (proto_heartbeat_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_heartbeat (uint8_t const * p_buf, uint32_t len,
                                 proto_heartbeat_t * p_msg);
uint32_t proto_encode_session_config (proto_session_config_t const * p_msg,
                                      uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_session_config (uint8_t const * p_buf, uint32_t len,
                                      proto_session_config_t * p_msg);
uint32_t proto_encode_manifest (proto_manifest_t const * p_msg, uint8_t * p_buf,
                                uint32_t buf_len);
bool     proto_decode_manifest (uint8_t const * p_buf, uint32_t len,
                                proto_manifest_t * p_msg);
uint32_t proto_encode_asset_chunk (proto_asset_chunk_t const * p_msg,
                                   uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_asset_chunk (uint8_t const * p_buf, uint32_t len,
                                   proto_asset_chunk_t * p_msg);
uint32_t proto_encode_chunk_ack (proto_chunk_ack_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_chunk_ack (uint8_t const * p_buf, uint32_t len,
                                 proto_chunk_ack_t * p_msg);
uint32_t proto_encode_nack (proto_nack_t const * p_msg, uint8_t * p_buf,
                            uint32_t buf_len);
bool     proto_decode_nack (uint8_t const * p_buf, uint32_t len,
                            proto_nack_t * p_msg);
uint32_t proto_encode_busy (proto_busy_t const * p_msg, uint8_t * p_buf,
                            uint32_t buf_len);
bool     proto_decode_busy (uint8_t const * p_buf, uint32_t len,
                            proto_busy_t * p_msg);
uint32_t proto_encode_asset_ready (proto_asset_ready_t const * p_msg,
                                   uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_asset_ready (uint8_t const * p_buf, uint32_t len,
                                   proto_asset_ready_t * p_msg);
uint32_t proto_encode_sync_ready (proto_sync_ready_t const * p_msg,
                                  uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_sync_ready (uint8_t const * p_buf, uint32_t len,
                                  proto_sync_ready_t * p_msg);
uint32_t proto_encode_beacon (proto_beacon_t const * p_msg, uint8_t * p_buf,
                              uint32_t buf_len);
bool     proto_decode_beacon (uint8_t const * p_buf, uint32_t len,
                              proto_beacon_t * p_msg);
uint32_t proto_encode_start_at (proto_start_at_t const * p_msg, uint8_t * p_buf,
                                uint32_t buf_len);
bool     proto_decode_start_at (uint8_t const * p_buf, uint32_t len,
                                proto_start_at_t * p_msg);
uint32_t proto_encode_resume_at (proto_resume_at_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_resume_at (uint8_t const * p_buf, uint32_t len,
                                 proto_resume_at_t * p_msg);
uint32_t proto_encode_pause (proto_command_t const * p_msg, uint8_t * p_buf,
                             uint32_t buf_len);
bool     proto_decode_pause (uint8_t const * p_buf, uint32_t len,
                             proto_command_t * p_msg);
uint32_t proto_encode_stop (proto_command_t const * p_msg, uint8_t * p_buf,
                            uint32_t buf_len);
bool     proto_decode_stop (uint8_t const * p_buf, uint32_t len,
                            proto_command_t * p_msg);
uint32_t proto_encode_status (proto_status_t const * p_msg, uint8_t * p_buf,
                              uint32_t buf_len);
bool     proto_decode_status (uint8_t const * p_buf, uint32_t len,
                              proto_status_t * p_msg);
uint32_t proto_encode_telemetry (proto_telemetry_t const * p_msg,
                                 uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_telemetry (uint8_t const * p_buf, uint32_t len,
                                 proto_telemetry_t * p_msg);
uint32_t proto_encode_telemetry_ack (proto_telemetry_ack_t const * p_msg,
                                     uint8_t * p_buf, uint32_t buf_len);
bool     proto_decode_telemetry_ack (uint8_t const * p_buf, uint32_t len,
                                     proto_telemetry_ack_t * p_msg);

/**
 * @brief  Overwrite only the t_p field of an encoded BEACON, so the sender can
 *         read time_us_64() after everything else is encoded (T-B2-02: t_p is
 *         read immediately before udp_sendto).
 * @param  p_buf  An encoded BEACON of PROTO_BEACON_BYTES bytes.
 * @param  t_p    Pico time, microseconds.
 */
void proto_beacon_set_t_p (uint8_t * p_buf, uint64_t t_p);

#endif /* PROTO_H */

/*** end of file ***/
