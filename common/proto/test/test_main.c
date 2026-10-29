/**
 * @file    test_main.c
 * @brief   Host test runner for common/proto (T-B2-01, beacon row).
 *
 * Minimal implementation by Buddy 4 for T-B4-03. Owner: Benjamin, task
 * T-B2-01. Implemented: runner for the beacon-row tests. Not yet
 * implemented: tests for the other message rows.
 * Build and run: make -C common/proto/test
 */
#include "test_main.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "test_proto.h"

/** One test. */
typedef struct
{
    char const *   p_name;
    test_main_fn_t p_test;
} test_case_t;

static test_case_t const g_cases[] = {
    {"bits: put/get round trip, widths 1 to 64, offsets 0 to 7",
     test_proto_bits},
    {"beacon: reference bytes and T-B2-01 boundary set", test_proto_beacon},
    {"beacon: short buffer, wrong length and wrong type rejected",
     test_proto_beacon_reject},
};

/** Number of tests. */
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
 * @brief  Run every test.
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

    printf("Pass = %" PRIu32 " of %" PRIu32 "\n", passed, CASE_COUNT);

    /* DEVIATION D-2: the exit status is the int that main returns. */
    int status = 1;

    if (CASE_COUNT == passed)
    {
        status = 0;
    }

    return status;
}

/*** end of file ***/
