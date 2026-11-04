/**
 * @file    test_coord_lock.h
 * @brief   Host tests of coord_lock (R1-5, R1-6), run by test_main.c.
 *
 * Implemented by Ahsanul (B4) for Cheston.
 */
#ifndef TEST_COORD_LOCK_H
#define TEST_COORD_LOCK_H

#include <stdbool.h>

/**
 * @brief  Test L1: the first beacon locks its sender (SWITCHED), a repeat
 *         is ACCEPTed and a foreign sender is counted and refused.
 * @return true when every check passed.
 */
bool test_coord_lock_first (void);

/**
 * @brief  Test L2: after 1.9 s of silence the locked sender is still
 *         locked; a foreign sender after 2.1 s is SWITCHED.
 * @return true when every check passed.
 */
bool test_coord_lock_switch (void);

/**
 * @brief  Test L3: commands are refused while unlocked, accepted from the
 *         locked address and refused from others (R1-5).
 * @return true when every check passed.
 */
bool test_coord_lock_commands (void);

/**
 * @brief  Test L4: a local time near INT64_MAX / 2 does not overflow.
 * @return true when every check passed.
 */
bool test_coord_lock_large_time (void);

#endif /* TEST_COORD_LOCK_H */

/*** end of file ***/
