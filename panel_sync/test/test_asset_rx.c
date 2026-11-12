/**
 * @file    test_asset_rx.c
 * @brief   Host tests of asset_rx (T-B3-05), run by test_main.c.
 *
 * Implemented by Ahsanul (B4) for Cheston, task T-B3-05. Build and run:
 * make -C panel_sync/test.
 */
#include "test_asset_rx.h"

#include <stddef.h>
#include <stdint.h>

#include "test_main.h"
#include "../main/asset_rx.h"

/** Frames in the Week 13 asset (VB-4b). */
#define A_FRAMES 600u

/** Its payload: 600 x 96 B (LDC-1). */
#define A_PAYLOAD 57600u

/** Its chunks: ceil(57,600 / 1,024) (derived). */
#define A_CHUNKS 57u

/** Its last chunk: 57,600 - 56 x 1,024 (derived). */
#define A_LAST_LEN 256u

/** Session used by the tests. */
#define A_SESSION 7u

/** Shipped and 8-bit format_version values (layout_table.md 5.4). */
#define A_FMT_PACKED 2u
#define A_FMT_8BIT   1u

/** Stride that visits all 57 chunks once in a scrambled order (coprime). */
#define A_STRIDE 7u

/**
 * @brief  Test A1: manifest rules.
 * @return true when every check passed.
 */
