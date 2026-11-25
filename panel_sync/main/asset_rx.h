/**
 * @file    asset_rx.h
 * @brief   Asset upload checks (T-B3-05): manifest rules, the chunk map and
 *          the zlib CRC32. Pure C with no ESP-IDF dependency, so the rules
 *          run in the host tests; asset_store owns the only instance.
 *
 * Owner: Cheston (Buddy 3), task T-B3-05. Implemented by Ahsanul (B4) for
 * Cheston, written 29 Oct 2026 in Jia Jun's (B5) session.
 *
 * Rules from the T-B3-05 row and protocol/layout_table.md (MANIFEST,
 * ASSET_CHUNK, sections 5.4 and 5.5):
 * - format_version 2 (packed 4-bit, 96 B frames) is the only shipped
 *   format; format_version 1 (8-bit, 192 B frames) is accepted only when
 *   the caller allows it (DE_PROFILE build, OD-15); 0 and 3 are unknown.
 * - payload length = frame_count x frame bytes; chunks are 1,024 B, the
 *   last one the remainder (a 600-frame packed asset: 57 chunks, the last
 *   256 B, derived); a chunk index beyond the last chunk or a chunk of the
 *   wrong size is a wrong payload length (NACK reason 1).
 * - a chunk whose session_id differs from the manifest's is ignored.
 * - each chunk is recorded once; a repeat is acknowledged again but not
 *   written again.
 * - a manifest for the asset already published opens a held upload (DS-12
 *   B, 12 Nov 2026): every chunk counts as held, so each one the PC sends
 *   is acknowledged as a repeat and never written, and the upload is never
 *   complete (nothing to verify).
 */
#ifndef ASSET_RX_H
#define ASSET_RX_H

#include <stdbool.h>
#include <stdint.h>

/** Chunk size, 1,024 B (LDC-8, layout_table.md ASSET_CHUNK). */
#define ASSET_RX_CHUNK_BYTES 1024u

/** Highest chunk index on the wire plus one (0 to 112, 7 bits). */
#define ASSET_RX_CHUNK_MAX 113u

/** Bytes per packed frame: 64 LEDs x 12 bits / 8 (LDC-1). */
#define ASSET_RX_PACKED_FRAME_BYTES 96u

/** Bytes per 8-bit frame of the DE_PROFILE build: 64 x 3 (OD-15). */
#define ASSET_RX_8BIT_FRAME_BYTES 192u

/** Words in the chunk map: ceil(113 / 32) = 4 (derived). */
#define ASSET_RX_MAP_WORDS 4u

/** Result of a manifest check. */
typedef enum
{
    ASSET_RX_MANIFEST_OK = 0,
    ASSET_RX_BAD_FORMAT, /**< NACK reason 0, unknown format_version. */
    ASSET_RX_BAD_LENGTH  /**< NACK reason 1, wrong payload length. */
} asset_rx_manifest_result_t;

/** Result of one chunk. */
typedef enum
{
    ASSET_RX_CHUNK_NEW = 0,    /**< First copy: write it, then ACK. */
    ASSET_RX_CHUNK_REPEAT,     /**< Already held: ACK again, no write. */
    ASSET_RX_CHUNK_BAD_LENGTH, /**< Index or size wrong: NACK reason 1. */
    ASSET_RX_CHUNK_IGNORED     /**< No upload open, or another session. */
} asset_rx_chunk_result_t;

/** One upload in progress (or none). */
typedef struct
{
    bool     b_open;                  /**< A manifest was accepted. */
    bool     b_held;                  /**< Open for the published asset. */
    uint8_t  session_id;              /**< Manifest header session_id. */
    uint8_t  format_version;          /**< 1 or 2. */
    uint16_t frame_count;             /**< 1 to 1023. */
    uint32_t crc32;                   /**< Manifest CRC32. */
    uint32_t payload_len;             /**< frame_count x frame bytes. */
    uint32_t chunk_count;             /**< ceil(payload_len / 1,024). */
    uint32_t received_count;          /**< Distinct chunks held. */
    uint32_t map[ASSET_RX_MAP_WORDS]; /**< Bit i = chunk i held. */
} asset_rx_t;

/**
 * @brief  No upload open.
 * @param  p_rx  The upload state.
 */
void asset_rx_init (asset_rx_t * p_rx);

/**
 * @brief  Check a manifest without changing any state.
 * @param  format_version  From the MANIFEST.
 * @param  frame_count     From the MANIFEST.
 * @param  b_allow_8bit    true only in the DE_PROFILE build (OD-15).
 * @return ASSET_RX_MANIFEST_OK, or the NACK reason to send.
 */
asset_rx_manifest_result_t asset_rx_check_manifest (uint8_t  format_version,
                                                    uint16_t frame_count,
                                                    bool     b_allow_8bit);

/**
 * @brief  Open an upload for a manifest that passed the check: every chunk
 *         is missing.
 * @param  p_rx            The upload state.
 * @param  session_id      Manifest header session_id.
 * @param  format_version  1 or 2.
 * @param  frame_count     1 to 1023.
 * @param  crc32           Manifest CRC32.
 */
void asset_rx_open (asset_rx_t * p_rx, uint8_t session_id,
                    uint8_t format_version, uint16_t frame_count,
                    uint32_t crc32);

/**
 * @brief  Open a held upload for a manifest that names the asset already
 *         published (DS-12 B): every chunk is held, so asset_rx_chunk()
 *         answers ASSET_RX_CHUNK_REPEAT and asset_rx_complete() stays false.
 * @param  p_rx            The upload state.
 * @param  session_id      Manifest header session_id.
 * @param  format_version  1 or 2.
 * @param  frame_count     1 to 1023.
 * @param  crc32           Manifest CRC32.
 */
void asset_rx_open_held (asset_rx_t * p_rx, uint8_t session_id,
                         uint8_t format_version, uint16_t frame_count,
                         uint32_t crc32);

/**
 * @brief  Classify one chunk and, when new, record it as held.
 * @param  p_rx         The upload state.
 * @param  session_id   Chunk header session_id.
 * @param  chunk_index  0 to 112.
 * @param  len          Payload bytes in the datagram.
 * @return What to do with the chunk.
 */
asset_rx_chunk_result_t asset_rx_chunk (asset_rx_t * p_rx, uint8_t session_id,
                                        uint8_t chunk_index, uint32_t len);

/**
 * @brief  Whether every chunk of the open upload is held.
 * @param  p_rx  The upload state.
 * @return true when the upload is open and complete; never for a held
 *         upload (asset_rx_open_held()).
 */
bool asset_rx_complete (asset_rx_t const * p_rx);

/**
 * @brief  Payload bytes chunk_index must carry.
 * @param  p_rx         The upload state (open).
 * @param  chunk_index  Below chunk_count.
 * @return 1,024, or the remainder for the last chunk; 0 if out of range.
 */
uint32_t asset_rx_chunk_len (asset_rx_t const * p_rx, uint8_t chunk_index);

/**
 * @brief  zlib CRC32 (reflected, polynomial 0xEDB88320, initial and final
 *         XOR 0xFFFFFFFF), continued over several buffers: start with 0 and
 *         pass each result back in.
 * @param  crc    CRC of the bytes so far (0 at the start).
 * @param  p_buf  Next bytes.
 * @param  len    Their number.
 * @return CRC of everything so far.
 */
uint32_t asset_rx_crc32 (uint32_t crc, uint8_t const * p_buf, uint32_t len);

#endif /* ASSET_RX_H */

/*** end of file ***/
