/**
 * @file    test_telemetry_book.h
 * @brief   Host tests of telemetry_book and the VC-4 send/ACK rule (T-B3-06),
 *          run by test_main.c.
 *
 * Implemented by Ahsanul (B4) for Cheston, task T-B3-06.
 */
#ifndef TEST_TELEMETRY_BOOK_H
#define TEST_TELEMETRY_BOOK_H

#include <stdbool.h>

/**
 * @brief  Test T1: an ACK of a recorded seq returns that packet's bits and
 *         leaves the other entries waiting.
 * @return true when every check passed.
 */
bool test_telemetry_book_ack_match (void);

/**
 * @brief  Test T2: a repeated ACK and an ACK of a seq never sent both return
 *         false.
 * @return true when every check passed.
 */
bool test_telemetry_book_repeat (void);

/**
 * @brief  Test T3: the ring keeps the last TELEMETRY_BOOK_CAPACITY packets;
 *         the oldest expires when one more is recorded.
 * @return true when every check passed.
 */
bool test_telemetry_book_expiry (void);

/**
 * @brief  Test T4: seq wraps from 65,535 to 0 and both stay distinct.
 * @return true when every check passed.
 */
bool test_telemetry_book_wrap (void);

/**
 * @brief  Test T5: host form of the VC-4 test with the real fault_word. The
 *         receiver drops every third TELEMETRY (no ACK for it) and ACKs the
 *         rest one send late. Over 60 sends every injected event reaches a
 *         received packet, an event clears only after an ACK of a packet
 *         that carried it, and a condition bit held throughout is in every
 *         packet (0 off/on transitions).
 * @return true when every check passed.
 */
bool test_telemetry_book_vc4 (void);

#endif /* TEST_TELEMETRY_BOOK_H */

/*** end of file ***/
