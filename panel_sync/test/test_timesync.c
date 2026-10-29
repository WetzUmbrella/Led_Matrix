/**
 * @file    test_timesync.c
 * @brief   Host tests of timesync (T-B4-02), run by test_main.c.
 *
 * Owner: Ahsanul (Buddy 4). Build and run: make -C panel_sync/test
 * The four tests are the four in the T-B4-02 row, with the row's pass
 * criteria. The supplementary checks cover the integer arithmetic ranges
 * documented in timesync.c. Every time value used here is synthetic test
 * data.
 *
 * DEVIATION D-1 (module-wide): built as C11 with timesync.h (stdatomic.h).
 */
#include "test_timesync.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "test_main.h"
#include "../main/timesync.h"

#define BEACON_PERIOD_US INT64_C(100000)
#define SLEW_LIMIT_US    INT64_C(100)
#define TEST1_EXACT      3u
#define TEST2_KNOWN      9u
#define TEST3_TICKS      INT64_C(40)
#define TEST3_ABSORB     INT64_C(20)
#define TEST4_STEPS      3u
#define TEST4_FAILS      3
#define TEST4_RANDOM     200000u
#define SUPP_RANDOM      1000000u
#define READY_START_US   INT64_C(1000000)
#define READY_DELAY_US   INT64_C(2000)
#define READY_STALE_US   INT64_C(1100000)
#define FRAMES_600       600u
#define FRAME_TICKS      UINT64_C(1200)
#define SET_SCALE        INT64_C(0x100000001)
#define LCG_MUL          1103515245u
#define LCG_ADD          12345u
#define LCG_SHIFT        16u
#define LCG_ONE_IN       4u

static timesync_snapshot_t set_k (uint32_t set_number);
static int64_t             set_id (timesync_snapshot_t const * p_snap);
static int64_t             beacon_time (int64_t start_us, uint32_t idx);
static bool                step_case (int64_t step_us);
static bool                supp_random (void);
static void                hook_publish_new (timesync_hook_point_t point);
static void hook_read_inside_writer (timesync_hook_point_t point);
static void hook_random (timesync_hook_point_t point);

/* Hook state for test 4. */
static timesync_hook_point_t g_point;  /* Where the hook acts. */
static int32_t               g_budget; /* Remaining actions. */
static bool                  g_b_in_hook;
static uint32_t              g_next_k;
static timesync_reader_t     g_inner_reader; /* Read inside a writer. */
static bool                  g_b_inner_ok;
static uint32_t              g_lcg = 12345u;

/**
 * @brief  Test 1: slew int8_t round trip and limit at -100/+100.
 * @return true when every check passed.
 */
bool test_timesync_slew_int8 (void)
{
    static int64_t const exact[TEST1_EXACT] = {-100, 0, 100};

    bool b_ok = true;

    for (uint32_t idx = 0u; idx < TEST1_EXACT; idx++)
    {
        int8_t const field = timesync_slew_clamp(exact[idx]);

        b_ok = test_main_check(field == exact[idx], __LINE__) && b_ok;
        printf("    request %+4" PRId64 " -> int8_t %+4d\n", exact[idx], field);
    }

    b_ok = test_main_check(-100 == timesync_slew_clamp(-101), __LINE__) && b_ok;
    b_ok = test_main_check(100 == timesync_slew_clamp(101), __LINE__) && b_ok;
    printf("    request -101 -> int8_t %+4d\n    request +101 -> int8_t "
           "%+4d\n",
           timesync_slew_clamp(-101), timesync_slew_clamp(101));

    /* The same limit applies through timesync_tick(): a 101 us error gives a
     * 100 us slew. */
    timesync_init();

    for (uint32_t idx = 0u; idx < TIMESYNC_WINDOW; idx++)
    {
        int64_t const t_p = beacon_time(1000000, idx);

        timesync_add_sample(t_p, t_p + 5000);
    }

    timesync_tick(3000000); /* First publication. */

    for (uint32_t idx = 0u; idx < TIMESYNC_WINDOW; idx++)
    {
        int64_t const t_p = beacon_time(3000000, idx);

        timesync_add_sample(t_p, t_p + 5101);
    }

    timesync_tick(5000000);
    b_ok = test_main_check(100 == timesync_last_slew(), __LINE__) && b_ok;
    timesync_tick(5016667);
    b_ok = test_main_check(1 == timesync_last_slew(), __LINE__) && b_ok;

    for (uint32_t idx = 0u; idx < TIMESYNC_WINDOW; idx++)
    {
        int64_t const t_p = beacon_time(6000000, idx);

        timesync_add_sample(t_p, t_p + 5000);
    }

    timesync_tick(8000000); /* Error -101 us. */
    b_ok = test_main_check(-100 == timesync_last_slew(), __LINE__) && b_ok;
    timesync_tick(8016667);
    b_ok = test_main_check(-1 == timesync_last_slew(), __LINE__) && b_ok;
    printf("    through ts_tick: error +101 -> slews +100, +1; error -101 -> "
           "slews -100, -1\n");

    return b_ok;
}

