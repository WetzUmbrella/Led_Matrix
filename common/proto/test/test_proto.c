/**
 * @file    test_proto.c
 * @brief   Host tests of common/proto (T-B2-01, beacon row).
 *
 * Implemented by Ahsanul (B4) for Benjamin, task T-B2-01. Bit packing and
 * the BEACON reference bytes; every other row is in test_messages.c and the
 * cross-language vectors are pc/tests/test_cross_lang.py (T-B1-01).
 */
#include "test_proto.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "test_main.h"
#include "proto.h"

#define BUF_BYTES    16u
#define MAX_OFFSET   8u
#define MAX_WIDTH    64u
#define FILL_BYTE    0xA5u
#define PATTERN_BITS UINT64_C(0x0123456789ABCDEF)
#define BOUNDARIES   2u

/** Reference bytes: type 10, session_id 0x5A, seq 0x1234,
 *  t_p 0x0123456789ABCDEF, packed as one little-endian integer
 *  type | session << 5 | seq << 13 | t_p << 29, computed in Python with
 *  int.to_bytes(12, "little"), independently of proto.c. */
static uint8_t const g_ref_mid[PROTO_BEACON_BYTES] = {
    0x4Au, 0x8Bu, 0x46u, 0xE2u, 0xBDu, 0x79u,
    0x35u, 0xF1u, 0xACu, 0x68u, 0x24u, 0x00u};

/** Reference bytes with every field at its maximum (same method). */
static uint8_t const g_ref_max[PROTO_BEACON_BYTES] = {
    0xEAu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu,
    0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0x1Fu};

static bool bytes_equal (uint8_t const * p_left, uint8_t const * p_right,
                         uint32_t count);
static bool round_trip (proto_beacon_t const * p_msg);

/**
 * @brief  Bit packing round trip.
 * @return true when every check passed.
 */
bool test_proto_bits (void)
{
    bool     b_ok   = true;
    uint32_t checks = 0u;

    for (uint32_t offset = 0u; offset < MAX_OFFSET; offset++)
    {
        for (uint32_t width = 1u; width <= MAX_WIDTH; width++)
        {
            uint8_t buf[BUF_BYTES];

            for (uint32_t idx = 0u; idx < BUF_BYTES; idx++)
            {
                buf[idx] = FILL_BYTE;
            }

            proto_put_bits(buf, offset, width, PATTERN_BITS);

            uint64_t mask = UINT64_MAX;

            if (width < MAX_WIDTH)
            {
                mask = (UINT64_C(1) << width) - 1u;
            }

            b_ok = (test_main_check((PATTERN_BITS & mask) ==
                                        proto_get_bits(buf, offset, width),
                                    __LINE__)) &&
                   b_ok;

            /* The bits below the field and above it keep the fill. */
            uint8_t const fill = FILL_BYTE;

            if (offset > 0u)
            {
                b_ok = (test_main_check(proto_get_bits(&fill, 0u, offset) ==
                                            proto_get_bits(buf, 0u, offset),
                                        __LINE__)) &&
                       b_ok;
            }

            uint32_t const end = offset + width;

            b_ok =
                (test_main_check(FILL_BYTE == buf[BUF_BYTES - 1u], __LINE__)) &&
                b_ok;
            b_ok = (test_main_check(
                       proto_get_bits(&fill, end % 8u, 8u - (end % 8u)) ==
                           proto_get_bits(buf, end, 8u - (end % 8u)),
                       __LINE__)) &&
                   b_ok;
            checks++;
        }
    }

    printf("    %" PRIu32 " width and offset pairs round-tripped\n", checks);

    return b_ok;
}

/**
 * @brief  BEACON reference bytes and boundary set.
 * @return true when every check passed.
 */
