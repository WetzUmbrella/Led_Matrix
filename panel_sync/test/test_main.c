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

#include "test_asset_rx.h"
#include "test_coord_lock.h"
#include "test_display.h"
#include "test_fault_word.h"
#include "test_playback.h"
#include "test_telemetry_book.h"
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

static test_case_t const g_playback[] = {
    {"Test P1: three identical START_AT give 1 transition",
     test_playback_start_once},
    {"Test P2: a wrong session_id gives 0 transitions",
     test_playback_wrong_session},
    {"Test P3: frame at origin + k x 10^6 / 60 is k mod 600, k 0 to 1199",
     test_playback_frame_index},
    {"Test P4: STOP accepted from PAUSED", test_playback_stop_from_paused},
    {"Test P5: RESUME_AT shows the saved index at t_resume",
     test_playback_resume_frame},
    {"Test P6: 0.9 s without beacons keeps playing, 1.1 s pauses, 10 of 10",
     test_playback_holdover},
};

static test_case_t const g_telemetry[] = {
    {"Test T1: ACK of a recorded seq returns its bits",
     test_telemetry_book_ack_match},
    {"Test T2: repeated and unknown ACKs change nothing",
     test_telemetry_book_repeat},
    {"Test T3: ring of 4, the oldest expires", test_telemetry_book_expiry},
    {"Test T4: seq wrap 65535 to 0", test_telemetry_book_wrap},
    {"Test T5: VC-4 drop every third, 100% delivered, 0 transitions",
     test_telemetry_book_vc4},
};

static test_case_t const g_asset[] = {
    {"Test A1: manifest format and length rules", test_asset_rx_manifest},
    {"Test A2: 600 frames = 57 chunks, the last 256 B",
     test_asset_rx_chunk_count},
    {"Test A3: new, repeat, other session, wrong length",
     test_asset_rx_chunk_classes},
    {"Test A4: complete after all 57 chunks in any order",
     test_asset_rx_complete},
    {"Test A5: zlib CRC32 0xCBF43926 and continuation", test_asset_rx_crc32},
};

static test_case_t const g_coord[] = {
    {"Test L1: first lock, repeat, foreign sender counted and refused",
     test_coord_lock_first},
    {"Test L2: 1.9 s keeps the lock, 2.1 s of silence switches it",
     test_coord_lock_switch},
    {"Test L3: commands refused while unlocked and from others",
     test_coord_lock_commands},
    {"Test L4: t_l near INT64_MAX / 2 does not overflow",
     test_coord_lock_large_time},
};

static test_case_t const g_display[] = {
    {"Test D1: frame_pattern vector 192 of 192 bytes, all 0 to 8",
     test_display_unpack_vector},
    {"Test D2: rotation remap, 4 x 90 = identity, 180 = reverse",
     test_display_rotation},
    {"Test D3: frame index from ticks", test_display_frame_index},
    {"Test O1: 25 equal samples give 0 changes, 26 give 1",
     test_display_orient_debounce},
    {"Test O2: forced NACK keeps the orientation, IMU bit set then cleared",
     test_display_orient_nack},
    {"Test O3: quadrants, weak gravity, flicker restarts the count",
     test_display_orient_classify},
};

/** Number of counted display and orientation tests (6): the element count
 *  of a six-entry array fits uint32_t. */
#define DISPLAY_COUNT                                                          \
    ((uint32_t)(sizeof(g_display) / sizeof(g_display[0]))) /* Fits. */

/** Number of counted coordinator lock tests (4): a four-entry array count
 *  fits uint32_t. */
#define COORD_COUNT                                                            \
    ((uint32_t)(sizeof(g_coord) / sizeof(g_coord[0]))) /* Fits. */

/** Number of counted asset tests (5): the element count of a five-entry
 *  array fits uint32_t. */
#define ASSET_COUNT                                                            \
    ((uint32_t)(sizeof(g_asset) / sizeof(g_asset[0]))) /* Fits. */

/** Number of counted telemetry tests (5): the element count of a
 *  five-entry array fits uint32_t. */
#define TELEMETRY_COUNT                                                        \
    ((uint32_t)(sizeof(g_telemetry) / sizeof(g_telemetry[0]))) /* Fits. */

/** Number of counted playback tests (6): a six-entry array count fits
 *  uint32_t. */
#define PLAYBACK_COUNT ((uint32_t)(sizeof(g_playback) / sizeof(g_playback[0])))

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
 * @brief  Run a table of counted tests.
 * @param  p_cases  The tests.
 * @param  count    Number of tests.
 * @return How many passed.
 */
static uint32_t run_cases (test_case_t const * p_cases, uint32_t count)
{
    uint32_t passed = 0u;

    for (uint32_t idx = 0u; idx < count; idx++)
    {
        if (test_main_run(p_cases[idx].p_name, p_cases[idx].p_test))
        {
            passed++;
        }
    }

    return passed;
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
    bool const b_reseed =
        test_main_run("R1-2 re-seed on another Pico clock (not counted)",
                      test_timesync_reseed);

    printf("Pass = %" PRIu32 " of %" PRIu32 "\n", passed, CASE_COUNT);

    uint32_t fault_passed = run_cases(g_faults, FAULT_COUNT);

    printf("Fault word: Pass = %" PRIu32 " of %" PRIu32 "\n", fault_passed,
           FAULT_COUNT);

    uint32_t playback_passed = run_cases(g_playback, PLAYBACK_COUNT);

    printf("Playback: Pass = %" PRIu32 " of %" PRIu32 "\n", playback_passed,
           PLAYBACK_COUNT);

    uint32_t telemetry_passed = run_cases(g_telemetry, TELEMETRY_COUNT);

    printf("Telemetry: Pass = %" PRIu32 " of %" PRIu32 "\n", telemetry_passed,
           TELEMETRY_COUNT);

    uint32_t asset_passed = run_cases(g_asset, ASSET_COUNT);

    printf("Asset: Pass = %" PRIu32 " of %" PRIu32 "\n", asset_passed,
           ASSET_COUNT);

    uint32_t coord_passed = run_cases(g_coord, COORD_COUNT);

    printf("Coordinator lock: Pass = %" PRIu32 " of %" PRIu32 "\n",
           coord_passed, COORD_COUNT);

    uint32_t display_passed = run_cases(g_display, DISPLAY_COUNT);

    printf("Display: Pass = %" PRIu32 " of %" PRIu32 "\n", display_passed,
           DISPLAY_COUNT);

    /* DEVIATION D-2: the exit status is the int that main returns. */
    int status = 1;

    if ((CASE_COUNT == passed) && b_supp && b_ready && b_reseed &&
        (FAULT_COUNT == fault_passed) && (PLAYBACK_COUNT == playback_passed) &&
        (TELEMETRY_COUNT == telemetry_passed) &&
        (ASSET_COUNT == asset_passed) && (COORD_COUNT == coord_passed) &&
        (DISPLAY_COUNT == display_passed))
    {
        status = 0;
    }

    return status;
}

/*** end of file ***/