/**
 * @brief  Test 2: 16-sample minimum-delay filter with a known minimum.
 * @return true when every check passed.
 */
bool test_timesync_min_filter (void)
{
    int64_t const true_offset = 123456789; /* Local - Pico, test data. */

    int64_t pico[TIMESYNC_WINDOW];
    int64_t local[TIMESYNC_WINDOW];
    bool    b_ok = true;

    timesync_init();

    for (uint32_t idx = 0u; idx < TIMESYNC_WINDOW; idx++)
    {
        /* (idx x 37) mod 11 is below 11, so it fits int64_t. */
        int64_t delay = 1500 + ((int64_t)((idx * 37u) % 11u) * 100);

        if (TEST2_KNOWN == idx)
        {
            delay = 600;
        }

        pico[idx]  = beacon_time(10000000, idx); /* 10 Hz beacons. */
        local[idx] = pico[idx] + true_offset + delay;
        timesync_add_sample(pico[idx], local[idx]);
    }

    int64_t est = 0;
    int64_t ref = 0;

    b_ok =
        test_main_check(timesync_filter_estimate(&est, &ref), __LINE__) && b_ok;
    printf("    estimate %" PRId64 " us, sample %" PRIu32 " offset %" PRId64
           " us\n",
           est, TEST2_KNOWN, local[TEST2_KNOWN] - pico[TEST2_KNOWN]);
    b_ok = test_main_check(est == (local[TEST2_KNOWN] - pico[TEST2_KNOWN]),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(ref == local[TEST2_KNOWN], __LINE__) && b_ok;

    return b_ok;
}

/**
 * @brief  Test 3: an injected 2 ms offset step is absorbed in exactly 20
 *         ticks. Definition: drift 0; the published offset equals the filter
 *         estimate; the estimate then changes by 2,000 us. Tick k (k = 1 to
 *         20) moves the published offset by exactly 100 us towards the
 *         estimate, so the residual after tick k is 2,000 - 100k us: 100 us
 *         after tick 19 (not absorbed) and 0 after tick 20 (absorbed), and it
 *         stays 0 on ticks 21 to 40. No tick moves corrected time by more than
 *         100 us against local time. Checked for +2 ms and -2 ms.
 * @return true when every check passed.
 */
bool test_timesync_step_20_ticks (void)
{
    bool const b_up   = step_case(2000);
    bool const b_down = step_case(-2000);

    return b_up && b_down;
}

/**
 * @brief  Test 4: forced writer interleaving. Every set the writer publishes
 *         is derived from one counter k, so a mix of two sets is detectable
 *         (same idea as T-X-05).
 * @return true when every check passed.
 */
bool test_timesync_seqlock (void)
{
    static char const * const p_reader_steps[TEST4_STEPS] = {
        "after acquire load", "mid copy", "before reload"};
    static char const * const p_writer_steps[TEST4_STEPS] = {
        "after odd store", "mid fields", "before even store"};

    bool b_ok = true;

    /* (a) A complete writer update at each reader step: the read retries and
     *     returns a complete new set, never a mix. */
    for (uint32_t idx = 0u; idx < TEST4_STEPS; idx++)
    {
        timesync_reader_t         reader = {0};
        timesync_snapshot_t const old    = set_k(1u);

        timesync_init();
        timesync_test_set_hook(NULL);
        timesync_publish(&old);
        b_ok = test_main_check(timesync_read_snapshot(&reader) &&
                                   (1 == set_id(&reader.snap)),
                               __LINE__) &&
               b_ok;

        /* idx < 3, so the sum is one of the three reader hook points. */
        g_point  = (timesync_hook_point_t)(TIMESYNC_HOOK_READER_SEQ1 + idx);
        g_next_k = 2u;
        g_budget = 1;
        timesync_test_set_hook(hook_publish_new);

        bool const b_read = timesync_read_snapshot(&reader);

        timesync_test_set_hook(NULL);
        b_ok = test_main_check(
                   b_read && (2 == set_id(&reader.snap)) &&
                       (0u == (reader.snap.flags & TIMESYNC_SYNC_STALE)),
                   __LINE__) &&
               b_ok;
        printf("    (a) writer update %-18s: read returned complete set "
               "k=%" PRId64 "\n",
               p_reader_steps[idx], set_id(&reader.snap));
    }

    /* (b) Writer pre-empted at each step of its update while a reader runs:
     *     all 3 attempts fail, SYNC_STALE is set and the last good set
     *     returned. */
    for (uint32_t idx = 0u; idx < TEST4_STEPS; idx++)
    {
        timesync_reader_t const   empty = {0};
        timesync_snapshot_t const old   = set_k(1u);
        timesync_snapshot_t const next  = set_k(2u);

        timesync_init();
        timesync_test_set_hook(NULL);
        timesync_publish(&old);
        g_inner_reader = empty;
        b_ok = test_main_check(timesync_read_snapshot(&g_inner_reader) &&
                                   (1 == set_id(&g_inner_reader.snap)),
                               __LINE__) &&
               b_ok;

        /* idx < 3, so the sum is one of the three writer hook points. */
        g_point = (timesync_hook_point_t)(TIMESYNC_HOOK_WRITER_ODD + idx);
        timesync_test_set_hook(hook_read_inside_writer);
        timesync_publish(&next);
        timesync_test_set_hook(NULL);
        b_ok = test_main_check(!g_b_inner_ok, __LINE__) && b_ok;
        b_ok = test_main_check(
                   0u != (g_inner_reader.snap.flags & TIMESYNC_SYNC_STALE),
                   __LINE__) &&
               b_ok;
        b_ok = test_main_check(1 == set_id(&g_inner_reader.snap), __LINE__) &&
               b_ok;
        b_ok =
            test_main_check(1u == g_inner_reader.stale_count, __LINE__) && b_ok;
        b_ok = test_main_check(timesync_read_snapshot(&g_inner_reader) &&
                                   (2 == set_id(&g_inner_reader.snap)) &&
                                   (0u == (g_inner_reader.snap.flags &
                                           TIMESYNC_SYNC_STALE)),
                               __LINE__) &&
               b_ok;
        printf("    (b) writer pre-empted %-17s: SYNC_STALE set, last good k=1 "
               "returned\n",
               p_writer_steps[idx]);
    }

    /* (c) Exactly 3 forced failures: SYNC_STALE and the last good set (k=10),
     *     not any of the three sets published during the read. */
    {
        timesync_reader_t         reader = {0};
        timesync_snapshot_t const first  = set_k(10u);

        timesync_init();
        timesync_test_set_hook(NULL);
        timesync_publish(&first);
        b_ok = test_main_check(timesync_read_snapshot(&reader) &&
                                   (10 == set_id(&reader.snap)),
                               __LINE__) &&
               b_ok;
        g_next_k = 11u;
        g_point  = TIMESYNC_HOOK_READER_MID;
        g_budget = TEST4_FAILS;
        timesync_test_set_hook(hook_publish_new);

        bool b_read = timesync_read_snapshot(&reader);

        timesync_test_set_hook(NULL);
        b_ok = test_main_check(
                   (!b_read) &&
                       (0u != (reader.snap.flags & TIMESYNC_SYNC_STALE)) &&
                       (0u != (reader.snap.flags & TIMESYNC_SNAP_VALID)),
                   __LINE__) &&
               b_ok;
        b_ok = test_main_check((10 == set_id(&reader.snap)) &&
                                   (1u == reader.stale_count),
                               __LINE__) &&
               b_ok;
        printf("    (c) 3 forced failures: SYNC_STALE set, last good k=%" PRId64
               " returned (k=11..13 published)\n",
               set_id(&reader.snap));

        /* Boundary: 2 forced failures, then a clean third attempt returns
         * the newest set. */
        g_next_k = 20u;
        g_budget = TEST4_FAILS - 1;
        timesync_test_set_hook(hook_publish_new);
        b_read = timesync_read_snapshot(&reader);
        timesync_test_set_hook(NULL);
        b_ok = test_main_check(
                   b_read && (21 == set_id(&reader.snap)) &&
                       (0u == (reader.snap.flags & TIMESYNC_SYNC_STALE)),
                   __LINE__) &&
               b_ok;
        printf("    (c) 2 forced failures: third attempt fresh, k=%" PRId64
               ", SYNC_STALE clear\n",
               set_id(&reader.snap));
    }

    /* (d) Pseudo-random interleaving, 200,000 reads (plan choice: enough to
     *     hit every hook combination many times; the on-target count is
     *     T-X-05's). */
    {
        timesync_reader_t   reader = {0};
        uint32_t            fresh  = 0u;
        uint32_t            stale  = 0u;
        uint32_t            torn   = 0u;
        timesync_snapshot_t first;

        timesync_init();
        timesync_test_set_hook(NULL);
        g_next_k = 1u;
        first    = set_k(g_next_k);
        g_next_k++;
        timesync_publish(&first);
        b_ok =
            test_main_check(timesync_read_snapshot(&reader), __LINE__) && b_ok;
        timesync_test_set_hook(hook_random);

        for (uint32_t idx = 0u; idx < TEST4_RANDOM; idx++)
        {
            int64_t const before = set_id(&reader.snap);
            /* Widening uint32_t to int64_t keeps every value. */
            int64_t const k_low  = (int64_t)(g_next_k - 1u);
            bool const    b_read = timesync_read_snapshot(&reader);
            /* Widening uint32_t to int64_t keeps every value. */
            int64_t const k_high    = (int64_t)(g_next_k - 1u);
            int64_t const set_ident = set_id(&reader.snap);

            if (set_ident < 0)
            {
                torn++;
            }
            else if (b_read && ((set_ident < k_low) || (set_ident > k_high)))
            {
                torn++;
            }
            else if ((!b_read) && (set_ident != before))
            {
                torn++;
            }
            else
            {
                /* A complete set from the expected range: not torn. */
            }

            if (b_read)
            {
                fresh++;
            }
            else
            {
                stale++;
            }
        }

        timesync_test_set_hook(NULL);
        printf("    (d) 200000 random interleavings: %" PRIu32
               " fresh, %" PRIu32 " SYNC_STALE (last good), %" PRIu32 " torn\n",
               fresh, stale, torn);
        b_ok = test_main_check((0u == torn) && (stale > 0u) && (fresh > 0u),
                               __LINE__) &&
               b_ok;
        b_ok = test_main_check(reader.stale_count == stale, __LINE__) && b_ok;
    }

    return b_ok;
}

/**
 * @brief  OD-10 SYNC_READY rule.
 * @return true when every check passed.
 */
bool test_timesync_sync_ready (void)
{
    int64_t const newest = beacon_time(READY_START_US, TIMESYNC_WINDOW - 1u);

    bool b_ok = true;

    /* Window not full: 15 samples, checked at the newest sample's time. */
    timesync_init();

    for (uint32_t idx = 0u; idx < (TIMESYNC_WINDOW - 1u); idx++)
    {
        int64_t const t_p = beacon_time(READY_START_US, idx);

        timesync_add_sample(t_p, t_p + READY_DELAY_US);
    }

    timesync_test_set_now_us(newest);
    b_ok = test_main_check(!timesync_sync_ready(), __LINE__) && b_ok;
    printf("    window 15 of 16: SYNC_READY %d\n", timesync_sync_ready());

    /* Window full but no drift: every sample has the same local time, so the
     * two half-window minima have zero span and drift cannot be computed. */
    timesync_init();

    for (uint32_t idx = 0u; idx < TIMESYNC_WINDOW; idx++)
    {
        timesync_add_sample(beacon_time(READY_START_US, idx), newest);
    }

    int32_t drift = 0;

    b_ok = test_main_check(!timesync_drift_estimate(&drift), __LINE__) && b_ok;
    b_ok = test_main_check(!timesync_sync_ready(), __LINE__) && b_ok;
    printf("    window full, no drift: SYNC_READY %d\n", timesync_sync_ready());

    /* All met: window full, drift available, newest sample fresh. */
    timesync_init();

    for (uint32_t idx = 0u; idx < TIMESYNC_WINDOW; idx++)
    {
        int64_t const t_p = beacon_time(READY_START_US, idx);

        timesync_add_sample(t_p, t_p + READY_DELAY_US);
    }

    b_ok = test_main_check(timesync_drift_estimate(&drift), __LINE__) && b_ok;
    b_ok = test_main_check(timesync_sync_ready(), __LINE__) && b_ok;
    printf("    all met, newest 0 s old: SYNC_READY %d\n",
           timesync_sync_ready());

    /* Boundary: exactly the 1 s holdover old is still ready. */
    timesync_test_set_now_us(newest + TIMESYNC_HOLDOVER_US);
    b_ok = test_main_check(timesync_sync_ready(), __LINE__) && b_ok;
    printf("    all met, newest 1.0 s old: SYNC_READY %d\n",
           timesync_sync_ready());

    /* Newest sample 1.1 s old: no longer ready. */
    timesync_test_set_now_us(newest + READY_STALE_US);
    b_ok = test_main_check(!timesync_sync_ready(), __LINE__) && b_ok;
    printf("    all met, newest 1.1 s old: SYNC_READY %d\n",
           timesync_sync_ready());

    timesync_init();

    return b_ok;
}

/**
 * @brief  Supplementary arithmetic checks (not counted in Pass = N of 4).
 * @return true when every check passed.
 */
bool test_timesync_supp_arithmetic (void)
{
    bool b_ok = true;

    /* T-B4-04 form: frame_index at the deadline of tick k equals k mod 600,
     * k = 0 to 1199. */
    for (int64_t origin = 0; origin <= INT64_C(4000000000000);
         origin += INT64_C(1999999999999))
    {
        for (uint64_t tick = 0u; tick < FRAME_TICKS; tick++)
        {
            uint32_t const frame = timesync_frame_index(
                timesync_deadline_pico(origin, tick), origin, FRAMES_600);

            b_ok =
                test_main_check(frame == (tick % FRAMES_600), __LINE__) && b_ok;
        }
    }

    /* No accumulation: every 60 ticks are exactly 1,000,000 us, for large n
     * too. */
    for (uint64_t mult = 0u; mult < UINT64_C(5000000000);
         mult          = (mult * 3u) + 1u)
    {
        /* mult < 5 x 10^9, so mult x 10^6 fits int64_t. */
        int64_t const expect = (int64_t)mult * 1000000;

        b_ok = test_main_check(expect == timesync_deadline_pico(0, 60u * mult),
                               __LINE__) &&
               b_ok;
    }

    b_ok = test_main_check((16667 == timesync_deadline_pico(0, 1u)) &&
                               (33334 == timesync_deadline_pico(0, 2u)) &&
                               (50000 == timesync_deadline_pico(0, 3u)),
                           __LINE__) &&
           b_ok;

    /* Before origin: the timeline continues backwards. */
    b_ok = test_main_check(
               599u == timesync_frame_index(999999, 1000000, FRAMES_600),
               __LINE__) &&
           b_ok;
    b_ok = test_main_check(540u == timesync_frame_index(0, 1000000, FRAMES_600),
                           __LINE__) &&
           b_ok;
    b_ok =
        test_main_check(0u == timesync_frame_index(5, 0, 0u), __LINE__) && b_ok;

    /* Exact range of timesync_deadline_pico for origin = 0, and saturation
     * above it. __int128 is a GCC extension, used only in this host test for
     * reference values (Barr-C 1.1.c). INT64_MAX - 10^6 is positive, so the
     * quotient fits uint64_t. */
    uint64_t const nmax =
        (60u * (uint64_t)((INT64_MAX - 1000000) / 1000000)) + 59u;

    /* Widening uint64_t to __int128 keeps every value. */
    __int128 ref = (__int128)nmax * 1000000;

    ref = (ref + 59) / 60;

    /* Widening int64_t to __int128 keeps every value. */
    b_ok = test_main_check((__int128)timesync_deadline_pico(0, nmax) == ref,
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(INT64_MAX == timesync_deadline_pico(0, nmax + 1u),
                           __LINE__) &&
           b_ok;
    printf("    deadline exact up to n = %" PRIu64 " for origin 0\n", nmax);

    b_ok = supp_random() && b_ok;

    /* Round trip Pico -> local -> Pico within 2 us at 50 ppm and 400 s. */
    timesync_snapshot_t const snap = {
        .offset_us = 777777,
        .drift_ppb = 50000,
        .ref_us    = 1000000000,
        .flags     = TIMESYNC_SNAP_VALID,
    };

    for (int64_t delta = -400000000; delta <= 400000000; delta += 9999991)
    {
        int64_t const t_p  = 1000000000 + delta;
        int64_t const back = timesync_pico_from_local(
            &snap, timesync_local_from_pico(&snap, t_p));

        b_ok = test_main_check(((back - t_p) >= -2) && ((back - t_p) <= 2),
                               __LINE__) &&
               b_ok;
    }

    b_ok = test_main_check(!timesync_sync_ready(), __LINE__) && b_ok;

    /* timesync_next_tick(): the deadline before the answer is at or before
     * t, the deadline of the answer is after t; t before origin gives 0. */
    for (int64_t t_us = INT64_C(999000); t_us <= INT64_C(3000000);
         t_us += INT64_C(997))
    {
        uint64_t const tick = timesync_next_tick(1000000, t_us);

        b_ok = test_main_check(timesync_deadline_pico(1000000, tick) > t_us,
                               __LINE__) &&
               b_ok;

        if (tick > 0u)
        {
            b_ok = test_main_check(timesync_deadline_pico(1000000, tick - 1u) <=
                                       t_us,
                                   __LINE__) &&
                   b_ok;
        }
    }

    b_ok = test_main_check((0u == timesync_next_tick(1000000, 999999)) &&
                               (1u == timesync_next_tick(1000000, 1000000)) &&
                               (2u == timesync_next_tick(1000000, 1016667)),
                           __LINE__) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Random comparison against 128-bit references, 1,000,000 inputs.
 *         __int128 is a GCC extension, used only in this host test for
 *         reference values (Barr-C 1.1.c).
 * @return true when every check passed.
 */
static bool supp_random (void)
{
    uint64_t state = UINT64_C(88172645463325252);
    bool     b_ok  = true;

    for (uint32_t idx = 0u; idx < SUPP_RANDOM; idx++)
    {
        state ^= state << 13u;
        state ^= state >> 7u;
        state ^= state << 17u;

        /* Shifting right by 2 leaves 62 bits, which fit int64_t. */
        int64_t const t_us = (int64_t)(state >> 2u);
        /* The product is reduced mod 2^64, then 62 bits remain. */
        int64_t const origin = (int64_t)((state * UINT64_C(2654435761)) >> 2u);
        /* The remainder is below 1023, so it fits uint32_t. */
        uint32_t const count = (uint32_t)(state % 1023u) + 1u;
        /* Widening int64_t to __int128 keeps every value. */
        __int128 const delta   = (__int128)t_us - origin;
        __int128 const scaled  = delta * 60;
        __int128       floor_q = scaled / 1000000;

        if ((scaled % 1000000) < 0)
        {
            floor_q--;
        }

        __int128 mod = floor_q % count;

        if (mod < 0)
        {
            mod += count;
        }

        /* 0 <= mod < count, which fits uint32_t. */
        uint32_t const expect_frame = (uint32_t)mod;

        b_ok = test_main_check(timesync_frame_index(t_us, origin, count) ==
                                   expect_frame,
                               __LINE__) &&
               b_ok;

        /* The remainder is below 2 x 10^18, which fits int64_t. */
        int64_t const dt_us = (int64_t)(state % UINT64_C(2000000000000000000)) -
                              INT64_C(1000000000000000000);
        /* The high 32 bits reinterpreted as a signed drift (GCC: modulo). */
        int32_t const ppb = (int32_t)(uint32_t)(state >> 32u);
        /* Widening int64_t to __int128 keeps every value. */
        __int128 const product = (__int128)dt_us * ppb;
        __int128       floor_p = product / 1000000000;

        if ((product % 1000000000) < 0)
        {
            floor_p--;
        }

        /* Widening int64_t to __int128 keeps every value. */
        b_ok =
            test_main_check((__int128)timesync_scale_ppb(dt_us, ppb) == floor_p,
                            __LINE__) &&
            b_ok;
    }

    return b_ok;
}

/**
 * @brief  One direction of test 3.
 * @param  step_us  The injected offset step, +2000 or -2000 us.
 * @return true when every check passed.
 */
static bool step_case (int64_t step_us)
{
    int64_t const base   = 5000000;
    int64_t const delay  = 1000;
    int64_t const origin = 0;

    bool    b_ok    = true;
    int64_t t_local = 1000000;

    timesync_init();

    for (uint32_t idx = 0u; idx < TIMESYNC_WINDOW; idx++)
    {
        timesync_add_sample(t_local, t_local + base + delay);
        t_local += BEACON_PERIOD_US;
    }

    /* First publication = estimate. */
    timesync_tick(timesync_deadline_pico(origin, 0u) + t_local);

    /* Replace the whole window, so the estimate is exactly base + step and
     * drift stays 0. */
    for (uint32_t idx = 0u; idx < TIMESYNC_WINDOW; idx++)
    {
        timesync_add_sample(t_local, t_local + base + step_us + delay);
        t_local += BEACON_PERIOD_US;
    }

    int64_t est   = 0;
    int64_t ref   = 0;
    int32_t drift = 0;

    b_ok = test_main_check(timesync_filter_estimate(&est, &ref) &&
                               (est == (base + delay + step_us)),
                           __LINE__) &&
           b_ok;
    b_ok = test_main_check(timesync_drift_estimate(&drift) && (0 == drift),
                           __LINE__) &&
           b_ok;

    timesync_reader_t reader      = {0};
    int64_t           prev_pico   = 0;
    int64_t           prev_local  = 0;
    int64_t           absorbed_at = -1;

    for (int64_t tick = 1; tick <= TEST3_TICKS; tick++)
    {
        /* tick is 1 to 40, so it converts to uint64_t unchanged. Deadlines
         * are computed from tick. */
        int64_t const t_tick =
            t_local + timesync_deadline_pico(origin, (uint64_t)tick);

        timesync_tick(t_tick);
        b_ok =
            test_main_check(timesync_read_snapshot(&reader), __LINE__) && b_ok;

        int64_t const residual = est - reader.snap.offset_us;
        int64_t const sign     = (step_us > 0) ? SLEW_LIMIT_US : -SLEW_LIMIT_US;
        int64_t const expect =
            (tick <= TEST3_ABSORB) ? (step_us - (sign * tick)) : 0;

        b_ok = test_main_check(residual == expect, __LINE__) && b_ok;
        b_ok = test_main_check((timesync_last_slew() >= -100) &&
                                   (timesync_last_slew() <= 100),
                               __LINE__) &&
               b_ok;
        timesync_test_set_now_us(t_tick);

        int64_t const pico = timesync_now_corrected(&reader);

        if (tick > 1)
        {
            int64_t const jump = (pico - prev_pico) - (t_tick - prev_local);

            b_ok = test_main_check((jump >= -100) && (jump <= 100), __LINE__) &&
                   b_ok;
        }

        prev_pico  = pico;
        prev_local = t_tick;

        if ((0 == residual) && (absorbed_at < 0))
        {
            absorbed_at = tick;
        }

        if ((tick >= (TEST3_ABSORB - 1)) && (tick <= (TEST3_ABSORB + 1)))
        {
            printf("    step %+" PRId64 " us: after tick %" PRId64
                   " residual %+" PRId64 " us\n",
                   step_us, tick, residual);
        }
    }

    printf("    step %+" PRId64 " us absorbed at tick %" PRId64 "\n", step_us,
           absorbed_at);
    b_ok = test_main_check(TEST3_ABSORB == absorbed_at, __LINE__) && b_ok;

    return b_ok;
}

/**
 * @brief  Pico time of beacon idx in a 10 Hz series.
 * @param  start_us  Time of beacon 0, microseconds.
 * @param  idx       Beacon number.
 * @return start + idx x 100,000 us.
 */
static int64_t beacon_time (int64_t start_us, uint32_t idx)
{
    /* Widening uint32_t to int64_t keeps every value. */
    return start_us + ((int64_t)idx * BEACON_PERIOD_US);
}

/**
 * @brief  The complete set derived from counter k.
 * @param  set_number  k.
 * @return The set: both offset halves and the reference change with k.
 */
static timesync_snapshot_t set_k (uint32_t set_number)
{
    /* Widening uint32_t to int64_t keeps every value. */
    int64_t const wide = (int64_t)set_number;

    timesync_snapshot_t const snap = {
        .offset_us = wide * SET_SCALE,
        .drift_ppb = (int32_t)set_number, /* k's bits; set_id() undoes it. */
        .ref_us    = (wide * SET_SCALE) + 7,
        .flags     = TIMESYNC_SNAP_VALID,
    };

    return snap;
}

/**
 * @brief  Which complete set a snapshot is.
 * @param  p_snap  Snapshot.
 * @return k if p_snap is a complete set, else -1.
 */
static int64_t set_id (timesync_snapshot_t const * p_snap)
{
    /* Inverse of the cast in set_k(): the same 32-bit pattern. */
    uint32_t const            set_number = (uint32_t)p_snap->drift_ppb;
    timesync_snapshot_t const expect     = set_k(set_number);
    int64_t                   set_ident  = -1;

    if ((p_snap->offset_us == expect.offset_us) &&
        (p_snap->ref_us == expect.ref_us))
    {
        /* Widening uint32_t to int64_t keeps every value. */
        set_ident = (int64_t)set_number;
    }

    return set_ident;
}

/**
 * @brief  Hook: a complete writer update lands at g_point, g_budget times.
 * @param  point  Where the writer or reader is.
 */
static void hook_publish_new (timesync_hook_point_t point)
{
    if ((!g_b_in_hook) && (point == g_point) && (g_budget > 0))
    {
        timesync_snapshot_t const next = set_k(g_next_k);

        g_b_in_hook = true;
        g_budget--;
        g_next_k++;
        timesync_publish(&next);
        g_b_in_hook = false;
    }
}

/**
 * @brief  Hook: the writer is pre-empted at g_point and a reader runs.
 * @param  point  Where the writer is.
 */
static void hook_read_inside_writer (timesync_hook_point_t point)
{
    if ((!g_b_in_hook) && (point == g_point))
    {
        g_b_in_hook  = true;
        g_b_inner_ok = timesync_read_snapshot(&g_inner_reader);
        g_b_in_hook  = false;
    }
}

/**
 * @brief  Hook: a writer update at 1 in 4 reader steps, chosen by an LCG.
 * @param  point  Where the reader is.
 */
static void hook_random (timesync_hook_point_t point)
{
    if ((!g_b_in_hook) && (point >= TIMESYNC_HOOK_READER_SEQ1))
    {
        g_lcg = (g_lcg * LCG_MUL) + LCG_ADD;

        if (0u == ((g_lcg >> LCG_SHIFT) % LCG_ONE_IN))
        {
            timesync_snapshot_t const next = set_k(g_next_k);

            g_b_in_hook = true;
            g_next_k++;
            timesync_publish(&next);
            g_b_in_hook = false;
        }
    }
}

/*** end of file ***/
