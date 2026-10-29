/**
 * @file    test_main.c
 * @brief   Host test runner for panel_sync (T-B4-02, T-B3-06).
 *
 * Owner: Ahsanul (Buddy 4). Build and run: make -C panel_sync/test
 * The four counted tests are the four in the T-B4-02 row, with the row's pass
 * criteria. The supplementary checks cover the integer arithmetic ranges
 * documented in timesync.c; they are not part of "Pass = N of 4". The fault
 * word tests (T-B3-06, implemented by Ahsanul (B4) for Cheston) are counted
 * on their own line, "Fault word: Pass = N of 5".
 */
#include "test_main.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "test_fault_word.h"
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

static test_case_t const g_faults[] = {
    {"Test F1: fault_word_set and fault_word_snapshot",
     test_fault_word_set_snapshot},
    {"Test F2: ACK clears only the sent event bits",
     test_fault_word_ack_events_only},
    {"Test F3: condition bits clear only through their owner",
     test_fault_word_condition_owner},
    {"Test F4: VC-4 merge of repeats between send and ACK",
     test_fault_word_merge_rule},
    {"Test F5: two setters and a reporter, 0 lost (T-X-02 forerunner)",
     test_fault_word_two_threads},
};

/** Number of counted fault word tests (5): the element count of a
 *  five-entry array fits uint32_t. */
#define FAULT_COUNT ((uint32_t)(sizeof(g_faults) / sizeof(g_faults[0])))

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
 * @brief  Run the four counted tests, the supplementary checks and the
 *         five fault word tests.
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
    bool const b_ready = test_main_run("OD-10 SYNC_READY rule (not counted)",
                                       test_timesync_sync_ready);

    printf("Pass = %" PRIu32 " of %" PRIu32 "\n", passed, CASE_COUNT);

    uint32_t fault_passed = 0u;

    for (uint32_t idx = 0u; idx < FAULT_COUNT; idx++)
    {
        if (test_main_run(g_faults[idx].p_name, g_faults[idx].p_test))
        {
            fault_passed++;
        }
    }

    printf("Fault word: Pass = %" PRIu32 " of %" PRIu32 "\n", fault_passed,
           FAULT_COUNT);

    /* DEVIATION D-2: the exit status is the int that main returns. */
    int status = 1;

    if ((CASE_COUNT == passed) && b_supp && b_ready &&
        (FAULT_COUNT == fault_passed))
    {
        status = 0;
    }

    return status;
}

/*** end of file ***/
