/**
 * @file    test_fault_word.c
 * @brief   Host tests of fault_word (T-B3-06), run by test_main.c.
 *
 * Implemented by Ahsanul (B4) for Cheston, task T-B3-06. Build and run:
 * make -C panel_sync/test. Tests F1 to F4 check the T-B3-06 row rules on one
 * thread; F5 is the host forerunner of the T-X-02 stress test (two setters
 * and one reporter as POSIX threads), not T-X-02 itself, which runs on the
 * two ESP32-S3 cores.
 *
 * DEVIATION D-1 (module-wide): C11 stdatomic.h for the shared test flags.
 * The int results of pthread_create(), pthread_join() and sched_yield() are
 * compared or discarded at once, so no int variable is declared.
 */
#include "test_fault_word.h"

#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>

#include "test_main.h"
#include "../main/fault_word.h"

/** Sets per setter thread in F5 (plan choice: one tenth of the T-X-02
 *  1,000,000, so the host test runs in about a second). */
#define F5_SETS 100000u

/** Number of setter threads in F5 (one per core in T-X-02). */
#define F5_SETTERS 2u

/** One F5 setter: its bit and how many times it raised it. */
typedef struct
{
    uint32_t bit;  /**< FAULT_BIT_* event value owned by this thread. */
    uint32_t sets; /**< Sets made. */
} test_fault_setter_t;

/** Snapshots containing each setter's bit, counted by the reporter. */
static _Atomic uint32_t g_seen[F5_SETTERS];

/** Setters still running. */
static _Atomic uint32_t g_running = 0u;

/** The F5 setters' bits. */
static test_fault_setter_t g_setters[F5_SETTERS];

static void   word_reset (void);
static void * setter_thread (void * p_arg);
static void * reporter_thread (void * p_arg);

/**
 * @brief  Test F1: set and snapshot.
 * @return true when every check passed.
 */
