/**
 * @file    fault_word.c
 * @brief   Shared 32-bit tile fault word (T-B3-06).
 *
 * Implemented by Ahsanul (B4) for Cheston, task T-B3-06. Implemented and not
 * yet implemented: see fault_word.h. PROVISIONAL choices: see fault_word.h.
 *
 * Memory order. Every bit is changed by a read-modify-write on the one
 * atomic word, and all read-modify-writes on one atomic object form a single
 * modification order, so no set or clear is ever lost whatever order is used.
 * The order only decides what other memory becomes visible with the bit:
 * - fault_word_set() uses release and fault_word_snapshot() uses acquire, so
 *   a write the raising task made before the set is visible to a task whose
 *   snapshot sees the bit. This orders only writes made before the set: a
 *   counter the raising task keeps incrementing afterwards is still read
 *   concurrently, so any count read by another task (a drop counter, the
 *   missed-deadline counter) must itself be an atomic and is never made
 *   safe by this pairing alone;
 * - the two clears use relaxed: a clear publishes no other data, and the
 *   telemetry seq and sent bits it relies on stay inside the Network Task.
 *
 * DEVIATION D-1 (module-wide): C11 stdatomic.h (LDC-2, VB-3a).
 */
#include "fault_word.h"

#include <stdatomic.h>

/** The fault word: one 32-bit atomic, never a bit-field (LDC-2). */
static _Atomic uint32_t g_fault_word = 0u;

/**
 * @brief  Raise fault bits (atomic fetch-or, release).
 * @param  bits  FAULT_BIT_* values to set.
 */
void fault_word_set (uint32_t bits)
{
    /* The previous word is not needed: the set cannot be lost. */
    (void)atomic_fetch_or_explicit(&g_fault_word, bits, memory_order_release);
}

/**
 * @brief  Clear condition bits (atomic fetch-and, relaxed). Event bits in
 *         bits are ignored, so a condition owner cannot drop an unreported
 *         event.
 * @param  bits  FAULT_BIT_* condition values to clear.
 */
void fault_word_clear_condition (uint32_t bits)
{
    uint32_t const clear = bits & FAULT_CONDITION_MASK;

    /* The previous word is not needed by the owner. */
    (void)atomic_fetch_and_explicit(&g_fault_word, ~clear,
                                    memory_order_relaxed);
}

/**
 * @brief  Read the whole word (acquire).
 * @return The fault word.
 */
uint32_t fault_word_snapshot (void)
{
    return atomic_load_explicit(&g_fault_word, memory_order_acquire);
}

/**
 * @brief  Clear the acknowledged event bits (atomic fetch-and, relaxed). An
 *         event raised again between the snapshot and the ACK is cleared as
 *         well: VC-4 merges repeats between send and ACK into one report.
 * @param  sent_bits  The fault word sent in the acknowledged packet.
 */
void fault_word_clear_events (uint32_t sent_bits)
{
    uint32_t const clear = sent_bits & FAULT_EVENT_MASK;

    /* The previous word is not needed by the Network Task. */
    (void)atomic_fetch_and_explicit(&g_fault_word, ~clear,
                                    memory_order_relaxed);
}

/*** end of file ***/
