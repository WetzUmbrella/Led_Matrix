/**
 * @file    join_backoff.c
 * @brief   Router join retry schedule (see join_backoff.h).
 *
 * Owner: Benjamin (Buddy 2), coordinator.
 *
 * The schedule doubles from JOIN_BACKOFF_FIRST_MS until it reaches
 * JOIN_BACKOFF_MAX_MS and stays there. The loop runs at most four times, so
 * a very large retry count costs the same as a small one.
 */
#include "join_backoff.h"

/** The first wait, milliseconds (plan choice: the Pico retries quickly
 *  after a short router restart). */
#define JOIN_BACKOFF_FIRST_MS 1000u

/**
 * @brief  Wait before the next router join attempt.
 * @param  retry  Failed attempts before this wait: 0 gives the first wait.
 * @return The wait, milliseconds, at most JOIN_BACKOFF_MAX_MS.
 */
uint32_t join_backoff_ms (uint32_t retry)
{
    uint32_t wait_ms = JOIN_BACKOFF_FIRST_MS;
    uint32_t step    = 0u;

    while ((step < retry) && (wait_ms < JOIN_BACKOFF_MAX_MS))
    {
        wait_ms *= 2u; /* Below 2 x JOIN_BACKOFF_MAX_MS, so no overflow. */
        step++;
    }

    if (wait_ms > JOIN_BACKOFF_MAX_MS)
    {
        wait_ms = JOIN_BACKOFF_MAX_MS;
    }

    return wait_ms;
}

/*** end of file ***/
