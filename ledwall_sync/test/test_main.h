/**
 * @file    test_main.h
 * @brief   Host test runner for ledwall_sync: check and run functions shared
 *          by the test modules (Barr-C 4.2.a: the header of test_main.c).
 *
 * Owner: Benjamin (Buddy 2). Implemented by Ahsanul (B4) for Benjamin.
 * Build and run: make -C ledwall_sync/test
 */
#ifndef TEST_MAIN_H
#define TEST_MAIN_H

#include <stdbool.h>
#include <stdint.h>

/** A test: returns true when every check in it passed. */
typedef bool (*test_main_fn_t)(void);

/**
 * @brief  Record one check. Prints the line number when it fails.
 * @param  b_condition  The checked condition.
 * @param  line         __LINE__ of the check.
 * @return b_condition.
 */
bool test_main_check (bool b_condition, int32_t line);

#endif /* TEST_MAIN_H */

/*** end of file ***/
