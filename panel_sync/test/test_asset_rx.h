/**
 * @file    test_asset_rx.h
 * @brief   Host tests of asset_rx (T-B3-05), run by test_main.c.
 *
 * Implemented by Ahsanul (B4) for Cheston, task T-B3-05.
 */
#ifndef TEST_ASSET_RX_H
#define TEST_ASSET_RX_H

#include <stdbool.h>

/**
 * @brief  Test A1: manifest rules. format_version 2 is accepted, 0 and 3
 *         are unknown, 1 only in the DE_PROFILE build; frame_count 0 and an
 *         8-bit asset above 113 chunks are wrong lengths.
 * @return true when every check passed.
 */
bool test_asset_rx_manifest (void);

/**
 * @brief  Test A2: a 600-frame packed asset is 57,600 B in 57 chunks, the
 *         last 256 B (derived, T-B3-05 row).
 * @return true when every check passed.
 */
bool test_asset_rx_chunk_count (void);

/**
 * @brief  Test A3: chunk classes. New, repeat, another session (ignored),
 *         index past the last chunk and wrong size (wrong length), and any
 *         chunk with no upload open (ignored).
 * @return true when every check passed.
 */
bool test_asset_rx_chunk_classes (void);

/**
 * @brief  Test A4: complete only when all 57 chunks are held, in any order
 *         and with repeats.
 * @return true when every check passed.
 */
bool test_asset_rx_complete (void);

/**
 * @brief  Test A6: a held upload (the asset is already published, DS-12 B):
 *         every chunk is a repeat, and it is never complete.
 * @return true when every check passed.
 */
bool test_asset_rx_held (void);

/**
 * @brief  Test A5: zlib CRC32 check value 0xCBF43926 for "123456789", and a
 *         CRC continued over two parts equals the CRC of the whole.
 * @return true when every check passed.
 */
bool test_asset_rx_crc32 (void);

#endif /* TEST_ASSET_RX_H */

/*** end of file ***/