bool test_fault_word_set_snapshot (void)
{
    bool b_ok = true;

    word_reset();
    b_ok = test_main_check(0u == fault_word_snapshot(), __LINE__) && b_ok;

    fault_word_set(FAULT_BIT_MISSED_DEADLINE);
    fault_word_set(FAULT_BIT_WIFI);
    fault_word_set(FAULT_BIT_MISSED_DEADLINE);

    uint32_t const expected = FAULT_BIT_MISSED_DEADLINE | FAULT_BIT_WIFI;

    b_ok = test_main_check(expected == fault_word_snapshot(), __LINE__) && b_ok;
    b_ok = test_main_check(0u == (FAULT_EVENT_MASK & FAULT_CONDITION_MASK),
                           __LINE__) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Test F2: the ACK clears only the sent event bits.
 * @return true when every check passed.
 */
bool test_fault_word_ack_events_only (void)
{
    bool b_ok = true;

    word_reset();
    fault_word_set(FAULT_BIT_CRC_FAIL | FAULT_BIT_IMU |
                   FAULT_BIT_QUEUE_OVERFLOW);

    uint32_t const sent = fault_word_snapshot();

    /* An event raised after the packet left was not sent. */
    fault_word_set(FAULT_BIT_SYNC_STALE);
    fault_word_clear_events(sent);

    uint32_t const left = fault_word_snapshot();

    b_ok = test_main_check((FAULT_BIT_IMU | FAULT_BIT_SYNC_STALE) == left,
                           __LINE__) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Test F3: condition bits clear only through their owner.
 * @return true when every check passed.
 */
bool test_fault_word_condition_owner (void)
{
    bool b_ok = true;

    word_reset();
    fault_word_set(FAULT_BIT_ASSET_FORMAT | FAULT_BIT_MISSED_DEADLINE);

    /* Three ACKs, each carrying every bit: the condition stays. */
    for (uint32_t ack = 0u; ack < 3u; ack++)
    {
        fault_word_clear_events(fault_word_snapshot());
        b_ok = test_main_check(FAULT_BIT_ASSET_FORMAT == fault_word_snapshot(),
                               __LINE__) &&
               b_ok;
    }

    /* The owner's clear ignores an event bit passed with it. */
    fault_word_set(FAULT_BIT_CRC_FAIL);
    fault_word_clear_condition(FAULT_BIT_ASSET_FORMAT | FAULT_BIT_CRC_FAIL);
    b_ok = test_main_check(FAULT_BIT_CRC_FAIL == fault_word_snapshot(),
                           __LINE__) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Test F4: VC-4 merge rule.
 * @return true when every check passed.
 */
bool test_fault_word_merge_rule (void)
{
    bool b_ok = true;

    word_reset();
    fault_word_set(FAULT_BIT_MISSED_DEADLINE);

    uint32_t const sent = fault_word_snapshot();

    /* Raised again between send and ACK: merged into the report just sent. */
    fault_word_set(FAULT_BIT_MISSED_DEADLINE);
    /* Raised for the first time after the snapshot: not in this report. */
    fault_word_set(FAULT_BIT_QUEUE_OVERFLOW);
    fault_word_clear_events(sent);

    b_ok = test_main_check(FAULT_BIT_QUEUE_OVERFLOW == fault_word_snapshot(),
                           __LINE__) &&
           b_ok;

    /* Without an ACK the bit rides in the next packet. */
    uint32_t const next = fault_word_snapshot();

    b_ok = test_main_check(0u != (next & FAULT_BIT_QUEUE_OVERFLOW), __LINE__) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Test F5: two setters, one reporter, 0 lost sets.
 * @return true when every check passed.
 */
bool test_fault_word_two_threads (void)
{
    bool      b_ok = true;
    pthread_t setters[F5_SETTERS];
    pthread_t reporter;

    word_reset();
    g_setters[0].bit  = FAULT_BIT_MISSED_DEADLINE;
    g_setters[0].sets = 0u;
    g_setters[1].bit  = FAULT_BIT_QUEUE_OVERFLOW;
    g_setters[1].sets = 0u;

    for (uint32_t idx = 0u; idx < F5_SETTERS; idx++)
    {
        atomic_store_explicit(&g_seen[idx], 0u, memory_order_relaxed);
    }

    atomic_store_explicit(&g_running, F5_SETTERS, memory_order_release);

    b_ok = test_main_check(
               0 == pthread_create(&reporter, NULL, reporter_thread, NULL),
               __LINE__) &&
           b_ok;

    for (uint32_t idx = 0u; idx < F5_SETTERS; idx++)
    {
        b_ok =
            test_main_check(0 == pthread_create(&setters[idx], NULL,
                                                setter_thread, &g_setters[idx]),
                            __LINE__) &&
            b_ok;
    }

    for (uint32_t idx = 0u; idx < F5_SETTERS; idx++)
    {
        b_ok =
            test_main_check(0 == pthread_join(setters[idx], NULL), __LINE__) &&
            b_ok;
    }

    b_ok = test_main_check(0 == pthread_join(reporter, NULL), __LINE__) && b_ok;

    for (uint32_t idx = 0u; idx < F5_SETTERS; idx++)
    {
        uint32_t const seen =
            atomic_load_explicit(&g_seen[idx], memory_order_relaxed);

        printf("    setter %" PRIu32 ": sets=%" PRIu32 " seen=%" PRIu32
               " lost=%" PRIu32 "\n",
               idx, g_setters[idx].sets, seen, g_setters[idx].sets - seen);
        b_ok =
            test_main_check(F5_SETS == g_setters[idx].sets, __LINE__) && b_ok;
        b_ok = test_main_check(F5_SETS == seen, __LINE__) && b_ok;
    }

    return b_ok;
}

/**
 * @brief  Clear every bit, events and conditions, through the public API.
 */
static void word_reset (void)
{
    fault_word_clear_events(FAULT_EVENT_MASK);
    fault_word_clear_condition(FAULT_CONDITION_MASK);
}

/**
 * @brief  F5 setter: raise its bit F5_SETS times, each time waiting until the
 *         reporter has cleared it, so two sets never merge into one report.
 * @param  p_arg  This thread's test_fault_setter_t.
 * @return NULL.
 */
static void * setter_thread (void * p_arg)
{
    test_fault_setter_t * const p_setter = p_arg;

    for (uint32_t idx = 0u; idx < F5_SETS; idx++)
    {
        while (0u != (fault_word_snapshot() & p_setter->bit))
        {
            (void)sched_yield(); /* The result only says whether it yielded. */
        }

        fault_word_set(p_setter->bit);
        p_setter->sets++;
    }

    /* The count before the decrement is not needed. */
    (void)atomic_fetch_sub_explicit(&g_running, 1u, memory_order_release);

    return NULL;
}

/**
 * @brief  F5 reporter (the Network Task's part): snapshot, count each
 *         setter's bit, then clear the sent events as if the ACK had arrived.
 *         Stops when both setters are done and the word is empty.
 * @param  p_arg  Unused.
 * @return NULL.
 */
static void * reporter_thread (void * p_arg)
{
    bool b_done = false;

    (void)p_arg; /* The reporter takes no parameter. */

    while (!b_done)
    {
        uint32_t const running =
            atomic_load_explicit(&g_running, memory_order_acquire);
        uint32_t const snap = fault_word_snapshot();

        for (uint32_t idx = 0u; idx < F5_SETTERS; idx++)
        {
            if (0u != (snap & g_setters[idx].bit))
            {
                /* The count before the increment is not needed. */
                (void)atomic_fetch_add_explicit(&g_seen[idx], 1u,
                                                memory_order_relaxed);
            }
        }

        if (0u != snap)
        {
            fault_word_clear_events(snap);
        }
        else if (0u == running)
        {
            b_done = true;
        }
        else
        {
            (void)sched_yield(); /* The result only says whether it yielded. */
        }
    }

    return NULL;
}

/*** end of file ***/
