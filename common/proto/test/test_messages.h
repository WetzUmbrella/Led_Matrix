/**
 * @file    test_messages.h
 * @brief   Host tests of every message row of common/proto (T-B2-01).
 *
 * Implemented by Ahsanul (B4) for Benjamin, task T-B2-01.
 */
#ifndef TEST_MESSAGES_H
#define TEST_MESSAGES_H

#include <stdbool.h>

/**
 * @brief  Boundary set round trip for every message row.
 * @return true when there are 0 mismatches.
 */
bool test_messages_boundary (void);

/**
 * @brief  Each encoder returns the length in layout_table.md section 4.
 * @return true when every length matches.
 */
bool test_messages_lengths (void);

/**
 * @brief  Out-of-range fields, wrong types and wrong lengths are refused.
 * @return true when every refusal happened.
 */
bool test_messages_reject (void);

/**
 * @brief  Byte-aligned little-endian helpers.
 * @return true when the bytes and values match.
 */
bool test_messages_le (void);

/**
 * @brief  SESSION_CONFIG cells (step 4d): sparse grids accepted; a shared
 *         cell, a cell outside the grid or on a tile not placed refused.
 * @return true when every check passed.
 */
bool test_messages_cells (void);

#endif /* TEST_MESSAGES_H */

/*** end of file ***/
