/**
 * @file    test_main.c
 * @brief   Host test runner for ledwall_sync (T-B2-02 to T-B2-05).
 *
 * Owner: Benjamin (Buddy 2). Implemented by Ahsanul (B4) for Benjamin.
 * Each test is one plan acceptance check that needs no hardware, or a
 * check of a rule the plan row states. Build and run:
 * make -C ledwall_sync/test
 */
#include "test_main.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "test_coord.h"
#include "test_coord_game.h"
#include "test_ack_outbox.h"
#include "test_game_rules.h"
#include "test_join_backoff.h"
#include "test_score_font.h"

/** One test. */
typedef struct
{
    char const *   p_name;
    test_main_fn_t p_test;
} test_case_t;

static test_case_t const g_cases[] = {
    {"C1 T-B2-02: absolute 100 ms schedule, t_p at the send, lateness 0",
     test_coord_beacon_schedule},
    {"C2 T-B2-02: a stall skips whole slots, no burst, seq contiguous",
     test_coord_beacon_stall},
    {"C3 T-B2-03: 9 simulated Node IDs register, 9 of 9 in the registry",
     test_coord_nine_nodes},
    {"C4 T-B2-03: heartbeat loss logged 2.5 to 2.6 s after, 10 of 10",
     test_coord_heartbeat_loss},
    {"C5 T-B2-03: dispatch waits when 1 ms is not left before a beacon",
     test_coord_dispatch_guard},
    {"C6 T-B2-04: 100 wrong-session or old-seq messages, 0 accepted",
     test_coord_stale_rejected},
    {"C7 T-B2-04: Start refused until 2 of 2 ASSET_READY and SYNC_READY",
     test_coord_start_gate},
    {"C8 T-B2-04: ASSET_READY with the wrong CRC32 is not counted",
     test_coord_crc_mismatch},
    {"C9 T-B2-04: SESSION_CONFIG validation", test_coord_session_validation},
    {"C9b re-plan: new SESSION_CONFIG refused in COORDINATING GAME and "
     "COORDINATING PLAYBACK",
     test_coord_session_while_busy},
    {"C10 T-B2-04: STATUS every 250 ms to the PC address", test_coord_status},
    {"C11 T-B2-05: START_AT 3 copies, one seq, t_start = copy 1 + 600 ms",
     test_coord_start_copies},
    {"C12 T-B2-05: PAUSE, RESUME_AT shows the saved frame, STOP from PAUSED",
     test_coord_pause_resume_stop},
    {"C13 T-B2-04: pause policy, PAUSE then FAULT 5 s after the loss",
     test_coord_pause_policy},
    {"C14 T-B2-04: continue policy, others keep playing, rejoin RESUME_AT",
     test_coord_continue_rejoin},
    {"C15 re-plan: router join retried with 1, 2, 4, 8, then 10 s backoff",
     test_join_backoff_schedule},
    {"C16 re-plan: acked outbox resends until ACK, expires, refuses when full",
     test_ack_outbox_limits},
    {"C17 re-plan: 30 s round, mole 1200 to 500 ms, 300 ms gap, no repeats",
     test_game_rules_timing},
    {"C18 re-plan: hit +1, miss -1, dedupe within 30 ms by highest peak",
     test_game_rules_scoring},
    {"C18b re-plan: tap in flight at the hide still hits, tap after it misses, "
     "timeout keeps the score",
     test_game_rules_grace},
    {"C18c re-plan: a late tap and its late echo are one group, one HIT",
     test_game_rules_late_echo},
    {"C19 re-plan: score font pixel counts, centring and tile slices",
     test_score_font_slices},
    {"C19b step 3: wall pictures at the largest scale per shape, sparse",
     test_score_font_pictures},
    {"C20 re-plan: START refused in video mode, mode and START ACKed, repeat "
     "ACKed only",
     test_coord_game_mode_and_start},
    {"C21 re-plan: round: MOLE_ON resent until ACK, hit, STATUS score, END and "
     "WALL_PICTURE",
     test_coord_game_round},
    {"C22 re-plan: TAP ACKed and relayed once, SESSION_CONFIG refused in round",
     test_coord_game_taps},
    {"C23 re-plan: a re-registered tile's TAPs from a low seq are new",
     test_coord_game_tap_restart},
    {"C24 step 3: ARM, start sprite on the centre tile, countdown, level",
     test_coord_game_arm},
    {"C25 step 3: START_NOW, abort in the countdown, 60 s arm timeout",
     test_coord_game_abort},
    {"C26 step 3: tap test lights the tapped tile, refused while armed",
     test_coord_game_tap_test},
    {"C27 step 3: STATUS level, time left, wrong, missed, shown, reaction",
     test_coord_game_status},
};

/** Number of tests: the element count of a short array fits uint32_t. */
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
 * @brief  Run every test.
 * @return 0 when all pass, 1 otherwise.
 */
int main (void) /* DEVIATION D-2: the C standard fixes int main (void). */
{
    uint32_t passed = 0u;

    for (uint32_t idx = 0u; idx < CASE_COUNT; idx++)
    {
        printf("%s\n", g_cases[idx].p_name);

        bool const b_passed = g_cases[idx].p_test();

        printf("  %s\n", b_passed ? "PASS" : "FAIL");

        if (b_passed)
        {
            passed++;
        }
    }

    printf("Coordinator: Pass = %" PRIu32 " of %" PRIu32 "\n", passed,
           CASE_COUNT);

    /* DEVIATION D-2: main returns int. */
    int const status = (CASE_COUNT == passed) ? 0 : 1;

    return status;
}

/*** end of file ***/
