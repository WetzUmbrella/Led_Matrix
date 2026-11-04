/**
 * @file    tap_ring.h
 * @brief   pico_tap edge ring: single producer (the GPIO interrupt), single
 *          consumer (the main loop), both on core 0 of the RP2040.
 *
 * Owner: Ahsanul (Buddy 4). Bench tool, not shipped firmware: pico_tap
 * turns a spare Pico W into a timestamping edge recorder for the skew tests
 * when no logic analyser is available (bench plan 5 Nov 2026, section 2.2).
 *
 * The producer and the consumer run on the same core, so the shared fields
 * are volatile (coding standard interpretation 1, rule 1.8.c); no atomics
 * and no deviation are needed. No Pico SDK includes: host-testable in
 * tools/pico_tap/test.
 */
#ifndef TAP_RING_H
#define TAP_RING_H

#include <stdbool.h>
#include <stdint.h>

/** Ring capacity in events: a power of two, so the index wraps with a mask.
 *  1,024 events is about 1.7 s of 5 channels at 60 Hz with both edges. */
#define TAP_RING_CAPACITY 1024u

/** Mask that wraps an index into the ring. */
#define TAP_RING_INDEX_MASK (TAP_RING_CAPACITY - 1u)

/** One edge: the low 32 bits of the microsecond timer and the levels of all
 *  channels just after the edge (bit i = channel i). */
typedef struct
{
    uint32_t t_us; /**< time_us_32() at the interrupt, wraps every 71.6 min. */
    uint32_t mask; /**< Channel levels after the edge. */
} tap_event_t;

/** The ring. head is written only by the producer, tail only by the
 *  consumer. */
typedef struct
{
    tap_event_t volatile events[TAP_RING_CAPACITY]; /**< Slots. */
    uint32_t volatile head;    /**< Next slot to write (producer). */
    uint32_t volatile tail;    /**< Next slot to read (consumer). */
    uint32_t volatile dropped; /**< Events lost because the ring was full. */
} tap_ring_t;

/**
 * @brief  Empty the ring and clear the drop count. Call before the
 *         interrupt is enabled.
 * @param  p_ring  The ring.
 */
void tap_ring_init (tap_ring_t * p_ring);

/**
 * @brief  Producer: add one event, or count it as dropped when the ring is
 *         full. Interrupt context.
 * @param  p_ring  The ring.
 * @param  t_us    Timer value.
 * @param  mask    Channel levels.
 * @return true when the event was stored.
 */
bool tap_ring_push (tap_ring_t * p_ring, uint32_t t_us, uint32_t mask);

/**
 * @brief  Consumer: take the oldest event. Main loop only.
 * @param  p_ring   The ring.
 * @param  p_event  Receives the event.
 * @return true when an event was taken.
 */
bool tap_ring_pop (tap_ring_t * p_ring, tap_event_t * p_event);

#endif /* TAP_RING_H */

/*** end of file ***/
