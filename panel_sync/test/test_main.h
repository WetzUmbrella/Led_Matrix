/**
 * @file    test_main.h
 * @brief   Host test runner: check and run functions shared by the test
 *          modules in panel_sync/test (Barr-C 4.2.a: the header of
 *          test_main.c).
 *
 * Owner: Ahsanul (Buddy 4). Build and run: make -C panel_sync/test
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

/**
 * @brief  Run one test: print its name, run it, print PASS or FAIL.
 * @param  p_name  Name printed before the test.
 * @param  p_test  The test.
 * @return true when the test passed.
 */
bool test_main_run (char const * p_name, test_main_fn_t p_test);

#endif /* TEST_MAIN_H */

/*** end of file ***/
