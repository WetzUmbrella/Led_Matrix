/**
 * @file    test_join_backoff.h
 * @brief   Host test of the router join retry schedule (C15).
 *
 * Owner: Benjamin (Buddy 2), coordinator.
 */
#ifndef TEST_JOIN_BACKOFF_H
#define TEST_JOIN_BACKOFF_H

#include <stdbool.h>

/**
 * @brief  C15: the schedule is 1, 2, 4, 8, then 10 s for every later retry,
 *         never above 10 s, even for a very large retry count.
 * @return true when every check passed.
 */
bool test_join_backoff_schedule (void);

#endif /* TEST_JOIN_BACKOFF_H */

/*** end of file ***/
