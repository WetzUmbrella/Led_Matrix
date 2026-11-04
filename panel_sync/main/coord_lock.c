/**
 * @file    coord_lock.c
 * @brief   Coordinator lock (R1-5, R1-6). See coord_lock.h.
 *
 * Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston.
 *
 * No ESP-IDF includes: this module is host-testable.
 */
#include "coord_lock.h"

#include <string.h>

/**
 * @brief  Initialise the lock: nothing locked, no foreign beacons.
 * @param  p_lock  The lock.
 */
void coord_lock_init (coord_lock_t * p_lock)
{
    (void)memset(p_lock, 0, sizeof(*p_lock)); /* Returns p_lock. */
}

/**
 * @brief  Apply one beacon to the lock.
 * @param  p_lock    The lock.
 * @param  src_addr  Sender address.
 * @param  t_l       Local receive time (us).
 * @return COORD_LOCK_ACCEPT, COORD_LOCK_SWITCHED or COORD_LOCK_FOREIGN.
 */
coord_lock_result_t coord_lock_beacon (coord_lock_t * p_lock, uint32_t src_addr,
                                       int64_t t_l)
{
    coord_lock_result_t result = COORD_LOCK_ACCEPT;

    bool const b_stale =
        (t_l - p_lock->last_us) > COORD_LOCK_SWITCH_US; /* Silent. */

    if ((0u == p_lock->addr) || (b_stale))
    {
        if (p_lock->addr != src_addr)
        {
            result = COORD_LOCK_SWITCHED; /* New sender: new seq baseline. */
        }

        p_lock->addr = src_addr;
    }

    if (src_addr != p_lock->addr)
    {
        p_lock->foreign++; /* Another coordinator: never mixed in. */
        result = COORD_LOCK_FOREIGN;
    }
    else
    {
        p_lock->last_us = t_l;
    }

    return result;
}

/**
 * @brief  May a command from this sender be accepted? Not while nothing is
 *         locked (R1-5).
 * @param  p_lock    The lock.
 * @param  src_addr  Sender address.
 * @return true when a coordinator is locked and src_addr is it.
 */
bool coord_lock_command_ok (coord_lock_t const * p_lock, uint32_t src_addr)
{
    return (0u != p_lock->addr) && (src_addr == p_lock->addr);
}

/*** end of file ***/
