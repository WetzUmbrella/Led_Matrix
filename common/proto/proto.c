/**
 * @file    proto.c
 * @brief   Shared wire layout (T-B2-01): bit packing and the BEACON row.
 *
 * Minimal implementation by Buddy 4 for T-B4-03. Owner: Benjamin, task
 * T-B2-01. Implemented: proto_put_bits(), proto_get_bits(), BEACON encode and
 * decode. Not yet implemented: see proto.h. PROVISIONAL choices: see proto.h.
 */
#include "proto.h"

#include <stddef.h>

#define BITS_PER_BYTE  8u
#define BYTE_MASK      UINT64_C(0xFF)
#define FULL_WIDTH     64u
#define HEADER_TYPE    0u
#define HEADER_SESSION (HEADER_TYPE + PROTO_TYPE_BITS)
#define HEADER_SEQ     (HEADER_SESSION + PROTO_SESSION_BITS)
#define BEACON_T_P     PROTO_HEADER_BITS

static uint64_t width_mask (uint32_t width);
static void     put_header (uint8_t * p_buf, proto_header_t const * p_header);
static void     get_header (uint8_t const * p_buf, proto_header_t * p_header);

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

    if (buf_len >= PROTO_BEACON_BYTES)
    {
        proto_header_t header = p_msg->header;

        /* PROTO_TYPE_BEACON is 10, which fits uint8_t. */
        header.type = (uint8_t)PROTO_TYPE_BEACON;

        for (uint32_t idx = 0u; idx < PROTO_BEACON_BYTES; idx++)
        {
            p_buf[idx] = 0u;
        }

        put_header(p_buf, &header);
        proto_put_bits(p_buf, BEACON_T_P, PROTO_TIME_BITS, p_msg->t_p);
        written = PROTO_BEACON_BYTES;
    }

    return written;
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
    bool b_ok = false;

    if (PROTO_BEACON_BYTES == len)
    {
        proto_header_t header;

        get_header(p_buf, &header);

        if ((uint8_t)PROTO_TYPE_BEACON == header.type) /* Code 10 fits. */
        {
            p_msg->header = header;
            p_msg->t_p    = proto_get_bits(p_buf, BEACON_T_P, PROTO_TIME_BITS);
            b_ok          = true;
        }
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
 * @brief  Write the common header at bit 0.
 * @param  p_buf     Buffer.
 * @param  p_header  Header.
 */
static void put_header (uint8_t * p_buf, proto_header_t const * p_header)
{
    proto_put_bits(p_buf, HEADER_TYPE, PROTO_TYPE_BITS, p_header->type);
    proto_put_bits(p_buf, HEADER_SESSION, PROTO_SESSION_BITS,
                   p_header->session_id);
    proto_put_bits(p_buf, HEADER_SEQ, PROTO_SEQ_BITS, p_header->seq);
}

/**
 * @brief  Read the common header at bit 0.
 * @param  p_buf     Buffer.
 * @param  p_header  Receives the header.
 */
static void get_header (uint8_t const * p_buf, proto_header_t * p_header)
{
    /* Each field is read with its own width (5, 8, 16 bits), so the value
     * fits the member type. */
    p_header->type =
        (uint8_t)proto_get_bits(p_buf, HEADER_TYPE, PROTO_TYPE_BITS);
    /* 8 bits fit uint8_t. */
    p_header->session_id =
        (uint8_t)proto_get_bits(p_buf, HEADER_SESSION, PROTO_SESSION_BITS);
    /* 16 bits fit uint16_t. */
    p_header->seq = (uint16_t)proto_get_bits(p_buf, HEADER_SEQ, PROTO_SEQ_BITS);
}

/*** end of file ***/
