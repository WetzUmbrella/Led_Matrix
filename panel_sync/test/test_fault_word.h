/**
 * @file    test_fault_word.h
 * @brief   Host tests of fault_word (T-B3-06), run by test_main.c.
 *
 * Implemented by Ahsanul (B4) for Cheston, task T-B3-06.
 */
#ifndef TEST_FAULT_WORD_H
#define TEST_FAULT_WORD_H

#include <stdbool.h>

/**
 * @brief  Test F1: bits raised by fault_word_set() appear in
 *         fault_word_snapshot(), and setting a bit twice leaves one bit.
 * @return true when every check passed.
 */
bool test_fault_word_set_snapshot (void);

/**
 * @brief  Test F2: fault_word_clear_events() clears only event bits that
 *         were in the acknowledged packet; condition bits and unsent event
 *         bits stay.
 * @return true when every check passed.
 */
bool test_fault_word_ack_events_only (void);

/**
 * @brief  Test F3: condition bits survive any number of ACKs and clear only
 *         through fault_word_clear_condition(), which ignores event bits.
 * @return true when every check passed.
 */
bool test_fault_word_condition_owner (void);

/**
 * @brief  Test F4: VC-4 merge rule. An event sent, raised again before the
 *         ACK, is cleared by that ACK (one report); an event first raised
 *         after the snapshot stays for the next packet.
 * @return true when every check passed.
 */
bool test_fault_word_merge_rule (void);

/**
 * @brief  Test F5: host forerunner of T-X-02 (not T-X-02 itself). Two setter
 *         threads, each with its own event bit, set it 100,000 times, each
 *         time waiting until it has been cleared; a reporter thread
 *         snapshots and clears on simulated ACKs. Every set appears in
 *         exactly one snapshot: 0 lost.
 * @return true when every check passed.
 */
bool test_fault_word_two_threads (void);

#endif /* TEST_FAULT_WORD_H */

/*** end of file ***/
