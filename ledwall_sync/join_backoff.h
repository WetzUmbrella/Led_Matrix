/**
 * @file    join_backoff.h
 * @brief   Wait between router join attempts on the Pico: 1, 2, 4, 8, then
 *          10 s for every later retry. Pure C with no SDK call, so the
 *          schedule has a host test (ledwall_sync/test, C15).
 *
 * Owner: Benjamin (Buddy 2), coordinator. Written for the whack-a-mole
 * re-plan, build step 7 prerequisite: the Pico rejoins the router after a
 * router restart instead of sitting in FAULT.
 */
#ifndef JOIN_BACKOFF_H
#define JOIN_BACKOFF_H

#include <stdint.h>

/** Longest wait between two router join attempts, milliseconds. */
#define JOIN_BACKOFF_MAX_MS 10000u

/**
 * @brief  Wait before the next router join attempt.
 * @param  retry  Failed attempts before this wait: 0 gives the first wait.
 * @return 1000, 2000, 4000 and 8000 ms for retries 0 to 3, then
 *         JOIN_BACKOFF_MAX_MS for every later retry.
 */
uint32_t join_backoff_ms (uint32_t retry);

#endif /* JOIN_BACKOFF_H */

/*** end of file ***/