bool test_asset_rx_manifest (void)
{
    bool b_ok = true;

    b_ok = (test_main_check(
               ASSET_RX_MANIFEST_OK ==
                   asset_rx_check_manifest(A_FMT_PACKED, A_FRAMES, false),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(ASSET_RX_BAD_FORMAT ==
                                asset_rx_check_manifest(0u, A_FRAMES, true),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(ASSET_RX_BAD_FORMAT ==
                                asset_rx_check_manifest(3u, A_FRAMES, true),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               ASSET_RX_BAD_FORMAT ==
                   asset_rx_check_manifest(A_FMT_8BIT, A_FRAMES, false),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               ASSET_RX_MANIFEST_OK ==
                   asset_rx_check_manifest(A_FMT_8BIT, A_FRAMES, true),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               ASSET_RX_BAD_LENGTH ==
                   asset_rx_check_manifest(A_FMT_8BIT, A_FRAMES + 3u, true),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(ASSET_RX_BAD_LENGTH == asset_rx_check_manifest(
                                                       A_FMT_PACKED, 0u, false),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               ASSET_RX_MANIFEST_OK ==
                   asset_rx_check_manifest(A_FMT_PACKED, 1023u, false),
               __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Test A2: 600 frames, 57 chunks, last 256 B.
 * @return true when every check passed.
 */
bool test_asset_rx_chunk_count (void)
{
    bool       b_ok = true;
    asset_rx_t upload;

    asset_rx_open(&upload, A_SESSION, A_FMT_PACKED, A_FRAMES, 0u);

    b_ok = (test_main_check(A_PAYLOAD == upload.payload_len, __LINE__)) && b_ok;
    b_ok = (test_main_check(A_CHUNKS == upload.chunk_count, __LINE__)) && b_ok;
    b_ok = (test_main_check(ASSET_RX_CHUNK_BYTES ==
                                asset_rx_chunk_len(&upload, 0u),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(A_LAST_LEN ==
                                asset_rx_chunk_len(&upload, A_CHUNKS - 1u),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(0u == asset_rx_chunk_len(&upload, A_CHUNKS),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Test A3: chunk classes.
 * @return true when every check passed.
 */
bool test_asset_rx_chunk_classes (void)
{
    bool       b_ok = true;
    asset_rx_t upload;

    asset_rx_init(&upload);
    b_ok = (test_main_check(
               ASSET_RX_CHUNK_IGNORED ==
                   asset_rx_chunk(&upload, A_SESSION, 0u, ASSET_RX_CHUNK_BYTES),
               __LINE__)) &&
           b_ok;

    asset_rx_open(&upload, A_SESSION, A_FMT_PACKED, A_FRAMES, 0u);
    b_ok = (test_main_check(
               ASSET_RX_CHUNK_NEW ==
                   asset_rx_chunk(&upload, A_SESSION, 3u, ASSET_RX_CHUNK_BYTES),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(
               ASSET_RX_CHUNK_REPEAT ==
                   asset_rx_chunk(&upload, A_SESSION, 3u, ASSET_RX_CHUNK_BYTES),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(ASSET_RX_CHUNK_IGNORED ==
                                asset_rx_chunk(&upload, A_SESSION + 1u, 4u,
                                               ASSET_RX_CHUNK_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(ASSET_RX_CHUNK_BAD_LENGTH ==
                                asset_rx_chunk(&upload, A_SESSION, A_CHUNKS,
                                               ASSET_RX_CHUNK_BYTES),
                            __LINE__)) &&
           b_ok;
    b_ok =
        (test_main_check(ASSET_RX_CHUNK_BAD_LENGTH ==
                             asset_rx_chunk(&upload, A_SESSION, A_CHUNKS - 1u,
                                            ASSET_RX_CHUNK_BYTES),
                         __LINE__)) &&
        b_ok;
    b_ok = (test_main_check(1u == upload.received_count, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test A4: complete after all chunks, any order, with repeats.
 * @return true when every check passed.
 */
bool test_asset_rx_complete (void)
{
    bool       b_ok = true;
    asset_rx_t upload;

    asset_rx_open(&upload, A_SESSION, A_FMT_PACKED, A_FRAMES, 0u);

    for (uint32_t step = 0u; step < A_CHUNKS; step++)
    {
        /* Below 57, so it fits uint8_t. */
        uint8_t const index = (uint8_t)((step * A_STRIDE) % A_CHUNKS);

        b_ok = (test_main_check(!asset_rx_complete(&upload), __LINE__)) && b_ok;
        /* The class is checked in A3; here only the count matters. */
        (void)asset_rx_chunk(&upload, A_SESSION, index,
                             asset_rx_chunk_len(&upload, index));
        /* Chunk 0 again each step: new once, then a repeat. */
        (void)asset_rx_chunk(&upload, A_SESSION, 0u, ASSET_RX_CHUNK_BYTES);
    }

    b_ok = (test_main_check(asset_rx_complete(&upload), __LINE__)) && b_ok;
    b_ok =
        (test_main_check(A_CHUNKS == upload.received_count, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test A6: a held upload (DS-12 B) answers every chunk as a repeat,
 *         refuses a wrong length or another session as before, and is never
 *         complete.
 * @return true when every check passed.
 */
bool test_asset_rx_held (void)
{
    bool       b_ok = true;
    asset_rx_t upload;

    asset_rx_open_held(&upload, A_SESSION, A_FMT_PACKED, A_FRAMES, 0u);
    b_ok = (test_main_check((upload.b_open) && (upload.b_held) &&
                                (A_CHUNKS == upload.chunk_count) &&
                                (A_CHUNKS == upload.received_count),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(!asset_rx_complete(&upload), __LINE__)) && b_ok;

    for (uint32_t step = 0u; step < A_CHUNKS; step++)
    {
        uint8_t const index = (uint8_t)step; /* Below 57: fits uint8_t. */

        b_ok = (test_main_check(
                   ASSET_RX_CHUNK_REPEAT ==
                       asset_rx_chunk(&upload, A_SESSION, index,
                                      asset_rx_chunk_len(&upload, index)),
                   __LINE__)) && b_ok;
    }

    b_ok = (test_main_check(!asset_rx_complete(&upload), __LINE__)) && b_ok;
    b_ok = (test_main_check(A_CHUNKS == upload.received_count, __LINE__)) &&
           b_ok;
    /* Chunk 57 does not exist; another session is not this upload. */
    uint8_t const past  = (uint8_t)A_CHUNKS;          /* 57 fits. */
    uint8_t const other = (uint8_t)(A_SESSION + 1u);  /* 8 fits. */

    b_ok = (test_main_check(
               ASSET_RX_CHUNK_BAD_LENGTH ==
                   asset_rx_chunk(&upload, A_SESSION, past,
                                  ASSET_RX_CHUNK_BYTES),
               __LINE__)) && b_ok;
    b_ok = (test_main_check(
               ASSET_RX_CHUNK_IGNORED ==
                   asset_rx_chunk(&upload, other, 0u, ASSET_RX_CHUNK_BYTES),
               __LINE__)) && b_ok;

    /* A real manifest afterwards opens a normal upload: nothing held. */
    asset_rx_open(&upload, A_SESSION, A_FMT_PACKED, A_FRAMES, 1u);
    b_ok = (test_main_check((!upload.b_held) && (0u == upload.received_count),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(
               ASSET_RX_CHUNK_NEW == asset_rx_chunk(&upload, A_SESSION, 0u,
                                                    ASSET_RX_CHUNK_BYTES),
               __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test A5: CRC32 check value and continuation.
 * @return true when every check passed.
 */
bool test_asset_rx_crc32 (void)
{
    static uint8_t const digits[] = {'1', '2', '3', '4', '5',
                                     '6', '7', '8', '9'};

    bool           b_ok  = true;
    uint32_t const whole = asset_rx_crc32(0u, digits, sizeof(digits));
    uint32_t const part  = asset_rx_crc32(0u, digits, 4u);

    b_ok = (test_main_check(UINT32_C(0xCBF43926) == whole, __LINE__)) && b_ok;
    b_ok = (test_main_check(
               whole == asset_rx_crc32(part, &digits[4], sizeof(digits) - 4u),
               __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(0u == asset_rx_crc32(0u, digits, 0u), __LINE__)) &&
           b_ok;

    return b_ok;
}

/*** end of file ***/
