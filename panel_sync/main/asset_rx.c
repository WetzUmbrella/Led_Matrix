/**
 * @file    asset_rx.c
 * @brief   Asset upload checks (T-B3-05). See asset_rx.h.
 *
 * Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston, task
 * T-B3-05.
 *
 * No ESP-IDF includes: this module is host-testable.
 */
#include "asset_rx.h"

#include <string.h>

/** Shipped packed format (layout_table.md section 5.4). */
#define AR_FORMAT_PACKED 2u

/** 8-bit format of the DE_PROFILE build (section 5.4, OD-15). */
#define AR_FORMAT_8BIT 1u

/** Largest frame_count on the wire (10 bits, VB-4b). */
#define AR_FRAME_MAX 1023u

/** Bits per chunk-map word. */
#define AR_MAP_BITS 32u

/** zlib CRC32 reflected polynomial. */
#define AR_CRC_POLY UINT32_C(0xEDB88320)

/** CRC bits processed per byte. */
#define AR_BYTE_BITS 8u

static uint32_t ar_frame_bytes (uint8_t format_version);

/**
 * @brief  No upload open.
 * @param  p_rx  The upload state.
 */
void asset_rx_init (asset_rx_t * p_rx)
{
    (void)memset(p_rx, 0, sizeof(*p_rx)); /* Returns p_rx. */
    p_rx->b_open = false;
}

/**
 * @brief  Check a manifest without changing any state.
 * @param  format_version  From the MANIFEST.
 * @param  frame_count     From the MANIFEST.
 * @param  b_allow_8bit    true only in the DE_PROFILE build.
 * @return ASSET_RX_MANIFEST_OK, or the NACK reason to send.
 */
asset_rx_manifest_result_t asset_rx_check_manifest (uint8_t  format_version,
                                                    uint16_t frame_count,
                                                    bool     b_allow_8bit)
{
    asset_rx_manifest_result_t result = ASSET_RX_MANIFEST_OK;

    bool const b_known = (AR_FORMAT_PACKED == format_version) ||
                         ((AR_FORMAT_8BIT == format_version) && (b_allow_8bit));

    if (!b_known)
    {
        result = ASSET_RX_BAD_FORMAT;
    }
    else if ((0u == frame_count) || (frame_count > AR_FRAME_MAX))
    {
        result = ASSET_RX_BAD_LENGTH;
    }
    else
    {
        /* Widening to uint32_t: the value is unchanged. */
        uint32_t const len =
            ((uint32_t)frame_count) * ar_frame_bytes(format_version);

        /* The payload must fit the 113 chunk indices on the wire. */
        if (len > (ASSET_RX_CHUNK_MAX * ASSET_RX_CHUNK_BYTES))
        {
            result = ASSET_RX_BAD_LENGTH;
        }
    }

    return result;
}

/**
 * @brief  Open an upload: every chunk is missing.
 * @param  p_rx            The upload state.
 * @param  session_id      Manifest header session_id.
 * @param  format_version  1 or 2.
 * @param  frame_count     1 to 1023.
 * @param  crc32           Manifest CRC32.
 */
void asset_rx_open (asset_rx_t * p_rx, uint8_t session_id,
                    uint8_t format_version, uint16_t frame_count,
                    uint32_t crc32)
{
    asset_rx_init(p_rx);
    p_rx->b_open         = true;
    p_rx->session_id     = session_id;
    p_rx->format_version = format_version;
    p_rx->frame_count    = frame_count;
    p_rx->crc32          = crc32;
    /* Widening to uint32_t: the value is unchanged. */
    p_rx->payload_len =
        ((uint32_t)frame_count) * ar_frame_bytes(format_version);
    p_rx->chunk_count = ((p_rx->payload_len + ASSET_RX_CHUNK_BYTES) - 1u) /
                        ASSET_RX_CHUNK_BYTES;
}

