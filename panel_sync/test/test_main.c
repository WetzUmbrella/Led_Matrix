/**
 * @file    test_main.c
 * @brief   Host test runner for panel_sync (T-B4-02).
 *
 * Owner: Ahsanul (Buddy 4). Build and run: make -C panel_sync/test
 * The four counted tests are the four in the T-B4-02 row, with the row's pass
 * criteria. The supplementary checks cover the integer arithmetic ranges
 * documented in timesync.c; they are not part of "Pass = N of 4".
 */
#include "test_main.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "test_timesync.h"

#ifndef TIMESYNC_HOST_TEST
#error "build with -DTIMESYNC_HOST_TEST (the Makefile does)"
#endif

/** One counted test. */
typedef struct
{
    char const *   p_name;
    test_main_fn_t p_test;
} test_case_t;

static test_case_t const g_cases[] = {
    {"Test 1: slew int8_t round trip and limit at -100/+100",
     test_timesync_slew_int8},
    {"Test 2: 16-sample minimum-delay filter, known minimum",
     test_timesync_min_filter},
    {"Test 3: 2 ms offset step absorbed in exactly 20 ticks",
     test_timesync_step_20_ticks},
    {"Test 4: forced writer interleaving, SYNC_STALE after 3 failures",
     test_timesync_seqlock},
};

/** Number of counted tests (4). */
#define CASE_COUNT ((uint32_t)(sizeof(g_cases) / sizeof(g_cases[0])))

/**
 * @brief  Record one check. Prints the line number when it fails.
 * @param  b_condition  The checked condition.
 * @param  line         __LINE__ of the check.
 * @return b_condition.
 */
bool test_main_check (bool b_condition, int32_t line)
{
    if (!b_condition)
    {
        printf("    check failed at line %" PRId32 "\n", line);
    }

    return b_condition;
}

/**
 * @brief  Run one test: print its name, run it, print PASS or FAIL.
 * @param  p_name  Name printed before the test.
 * @param  p_test  The test.
 * @return true when the test passed.
 */
bool test_main_run (char const * p_name, test_main_fn_t p_test)
{
    printf("%s\n", p_name);

    bool const b_passed = p_test();

    printf("  %s\n", b_passed ? "PASS" : "FAIL");

    return b_passed;
}

/**
 * @brief  Run the four counted tests and the supplementary checks.
 * @return 0 when all pass, 1 otherwise.
 */
int main (void) /* DEVIATION D-2: the C standard fixes int main (void). */
{
    uint32_t passed = 0u;

    for (uint32_t idx = 0u; idx < CASE_COUNT; idx++)
    {
        if (test_main_run(g_cases[idx].p_name, g_cases[idx].p_test))
        {
            passed++;
        }
    }

    bool const b_supp =
        test_main_run("Supplementary arithmetic checks (not counted)",
                      test_timesync_supp_arithmetic);

    printf("Pass = %" PRIu32 " of %" PRIu32 "\n", passed, CASE_COUNT);

    /* DEVIATION D-2: the exit status is the int that main returns. */
    int status = 1;

    if ((CASE_COUNT == passed) && b_supp)
    {
        status = 0;
    }

    return status;
}

/*** end of file ***/
