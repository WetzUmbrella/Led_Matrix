/**
 * @file    test_join_backoff.c
 * @brief   Host test of join_backoff_ms(): the retry schedule the Pico uses
 *          to rejoin the router (C15).
 *
 * Owner: Benjamin (Buddy 2), coordinator.
 */
#include "test_join_backoff.h"

#include <stdint.h>

#include "join_backoff.h"
#include "test_main.h"

/** Expected waits for retries 0 to 7, milliseconds (the plan's schedule). */
static uint32_t const g_expected_ms[8] = {1000u,  2000u,  4000u,  8000u,
                                          10000u, 10000u, 10000u, 10000u};

/**
 * @brief  C15: the schedule is 1, 2, 4, 8, then 10 s for every later retry,
 *         never above 10 s, even for a very large retry count.
 * @return true when every check passed.
 */
bool test_join_backoff_schedule (void)
{
    bool b_ok = true;

    for (uint32_t retry = 0u; retry < 8u; retry++)
    {
        b_ok = (test_main_check(join_backoff_ms(retry) == g_expected_ms[retry],
                                __LINE__)) &&
               b_ok;
    }

    b_ok = (test_main_check(JOIN_BACKOFF_MAX_MS == join_backoff_ms(UINT32_MAX),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(JOIN_BACKOFF_MAX_MS == join_backoff_ms(1000000u),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/*** end of file ***/
