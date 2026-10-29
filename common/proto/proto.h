/**
 * @file    proto.h
 * @brief   Shared wire layout (T-B2-01): explicit shifts and masks, bit 0 is
 *          the least significant bit of byte 0, no wire struct is ever sent.
 *
 * Minimal implementation by Buddy 4 for T-B4-03. Owner: Benjamin, task
 * T-B2-01. Implemented: proto_put_bits(), proto_get_bits(), the common header
 * (type, session_id, seq) and the BEACON row {session_id, seq, t_p}
 * including proto_beacon_set_t_p(), with
 * host tests. Not yet implemented: protocol/layout_table.md, the
 * byte-aligned proto_put_u16_le/u32_le/u64_le and proto_get_*_le helpers,
 * every other message row (REGISTER to TELEMETRY_ACK), the Python twin
 * (T-B1-01) and the test vectors.
 *
 * PROVISIONAL (OD-07, owner Benjamin): field widths from the OD-07 draft in
 * Week10_Kickoff/decision_briefs.md (type 5 bits, session_id 8 bits, seq 16
 * bits), message type codes in the order of the T-B2-01 message rows, and
 * contiguous packing with no padding between fields.
 * PROVISIONAL (plan file names): the plan names the files proto_layout.c/.h;
 * they are proto.c/.h so that the plan's function names (proto_put_bits(),
 * proto_encode_beacon()) carry their module prefix (Barr-C 6.1.i).
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdbool.h>
#include <stdint.h>

/** Width of the message type field, bits (PROVISIONAL, OD-07). */
#define PROTO_TYPE_BITS 5u

/** Width of the session_id field, bits (PROVISIONAL, OD-07). */
#define PROTO_SESSION_BITS 8u

/** Width of the seq field, bits (PROVISIONAL, OD-07). */
#define PROTO_SEQ_BITS 16u

/** Width of every timestamp, bits (LDC-6: 64-bit time). */
#define PROTO_TIME_BITS 64u

/** Bits of the common header: type, session_id, seq. */
#define PROTO_HEADER_BITS                                                      \
    (PROTO_TYPE_BITS + PROTO_SESSION_BITS + PROTO_SEQ_BITS)

/** Bits of a BEACON: header and t_p (93). */
#define PROTO_BEACON_BITS (PROTO_HEADER_BITS + PROTO_TIME_BITS)

/** Bytes of a BEACON on the wire: 93 bits round up to 12 bytes. */
#define PROTO_BEACON_BYTES ((PROTO_BEACON_BITS + 7u) / 8u)

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

/** Common header on every message (IFM-2a, W6D-2f). */
typedef struct
{
    uint8_t  type;       /**< proto_type_t code, 5 bits. */
    uint8_t  session_id; /**< 8 bits. */
    uint16_t seq;        /**< 16 bits. */
} proto_header_t;

/** BEACON {session_id, seq, t_p} (T-B2-01, T-B2-02). */
typedef struct
{
    proto_header_t header; /**< header.type is PROTO_TYPE_BEACON. */
    uint64_t       t_p;    /**< Pico time_us_64() read just before sending. */
} proto_beacon_t;

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
 * @brief  Encode a BEACON. header.type is written as PROTO_TYPE_BEACON
 *         whatever p_msg holds.
 * @param  p_msg    Message.
 * @param  p_buf    Output buffer.
 * @param  buf_len  Size of p_buf, bytes.
 * @return Bytes written (PROTO_BEACON_BYTES), or 0 if p_buf is too small.
 */
uint32_t proto_encode_beacon (proto_beacon_t const * p_msg, uint8_t * p_buf,
                              uint32_t buf_len);

/**
 * @brief  Overwrite only the t_p field of an encoded BEACON, so the sender can
 *         read time_us_64() after everything else is encoded (T-B2-02: t_p is
 *         read immediately before udp_sendto).
 * @param  p_buf  An encoded BEACON of PROTO_BEACON_BYTES bytes.
 * @param  t_p    Pico time, microseconds.
 */
void proto_beacon_set_t_p (uint8_t * p_buf, uint64_t t_p);

/**
 * @brief  Decode a BEACON. Rejects a datagram whose length is not
 *         PROTO_BEACON_BYTES or whose type is not PROTO_TYPE_BEACON. The
 *         session_id and seq checks belong to the receiver.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  p_msg  Receives the message when the result is true.
 * @return true when p_buf holds a BEACON.
 */
bool proto_decode_beacon (uint8_t const * p_buf, uint32_t len,
                          proto_beacon_t * p_msg);

#endif /* PROTO_H */

/*** end of file ***/
