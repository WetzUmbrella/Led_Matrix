/**
 * @file    test_proto.h
 * @brief   Host tests of common/proto (T-B2-01, beacon row), run by
 *          test_main.c.
 *
 * Implemented by Ahsanul (B4) for Benjamin, task T-B2-01. Bit packing and
 * the BEACON reference bytes; every other row is in test_messages.h.
 */
#ifndef TEST_PROTO_H
#define TEST_PROTO_H

#include <stdbool.h>

/**
 * @brief  proto_put_bits() and proto_get_bits() round-trip every width 1 to
 *         64 at every bit offset 0 to 7, and leave neighbouring bits alone.
 * @return true when every check passed.
 */
bool test_proto_bits (void);

/**
 * @brief  BEACON matches reference bytes computed independently as one
 *         little-endian integer, and round-trips the T-B2-01 boundary set.
 * @return true when every check passed.
 */
bool test_proto_beacon (void);

/**
 * @brief  BEACON encode refuses a short buffer; decode rejects a wrong
 *         length and a wrong type.
 * @return true when every check passed.
 */
bool test_proto_beacon_reject (void);

#endif /* TEST_PROTO_H */

/*** end of file ***/
