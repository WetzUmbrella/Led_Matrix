/**
 * @file    telemetry_book.h
 * @brief   Telemetry pending-ACK ring (T-B3-06): pure bookkeeping with no
 *          ESP-IDF dependency, so it compiles on the host for unit tests.
 *
 * Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston.
 * Task T-B3-06.
 *
 * The ring stores (seq, sent_bits) for the last TELEMETRY_BOOK_CAPACITY
 * packets. When a TELEMETRY_ACK arrives the caller looks up the acked_seq;
 * if it matches a valid entry the caller gets the sent_bits back and the
 * entry is invalidated so a duplicate ACK does nothing.
 */
#ifndef TELEMETRY_BOOK_H
#define TELEMETRY_BOOK_H

#include <stdbool.h>
#include <stdint.h>

/** Number of pending entries (PROVISIONAL, OD-11, owner Cheston). */
#define TELEMETRY_BOOK_CAPACITY 4u

/** One pending-ACK record. */
typedef struct
{
    uint16_t seq;       /**< Telemetry seq of the sent packet. */
    uint32_t sent_bits; /**< Fault word snapshot sent in that packet. */
    bool     b_valid;   /**< true while waiting for the ACK. */
} telemetry_book_entry_t;

/** The ring of pending entries. */
typedef struct
{
    telemetry_book_entry_t entries[TELEMETRY_BOOK_CAPACITY];
    uint32_t               next; /**< Index of the next slot to write. */
} telemetry_book_t;

/**
 * @brief  Initialise the ring: every entry invalid, next slot 0.
 * @param  p_book  The ring.
 */
void telemetry_book_init (telemetry_book_t * p_book);

/**
 * @brief  Record a sent packet. Overwrites the oldest slot.
 * @param  p_book     The ring.
 * @param  seq        Telemetry seq of the packet.
 * @param  sent_bits  Fault word snapshot sent in the packet.
 */
void telemetry_book_record (telemetry_book_t * p_book, uint16_t seq,
                            uint32_t sent_bits);

/**
 * @brief  Look up an ACK.
 * @param  p_book      The ring.
 * @param  acked_seq   seq from the TELEMETRY_ACK.
 * @param  p_bits_out  Receives the sent_bits when the result is true.
 * @return true when acked_seq matched a valid entry (now invalidated).
 */
bool telemetry_book_ack (telemetry_book_t * p_book, uint16_t acked_seq,
                         uint32_t * p_bits_out);

#endif /* TELEMETRY_BOOK_H */

/*** end of file ***/
