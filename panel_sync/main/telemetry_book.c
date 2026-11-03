/**
 * @file    telemetry_book.c
 * @brief   Telemetry pending-ACK ring (T-B3-06). See telemetry_book.h.
 *
 * Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston.
 * Task T-B3-06.
 *
 * No ESP-IDF includes: this module is host-testable.
 */
#include "telemetry_book.h"

#include <string.h>

/**
 * @brief  Initialise the ring: every entry invalid, next slot 0.
 * @param  p_book  The ring.
 */
void telemetry_book_init (telemetry_book_t * p_book)
{
    (void)memset(p_book, 0, sizeof(*p_book)); /* Returns p_book. */

    for (uint32_t idx = 0u; idx < TELEMETRY_BOOK_CAPACITY; idx++)
    {
        p_book->entries[idx].b_valid = false;
    }
}

/**
 * @brief  Record a sent packet. Overwrites the oldest slot.
 * @param  p_book     The ring.
 * @param  seq        Telemetry seq of the packet.
 * @param  sent_bits  Fault word snapshot sent in the packet.
 */
void telemetry_book_record (telemetry_book_t * p_book, uint16_t seq,
                            uint32_t sent_bits)
{
    uint32_t const slot = p_book->next;

    p_book->entries[slot].seq       = seq;
    p_book->entries[slot].sent_bits = sent_bits;
    p_book->entries[slot].b_valid   = true;
    p_book->next                    = (slot + 1u) % TELEMETRY_BOOK_CAPACITY;
}

/**
 * @brief  Look up an ACK. If acked_seq matches a valid entry, write its
 *         sent_bits to *p_bits_out and invalidate the entry.
 * @param  p_book      The ring.
 * @param  acked_seq   seq from the TELEMETRY_ACK.
 * @param  p_bits_out  Receives the sent_bits when the result is true.
 * @return true when acked_seq matched a valid entry (now invalidated).
 */
bool telemetry_book_ack (telemetry_book_t * p_book, uint16_t acked_seq,
                         uint32_t * p_bits_out)
{
    bool b_found = false;

    for (uint32_t idx = 0u; idx < TELEMETRY_BOOK_CAPACITY; idx++)
    {
        if ((!b_found) && (p_book->entries[idx].b_valid) &&
            (acked_seq == p_book->entries[idx].seq))
        {
            *p_bits_out                  = p_book->entries[idx].sent_bits;
            p_book->entries[idx].b_valid = false;
            b_found                      = true;
        }
    }

    return b_found;
}

/*** end of file ***/