/**
 * @brief  Classify one chunk and, when new, record it as held.
 * @param  p_rx         The upload state.
 * @param  session_id   Chunk header session_id.
 * @param  chunk_index  0 to 112.
 * @param  len          Payload bytes in the datagram.
 * @return What to do with the chunk.
 */
asset_rx_chunk_result_t asset_rx_chunk (asset_rx_t * p_rx, uint8_t session_id,
                                        uint8_t chunk_index, uint32_t len)
{
    asset_rx_chunk_result_t result = ASSET_RX_CHUNK_IGNORED;

    if ((p_rx->b_open) && (session_id == p_rx->session_id))
    {
        if ((chunk_index >= p_rx->chunk_count) ||
            (len != asset_rx_chunk_len(p_rx, chunk_index)))
        {
            result = ASSET_RX_CHUNK_BAD_LENGTH;
        }
        else
        {
            uint32_t const index = chunk_index; /* Widened, unchanged. */
            uint32_t const word  = index / AR_MAP_BITS;
            uint32_t const bit   = UINT32_C(1) << (index % AR_MAP_BITS);

            if (0u != (p_rx->map[word] & bit))
            {
                result = ASSET_RX_CHUNK_REPEAT;
            }
            else
            {
                p_rx->map[word] |= bit;
                p_rx->received_count++;
                result = ASSET_RX_CHUNK_NEW;
            }
        }
    }

    return result;
}

/**
 * @brief  Whether every chunk is held.
 * @param  p_rx  The upload state.
 * @return true when the upload is open and complete.
 */
bool asset_rx_complete (asset_rx_t const * p_rx)
{
    return ((p_rx->b_open) && (p_rx->chunk_count > 0u) &&
            (p_rx->received_count == p_rx->chunk_count));
}

/**
 * @brief  Payload bytes a chunk must carry.
 * @param  p_rx         The upload state.
 * @param  chunk_index  Chunk number.
 * @return 1,024, the remainder for the last chunk, or 0 if out of range.
 */
uint32_t asset_rx_chunk_len (asset_rx_t const * p_rx, uint8_t chunk_index)
{
    uint32_t len = 0u;

    uint32_t const index = chunk_index; /* Widened, unchanged. */

    if (index < p_rx->chunk_count)
    {
        uint32_t const start = index * ASSET_RX_CHUNK_BYTES;
        uint32_t const left  = p_rx->payload_len - start;

        len = (left < ASSET_RX_CHUNK_BYTES) ? left : ASSET_RX_CHUNK_BYTES;
    }

    return len;
}

/**
 * @brief  zlib CRC32, continued over several buffers (bitwise, no table:
 *         it runs once per upload and once per boot, not in the 60 Hz path).
 * @param  crc    CRC so far (0 at the start).
 * @param  p_buf  Next bytes.
 * @param  len    Their number.
 * @return CRC of everything so far.
 */
uint32_t asset_rx_crc32 (uint32_t crc, uint8_t const * p_buf, uint32_t len)
{
    uint32_t value = ~crc;

    for (uint32_t idx = 0u; idx < len; idx++)
    {
        value ^= (uint32_t)p_buf[idx]; /* Widened, unchanged. */

        for (uint32_t bit = 0u; bit < AR_BYTE_BITS; bit++)
        {
            uint32_t const mask = (0u != (value & 1u)) ? AR_CRC_POLY : 0u;

            value = (value >> 1u) ^ mask;
        }
    }

    return ~value;
}

/**
 * @brief  Bytes per frame of a known format.
 * @param  format_version  1 or 2.
 * @return 192 for the 8-bit format, otherwise 96.
 */
static uint32_t ar_frame_bytes (uint8_t format_version)
{
    return (AR_FORMAT_8BIT == format_version) ? ASSET_RX_8BIT_FRAME_BYTES
                                              : ASSET_RX_PACKED_FRAME_BYTES;
}

/*** end of file ***/
