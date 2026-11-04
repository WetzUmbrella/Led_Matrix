/**
 * @file    coord_lock.h
 * @brief   Coordinator lock (R1-5, R1-6): which Pico a tile follows. Pure
 *          bookkeeping with no ESP-IDF dependency, so it compiles on the
 *          host for unit tests.
 *
 * Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston.
 * Moved out of tile_net.c unchanged, except that commands are refused while
 * nothing is locked (R1-5).
 *
 * A tile locks the first coordinator it hears. Beacons from any other
 * address are counted and refused until the locked one has been silent for
 * COORD_LOCK_SWITCH_US; then the next sender is locked.
 */
#ifndef COORD_LOCK_H
#define COORD_LOCK_H

#include <stdbool.h>
#include <stdint.h>

/** A tile keeps the first coordinator it hears and ignores beacons from
 *  any other address until the locked one has been silent this long
 *  (PROVISIONAL, owners Cheston and Benjamin: 2 s, twice the 1 s holdover,
 *  so a Pico reboot is followed but a second Pico on the SSID never mixes
 *  its clock in; bench log 29 Oct 2026). */
#define COORD_LOCK_SWITCH_US INT64_C(2000000)

/** The lock state. addr 0 means nothing is locked. */
typedef struct
{
    uint32_t addr;    /**< Locked coordinator address, 0 = none. */
    int64_t  last_us; /**< Local time of its last beacon. */
    uint32_t foreign; /**< Beacons refused from other senders. */
} coord_lock_t;

/** What a beacon did to the lock. */
typedef enum
{
    COORD_LOCK_ACCEPT,   /**< From the locked sender: use it. */
    COORD_LOCK_SWITCHED, /**< A new sender was locked: reset the seq baseline;
                              use the beacon. */
    COORD_LOCK_FOREIGN   /**< From another sender: counted and refused. */
} coord_lock_result_t;

/**
 * @brief  Initialise the lock: nothing locked, no foreign beacons.
 * @param  p_lock  The lock.
 */
void coord_lock_init (coord_lock_t * p_lock);

/**
 * @brief  Apply one beacon to the lock.
 * @param  p_lock    The lock.
 * @param  src_addr  Sender address.
 * @param  t_l       Local receive time (us).
 * @return COORD_LOCK_ACCEPT, COORD_LOCK_SWITCHED or COORD_LOCK_FOREIGN.
 */
coord_lock_result_t coord_lock_beacon (coord_lock_t * p_lock, uint32_t src_addr,
                                       int64_t t_l);

/**
 * @brief  May a command from this sender be accepted? Not while nothing is
 *         locked (R1-5).
 * @param  p_lock    The lock.
 * @param  src_addr  Sender address.
 * @return true when a coordinator is locked and src_addr is it.
 */
bool coord_lock_command_ok (coord_lock_t const * p_lock, uint32_t src_addr);

#endif /* COORD_LOCK_H */

/*** end of file ***/