bool test_proto_beacon (void)
{
    uint8_t buf[PROTO_BEACON_BYTES];
    bool    b_ok = true;

    proto_beacon_t const mid = {
        .header = {.type = 0u, .session_id = 0x5Au, .seq = 0x1234u},
        .t_p    = PATTERN_BITS,
    };
    proto_beacon_t const max = {
        .header = {.type = 0u, .session_id = 0xFFu, .seq = 0xFFFFu},
        .t_p    = UINT64_MAX,
    };

    b_ok = (test_main_check(PROTO_BEACON_BYTES ==
                                proto_encode_beacon(&mid, buf, sizeof(buf)),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(bytes_equal(buf, g_ref_mid, PROTO_BEACON_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(PROTO_BEACON_BYTES ==
                                proto_encode_beacon(&max, buf, sizeof(buf)),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(bytes_equal(buf, g_ref_max, PROTO_BEACON_BYTES),
                            __LINE__)) &&
           b_ok;
    /* Setting t_p after encoding gives the same bytes as encoding it. */
    proto_beacon_t mid_zero = mid;

    mid_zero.t_p = 0u;
    (void)proto_encode_beacon(&mid_zero, buf, sizeof(buf)); /* Size known. */
    proto_beacon_set_t_p(buf, mid.t_p);
    b_ok = (test_main_check(bytes_equal(buf, g_ref_mid, PROTO_BEACON_BYTES),
                            __LINE__)) &&
           b_ok;
    printf("    93 bits, %" PRIu32 " bytes; reference vectors match\n",
           PROTO_BEACON_BYTES);

    /* T-B2-01 boundary set for the beacon: t_p 0 and 2^64 - 1, with
     * session_id and seq at 0 and their maximum. */
    static uint64_t const t_p_set[BOUNDARIES]     = {0u, UINT64_MAX};
    static uint8_t const  session_set[BOUNDARIES] = {0u, 0xFFu};
    static uint16_t const seq_set[BOUNDARIES]     = {0u, 0xFFFFu};

    uint32_t cases      = 0u;
    uint32_t mismatches = 0u;

    for (uint32_t t_idx = 0u; t_idx < BOUNDARIES; t_idx++)
    {
        for (uint32_t s_idx = 0u; s_idx < BOUNDARIES; s_idx++)
        {
            for (uint32_t q_idx = 0u; q_idx < BOUNDARIES; q_idx++)
            {
                proto_beacon_t const msg = {
                    .header = {.type       = 0u,
                               .session_id = session_set[s_idx],
                               .seq        = seq_set[q_idx]},
                    .t_p    = t_p_set[t_idx],
                };

                bool const b_same = round_trip(&msg);

                b_ok = (test_main_check(b_same, __LINE__)) && b_ok;
                cases++;

                if (!b_same)
                {
                    mismatches++;
                }
            }
        }
    }

    printf("    %" PRIu32 " boundary cases, %" PRIu32 " mismatches\n", cases,
           mismatches);

    return b_ok;
}

/**
 * @brief  BEACON rejections.
 * @return true when every check passed.
 */
bool test_proto_beacon_reject (void)
{
    uint8_t        buf[PROTO_BEACON_BYTES + 1u];
    proto_beacon_t decoded;
    bool           b_ok = true;

    proto_beacon_t const msg = {
        .header = {.type = 0u, .session_id = 1u, .seq = 2u},
        .t_p    = 3u,
    };

    b_ok = (test_main_check(
               0u == proto_encode_beacon(&msg, buf, PROTO_BEACON_BYTES - 1u),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(PROTO_BEACON_BYTES ==
                                proto_encode_beacon(&msg, buf, sizeof(buf)),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               !proto_decode_beacon(buf, PROTO_BEACON_BYTES - 1u, &decoded),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               !proto_decode_beacon(buf, PROTO_BEACON_BYTES + 1u, &decoded),
               __LINE__)) &&
           b_ok;

    /* Change the type field to START_AT: no longer a beacon. */
    proto_put_bits(buf, 0u, PROTO_TYPE_BITS, PROTO_TYPE_START_AT);
    b_ok = (test_main_check(
               !proto_decode_beacon(buf, PROTO_BEACON_BYTES, &decoded),
               __LINE__)) &&
           b_ok;
    printf("    short buffer, 11 B, 13 B and type START_AT rejected\n");

    return b_ok;
}

/**
 * @brief  Encode, decode and compare one beacon.
 * @param  p_msg  Message (its type field is ignored by the encoder).
 * @return true when the decoded fields equal the input.
 */
static bool round_trip (proto_beacon_t const * p_msg)
{
    uint8_t        buf[PROTO_BEACON_BYTES];
    proto_beacon_t decoded;

    uint32_t const len       = proto_encode_beacon(p_msg, buf, sizeof(buf));
    bool const     b_decoded = proto_decode_beacon(buf, len, &decoded);

    /* PROTO_TYPE_BEACON is 10, which fits uint8_t. */
    return b_decoded && ((uint8_t)PROTO_TYPE_BEACON == decoded.header.type) &&
           (p_msg->header.session_id == decoded.header.session_id) &&
           (p_msg->header.seq == decoded.header.seq) &&
           (p_msg->t_p == decoded.t_p);
}

/**
 * @brief  Compare two byte arrays.
 * @param  p_left   First array.
 * @param  p_right  Second array.
 * @param  count    Bytes to compare.
 * @return true when all count bytes are equal.
 */
static bool bytes_equal (uint8_t const * p_left, uint8_t const * p_right,
                         uint32_t count)
{
    bool b_equal = true;

    for (uint32_t idx = 0u; idx < count; idx++)
    {
        if (p_left[idx] != p_right[idx])
        {
            b_equal = false;
        }
    }

    return b_equal;
}

/*** end of file ***/
