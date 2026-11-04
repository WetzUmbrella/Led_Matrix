/**
 * @file    tap_ring.c
 * @brief   pico_tap edge ring. See tap_ring.h.
 *
 * Owner: Ahsanul (Buddy 4). Bench tool, not shipped firmware.
 *
 * No Pico SDK includes: this module is host-testable.
 */
#include "tap_ring.h"

/**
 * @brief  Empty the ring and clear the drop count.
 * @param  p_ring  The ring.
 */
void tap_ring_init (tap_ring_t * p_ring)
{
    p_ring->head    = 0u;
    p_ring->tail    = 0u;
    p_ring->dropped = 0u;
}

/**
 * @brief  Producer: add one event, or count it as dropped when full. The
 *         slot is filled before head moves, and both are volatile, so the
 *         consumer never sees a half-written slot.
 * @param  p_ring  The ring.
 * @param  t_us    Timer value.
 * @param  mask    Channel levels.
 * @return true when the event was stored.
 */
bool tap_ring_push (tap_ring_t * p_ring, uint32_t t_us, uint32_t mask)
{
    bool           b_stored = false;
    uint32_t const head     = p_ring->head;

    if ((head - p_ring->tail) < TAP_RING_CAPACITY)
    {
        p_ring->events[head & TAP_RING_INDEX_MASK].t_us = t_us;
        p_ring->events[head & TAP_RING_INDEX_MASK].mask = mask;
        p_ring->head                                    = head + 1u;
        b_stored                                        = true;
    }
    else
    {
        p_ring->dropped = p_ring->dropped + 1u;
    }

    return b_stored;
}

/**
 * @brief  Consumer: take the oldest event.
 * @param  p_ring   The ring.
 * @param  p_event  Receives the event.
 * @return true when an event was taken.
 */
bool tap_ring_pop (tap_ring_t * p_ring, tap_event_t * p_event)
{
    bool           b_taken = false;
    uint32_t const tail    = p_ring->tail;

    if (tail != p_ring->head)
    {
        p_event->t_us = p_ring->events[tail & TAP_RING_INDEX_MASK].t_us;
        p_event->mask = p_ring->events[tail & TAP_RING_INDEX_MASK].mask;
        p_ring->tail  = tail + 1u;
        b_taken       = true;
    }

    return b_taken;
}

/*** end of file ***/
