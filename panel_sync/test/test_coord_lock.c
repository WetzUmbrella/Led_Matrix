/**
 * @file    test_coord_lock.c
 * @brief   Host tests of coord_lock (R1-5, R1-6), run by test_main.c.
 *
 * Implemented by Ahsanul (B4) for Cheston. Build and run:
 * make -C panel_sync/test.
 */
#include "test_coord_lock.h"

#include <stddef.h>
#include <stdint.h>

#include "test_main.h"
#include "../main/coord_lock.h"

/** Two coordinator addresses (arbitrary non-zero values). */
#define PICO_A UINT32_C(0x0100A8C0)
#define PICO_B UINT32_C(0x0200A8C0)

/** Number of steps in a table (a few entries: fits uint32_t). */
#define STEP_COUNT(table) ((uint32_t)(sizeof(table) / sizeof((table)[0])))

/** One beacon step of a table test. */
typedef struct
{
    uint32_t            src_addr;
    int64_t             t_l;
    coord_lock_result_t expect;
    uint32_t            expect_addr;
    uint32_t            expect_foreign;
} lock_step_t;

/**
 * @brief  Run a table of beacon steps against one lock.
 * @param  p_steps  The steps.
 * @param  count    Number of steps.
 * @return true when every step gave the expected result and state.
 */
static bool run_steps (lock_step_t const * p_steps, uint32_t count)
{
    bool         b_ok = true;
    coord_lock_t lock;

    coord_lock_init(&lock);

    for (uint32_t idx = 0u; idx < count; idx++)
    {
        coord_lock_result_t const got =
            coord_lock_beacon(&lock, p_steps[idx].src_addr, p_steps[idx].t_l);

        b_ok = (test_main_check(got == p_steps[idx].expect, __LINE__)) && b_ok;
        b_ok = (test_main_check(lock.addr == p_steps[idx].expect_addr,
                                __LINE__)) &&
               b_ok;
        b_ok = (test_main_check(lock.foreign == p_steps[idx].expect_foreign,
                                __LINE__)) &&
               b_ok;
    }

    return b_ok;
}

/**
 * @brief  Test L1: first lock, repeat, foreign sender.
 * @return true when every check passed.
 */
bool test_coord_lock_first (void)
{
    static lock_step_t const steps[] = {
        {PICO_A, 1000000, COORD_LOCK_SWITCHED, PICO_A, 0u},
        {PICO_A, 1100000, COORD_LOCK_ACCEPT, PICO_A, 0u},
        {PICO_B, 1150000, COORD_LOCK_FOREIGN, PICO_A, 1u},
        {PICO_B, 1250000, COORD_LOCK_FOREIGN, PICO_A, 2u},
        {PICO_A, 1300000, COORD_LOCK_ACCEPT, PICO_A, 2u},
    };

    return run_steps(steps, STEP_COUNT(steps));
}

/**
 * @brief  Test L2: 1.9 s keeps the lock, 2.1 s of silence switches it.
 * @return true when every check passed.
 */
bool test_coord_lock_switch (void)
{
    static lock_step_t const same_after_1_9[] = {
        {PICO_A, 1000000, COORD_LOCK_SWITCHED, PICO_A, 0u},
        {PICO_A, 2900000, COORD_LOCK_ACCEPT, PICO_A, 0u},
    };
    static lock_step_t const foreign_after_1_9[] = {
        {PICO_A, 1000000, COORD_LOCK_SWITCHED, PICO_A, 0u},
        {PICO_B, 2900000, COORD_LOCK_FOREIGN, PICO_A, 1u},
    };
    static lock_step_t const foreign_after_2_1[] = {
        {PICO_A, 1000000, COORD_LOCK_SWITCHED, PICO_A, 0u},
        {PICO_B, 3100000, COORD_LOCK_SWITCHED, PICO_B, 0u},
        {PICO_A, 3200000, COORD_LOCK_FOREIGN, PICO_B, 1u},
    };

    bool b_ok = true;

    b_ok = (run_steps(same_after_1_9, 2u)) && b_ok;
    b_ok = (run_steps(foreign_after_1_9, 2u)) && b_ok;
    b_ok = (run_steps(foreign_after_2_1, 3u)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test L3: command acceptance (R1-5).
 * @return true when every check passed.
 */
bool test_coord_lock_commands (void)
{
    bool         b_ok = true;
    coord_lock_t lock;

    coord_lock_init(&lock);

    /* Unlocked: nobody is accepted, not even address 0. */
    b_ok = (test_main_check(!coord_lock_command_ok(&lock, PICO_A), __LINE__)) &&
           b_ok;
    b_ok =
        (test_main_check(!coord_lock_command_ok(&lock, 0u), __LINE__)) && b_ok;

    /* The result is not needed: only the lock state is under test. */
    (void)coord_lock_beacon(&lock, PICO_A, 1000000);

    b_ok = (test_main_check(coord_lock_command_ok(&lock, PICO_A), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(!coord_lock_command_ok(&lock, PICO_B), __LINE__)) &&
           b_ok;
    b_ok =
        (test_main_check(!coord_lock_command_ok(&lock, 0u), __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test L4: t_l near INT64_MAX / 2 does not overflow.
 * @return true when every check passed.
 */
bool test_coord_lock_large_time (void)
{
    static int64_t const t_big = INT64_MAX / 2;

    lock_step_t const steps[] = {
        {PICO_A, t_big, COORD_LOCK_SWITCHED, PICO_A, 0u},
        {PICO_A, t_big + 1000000, COORD_LOCK_ACCEPT, PICO_A, 0u},
        {PICO_B, t_big + 1500000, COORD_LOCK_FOREIGN, PICO_A, 1u},
        {PICO_B, t_big + 3100000, COORD_LOCK_SWITCHED, PICO_B, 1u},
    };

    return run_steps(steps, STEP_COUNT(steps));
}

/*** end of file ***/
