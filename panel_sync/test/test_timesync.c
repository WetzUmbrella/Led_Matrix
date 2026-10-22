// Host tests for timesync_core.c (T-B4-02). Build and run: make -C panel_sync/test
//
// The four tests are the four in the T-B4-02 row, with the row's pass criteria.
// The supplementary checks after them cover the integer arithmetic ranges
// documented in timesync_core.c; they are not part of "Pass = N of 4".
// Every time value used here is synthetic test data.
#ifndef TIMESYNC_HOST_TEST
#error "build with -DTIMESYNC_HOST_TEST (the Makefile does)"
#endif

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "../main/timesync.h"

/* ---- fake platform clock ---------------------------------------------- */
static int64_t g_now_us;
int64_t ts_platform_now_us(void) { return g_now_us; }

static int g_fail_lines;
#define CHECK(cond) do { if (!(cond)) { g_fail_lines++; \
    printf("    check failed at line %d: %s\n", __LINE__, #cond); return false; } } while (0)

/* ======================================================================== */
/* Test 1: slew values -100, 0, +100 round-trip through the int8_t field
 * unchanged; requests of -101 and +101 are limited to -100 and +100.        */
static bool test1_slew_int8(void)
{
    const int64_t exact[] = { -100, 0, 100 };
    for (int i = 0; i < 3; i++) {
        int8_t field = ts_slew_clamp(exact[i]);
        CHECK((int64_t)field == exact[i]);
        printf("    request %+4" PRId64 " -> int8_t %+4d\n", exact[i], field);
    }
    CHECK(ts_slew_clamp(-101) == -100);
    CHECK(ts_slew_clamp(101) == 100);
    printf("    request -101 -> int8_t %+4d\n    request +101 -> int8_t %+4d\n",
           ts_slew_clamp(-101), ts_slew_clamp(101));

    // The same limit applies through ts_tick(): a 101 us error gives a 100 us slew.
    ts_core_init();
    for (int i = 0; i < TS_WINDOW; i++) ts_add_sample(1000000 + i * 100000, 1000000 + i * 100000 + 5000);
    ts_tick(3000000);                                  // first publication
    for (int i = 0; i < TS_WINDOW; i++) ts_add_sample(3000000 + i * 100000, 3000000 + i * 100000 + 5101);
    ts_tick(5000000);
    CHECK(ts_last_slew() == 100);
    ts_tick(5016667);
    CHECK(ts_last_slew() == 1);
    for (int i = 0; i < TS_WINDOW; i++) ts_add_sample(6000000 + i * 100000, 6000000 + i * 100000 + 5000);
    ts_tick(8000000);                                  // error -101 us
    CHECK(ts_last_slew() == -100);
    ts_tick(8016667);
    CHECK(ts_last_slew() == -1);
    printf("    through ts_tick: error +101 -> slews +100, +1; error -101 -> slews -100, -1\n");
    return true;
}

/* ======================================================================== */
/* Test 2: 16 synthetic samples with one known minimum: the estimate equals
 * that sample's offset exactly.                                             */
static bool test2_min_filter(void)
{
    const int64_t true_offset = 123456789;         // local - Pico, test data
    const int known = 9;                           // index of the minimum-delay sample
    int64_t tp[TS_WINDOW], tl[TS_WINDOW];
    ts_core_init();
    for (int i = 0; i < TS_WINDOW; i++) {
        int64_t delay = 1500 + (int64_t)((i * 37) % 11) * 100;   // 1500..2500 us
        if (i == known) delay = 600;
        tp[i] = 10000000 + (int64_t)i * 100000;    // 10 Hz beacons
        tl[i] = tp[i] + true_offset + delay;
        ts_add_sample(tp[i], tl[i]);
    }
    int64_t est, ref;
    CHECK(ts_filter_estimate(&est, &ref));
    printf("    estimate %" PRId64 " us, sample %d offset %" PRId64 " us\n",
           est, known, tl[known] - tp[known]);
    CHECK(est == tl[known] - tp[known]);
    CHECK(ref == tl[known]);
    return true;
}

/* ======================================================================== */
/* Test 3: an injected 2 ms offset step is absorbed in exactly 20 ticks.
 * Definition: drift 0; the published offset equals the filter estimate; the
 * estimate then changes by 2,000 us. Tick k (k = 1..20) moves the published
 * offset by exactly 100 us towards the estimate, so the residual after tick k
 * is 2,000 - 100k us: 100 us after tick 19 (not absorbed) and 0 after tick 20
 * (absorbed), and it stays 0 on ticks 21 to 40. No tick moves corrected time
 * by more than 100 us against local time. Checked for +2 ms and -2 ms.      */
static bool step_case(int64_t step_us)
{
    const int64_t base = 5000000, delay = 1000;
    ts_core_init();
    int64_t t = 1000000;
    for (int i = 0; i < TS_WINDOW; i++, t += 100000) ts_add_sample(t, t + base + delay);
    uint64_t n = 0;
    const int64_t origin = 0;
    ts_tick(ts_deadline_pico(origin, n) + t);          // first publication = estimate
    // Replace the whole window, so the estimate is exactly base + step and drift stays 0.
    for (int i = 0; i < TS_WINDOW; i++, t += 100000) ts_add_sample(t, t + base + step_us + delay);
    int64_t est, ref; int32_t drift;
    CHECK(ts_filter_estimate(&est, &ref) && est == base + delay + step_us);
    CHECK(ts_drift_estimate(&drift) && drift == 0);

    ts_reader_t rd = {0};
    int64_t prev_pico = 0, prev_local = 0;
    int absorbed_at = -1;
    for (int k = 1; k <= 40; k++) {
        int64_t t_tick = t + ts_deadline_pico(origin, (uint64_t)k);   // deadlines computed from k
        ts_tick(t_tick);
        CHECK(ts_read_snapshot(&rd));
        int64_t residual = est - rd.snap.offset_us;
        int64_t expect = (k <= 20) ? step_us - (step_us > 0 ? 100 : -100) * k : 0;
        CHECK(residual == expect);
        CHECK(ts_last_slew() >= -100 && ts_last_slew() <= 100);
        g_now_us = t_tick;
        int64_t pico = now_corrected(&rd);
        if (k > 1) {
            int64_t jump = (pico - prev_pico) - (t_tick - prev_local);
            CHECK(jump >= -100 && jump <= 100);
        }
        prev_pico = pico; prev_local = t_tick;
        if (residual == 0 && absorbed_at < 0) absorbed_at = k;
        if (k == 19 || k == 20 || k == 21)
            printf("    step %+" PRId64 " us: after tick %d residual %+" PRId64 " us\n", step_us, k, residual);
    }
    printf("    step %+" PRId64 " us absorbed at tick %d\n", step_us, absorbed_at);
    CHECK(absorbed_at == 20);
    return true;
}

static bool test3_step_20_ticks(void)
{
    return step_case(2000) && step_case(-2000);
}

/* ======================================================================== */
/* Test 4: forced writer interleaving: every snapshot equals a complete old or
 * new set, and after 3 forced failures SYNC_STALE is set and the last good
 * snapshot is returned.
 * Every set the writer publishes is derived from one counter k, so a mix of
 * two sets is detectable (same idea as T-X-05).                              */
static ts_snapshot_t set_k(uint32_t k)
{
    ts_snapshot_t s = {
        .offset_us = (int64_t)k * 0x100000001LL,         // both halves change
        .drift_ppb = (int32_t)k,
        .ref_us    = (int64_t)k * 0x100000001LL + 7,
        .flags     = TS_SNAP_VALID,
    };
    return s;
}

// Returns k if s is a complete set, else -1.
static int64_t set_id(const ts_snapshot_t *s)
{
    uint32_t k = (uint32_t)s->drift_ppb;
    ts_snapshot_t e = set_k(k);
    if (s->offset_us != e.offset_us || s->ref_us != e.ref_us) return -1;
    return k;
}

static ts_hook_point_t g_point;          // where the hook acts
static int  g_budget;                    // how many more times it acts
static int  g_in_hook;
static uint32_t g_next_k;
static ts_reader_t g_inner_reader;       // reader run from inside a pre-empted writer
static bool g_inner_ok;
static uint32_t g_lcg = 12345u;

static void hook_publish_new(ts_hook_point_t p)   // a complete writer update lands here
{
    if (g_in_hook || p != g_point || g_budget <= 0) return;
    g_in_hook = 1; g_budget--;
    ts_snapshot_t s = set_k(g_next_k++);
    ts_publish(&s);
    g_in_hook = 0;
}

static void hook_read_inside_writer(ts_hook_point_t p)   // writer pre-empted; a reader runs
{
    if (g_in_hook || p != g_point) return;
    g_in_hook = 1;
    g_inner_ok = ts_read_snapshot(&g_inner_reader);
    g_in_hook = 0;
}

static void hook_random(ts_hook_point_t p)
{
    if (g_in_hook || p < TS_HOOK_READER_SEQ1) return;
    g_lcg = g_lcg * 1103515245u + 12345u;
    if ((g_lcg >> 16) % 4u != 0u) return;              // a writer update at 1 in 4 reader steps
    g_in_hook = 1;
    ts_snapshot_t s = set_k(g_next_k++);
    ts_publish(&s);
    g_in_hook = 0;
}

static bool test4_seqlock(void)
{
    static const char *const rname[] = { "after acquire load", "mid copy", "before reload" };
    static const char *const wname[] = { "after odd store", "mid fields", "before even store" };

    // (a) A complete writer update at each reader step: the read retries and
    //     returns a complete new set, never a mix.
    for (int p = TS_HOOK_READER_SEQ1; p <= TS_HOOK_READER_BEFORE_SEQ2; p++) {
        ts_core_init(); ts_test_hook = NULL;
        ts_reader_t rd = {0};
        ts_snapshot_t old = set_k(1); ts_publish(&old);
        CHECK(ts_read_snapshot(&rd) && set_id(&rd.snap) == 1);
        g_next_k = 2; g_point = (ts_hook_point_t)p; g_budget = 1; ts_test_hook = hook_publish_new;
        bool ok = ts_read_snapshot(&rd);
        ts_test_hook = NULL;
        CHECK(ok && set_id(&rd.snap) == 2 && !(rd.snap.flags & TS_SYNC_STALE));
        printf("    (a) writer update %-18s: read returned complete set k=%" PRId64 "\n",
               rname[p - TS_HOOK_READER_SEQ1], set_id(&rd.snap));
    }

    // (b) Writer pre-empted at each step of its update while a reader runs:
    //     all 3 attempts fail, SYNC_STALE is set and the last good set returned.
    for (int p = TS_HOOK_WRITER_ODD; p <= TS_HOOK_WRITER_BEFORE_EVEN; p++) {
        ts_core_init(); ts_test_hook = NULL;
        ts_snapshot_t old = set_k(1); ts_publish(&old);
        g_inner_reader = (ts_reader_t){0};
        CHECK(ts_read_snapshot(&g_inner_reader) && set_id(&g_inner_reader.snap) == 1);
        g_point = (ts_hook_point_t)p; ts_test_hook = hook_read_inside_writer;
        ts_snapshot_t s2 = set_k(2); ts_publish(&s2);
        ts_test_hook = NULL;
        CHECK(!g_inner_ok);
        CHECK(g_inner_reader.snap.flags & TS_SYNC_STALE);
        CHECK(set_id(&g_inner_reader.snap) == 1);
        CHECK(g_inner_reader.stale_count == 1);
        CHECK(ts_read_snapshot(&g_inner_reader) && set_id(&g_inner_reader.snap) == 2
              && !(g_inner_reader.snap.flags & TS_SYNC_STALE));
        printf("    (b) writer pre-empted %-17s: SYNC_STALE set, last good k=1 returned\n",
               wname[p - TS_HOOK_WRITER_ODD]);
    }

    // (c) Exactly 3 forced failures: SYNC_STALE and the last good set (k=10),
    //     not any of the three sets published during the read.
    {
        ts_core_init(); ts_test_hook = NULL;
        ts_reader_t rd = {0};
        ts_snapshot_t s = set_k(10); ts_publish(&s);
        CHECK(ts_read_snapshot(&rd) && set_id(&rd.snap) == 10);
        g_next_k = 11; g_point = TS_HOOK_READER_MID; g_budget = 3; ts_test_hook = hook_publish_new;
        bool ok = ts_read_snapshot(&rd);
        ts_test_hook = NULL;
        CHECK(!ok && (rd.snap.flags & TS_SYNC_STALE) && (rd.snap.flags & TS_SNAP_VALID));
        CHECK(set_id(&rd.snap) == 10 && rd.stale_count == 1);
        printf("    (c) 3 forced failures: SYNC_STALE set, last good k=%" PRId64 " returned (k=11..13 published)\n",
               set_id(&rd.snap));
        // Boundary: 2 forced failures, then a clean third attempt returns the newest set.
        g_next_k = 20; g_budget = 2; ts_test_hook = hook_publish_new;
        ok = ts_read_snapshot(&rd);
        ts_test_hook = NULL;
        CHECK(ok && set_id(&rd.snap) == 21 && !(rd.snap.flags & TS_SYNC_STALE));
        printf("    (c) 2 forced failures: third attempt fresh, k=%" PRId64 ", SYNC_STALE clear\n",
               set_id(&rd.snap));
    }

    // (d) Pseudo-random interleaving, 200,000 reads (plan choice: enough to hit
    //     every hook combination many times; the on-target count is T-X-05's).
    {
        ts_core_init(); ts_test_hook = NULL;
        ts_reader_t rd = {0};
        g_next_k = 1;
        ts_snapshot_t s = set_k(g_next_k++); ts_publish(&s);
        CHECK(ts_read_snapshot(&rd));
        ts_test_hook = hook_random;
        long fresh = 0, stale = 0, torn = 0;
        for (long i = 0; i < 200000; i++) {
            int64_t before = set_id(&rd.snap);
            uint32_t k_lo = g_next_k - 1;
            bool ok = ts_read_snapshot(&rd);
            uint32_t k_hi = g_next_k - 1;
            int64_t id = set_id(&rd.snap);
            if (id < 0) torn++;
            else if (ok && (id < (int64_t)k_lo || id > (int64_t)k_hi)) torn++;
            else if (!ok && id != before) torn++;
            if (ok) fresh++; else stale++;
        }
        ts_test_hook = NULL;
        printf("    (d) 200000 random interleavings: %ld fresh, %ld SYNC_STALE (last good), %ld torn\n",
               fresh, stale, torn);
        CHECK(torn == 0 && stale > 0 && fresh > 0);
        CHECK(rd.stale_count == (uint32_t)stale);
    }
    return true;
}

/* ======================================================================== */
/* Supplementary checks (not counted in Pass = N of 4).                      */
static bool supp_arithmetic(void)
{
    // T-B4-04 form: frame_index at the deadline of tick k equals k mod 600, k = 0..1199.
    for (int64_t origin = 0; origin <= 4000000000000LL; origin += 1999999999999LL)
        for (uint64_t k = 0; k < 1200; k++)
            CHECK(frame_index(ts_deadline_pico(origin, k), origin, 600) == k % 600);
    // No accumulation: every 60 ticks are exactly 1,000,000 us, for large n too.
    for (uint64_t m = 0; m < 5000000000ULL; m = m * 3 + 1)
        CHECK(ts_deadline_pico(0, 60 * m) == (int64_t)m * 1000000);
    CHECK(ts_deadline_pico(0, 1) == 16667 && ts_deadline_pico(0, 2) == 33334 && ts_deadline_pico(0, 3) == 50000);
    // Before origin: the timeline continues backwards.
    CHECK(frame_index(999999, 1000000, 600) == 599);
    CHECK(frame_index(1000000 - 1000000, 1000000, 600) == 540);
    CHECK(frame_index(5, 0, 0) == 0);
    // Exact range of ts_deadline_pico for origin = 0, and saturation above it.
    uint64_t nmax = 60u * (uint64_t)((INT64_MAX - 1000000) / 1000000) + 59u;
    __int128 ref = (__int128)nmax * 1000000;
    ref = (ref + 59) / 60;
    CHECK((__int128)ts_deadline_pico(0, nmax) == ref);
    CHECK(ts_deadline_pico(0, nmax + 1) == INT64_MAX);
    printf("    deadline exact up to n = %" PRIu64 " for origin 0\n", nmax);
    // Random comparison against 128-bit references.
    uint64_t x = 88172645463325252ULL;
    for (int i = 0; i < 1000000; i++) {
        x ^= x << 13; x ^= x >> 7; x ^= x << 17;
        int64_t t = (int64_t)(x >> 2);                       // 0 .. 2^62
        int64_t o = (int64_t)((x * 2654435761u) >> 2);
        uint32_t fc = (uint32_t)(x % 1023u) + 1u;
        __int128 d = (__int128)t - o;
        __int128 f = d * 60;
        __int128 fl = f / 1000000; if (f % 1000000 < 0) fl--;
        __int128 m = fl % fc; if (m < 0) m += fc;
        CHECK(frame_index(t, o, fc) == (uint32_t)m);
        int64_t dt = (int64_t)(x % 2000000000000000000ULL) - 1000000000000000000LL;   // |dt| < 10^18
        int32_t ppb = (int32_t)(uint32_t)(x >> 32);
        __int128 p = (__int128)dt * ppb;
        __int128 pl = p / 1000000000; if (p % 1000000000 < 0) pl--;
        CHECK((__int128)ts_scale_ppb(dt, ppb) == pl);
    }
    // Round trip Pico -> local -> Pico within 2 us at 50 ppm and 400 s.
    ts_snapshot_t s = { .offset_us = 777777, .drift_ppb = 50000, .ref_us = 1000000000, .flags = TS_SNAP_VALID };
    for (int64_t dt = -400000000; dt <= 400000000; dt += 9999991) {
        int64_t tp = 1000000000 + dt;
        int64_t back = ts_pico_from_local(&s, ts_local_from_pico(&s, tp));
        CHECK(back - tp >= -2 && back - tp <= 2);
    }
    CHECK(timesync_sync_ready() == false);
    return true;
}

int main(void)
{
    struct { const char *name; bool (*fn)(void); } tests[] = {
        { "Test 1: slew int8_t round trip and limit at -100/+100", test1_slew_int8 },
        { "Test 2: 16-sample minimum-delay filter, known minimum", test2_min_filter },
        { "Test 3: 2 ms offset step absorbed in exactly 20 ticks", test3_step_20_ticks },
        { "Test 4: forced writer interleaving, SYNC_STALE after 3 failures", test4_seqlock },
    };
    int pass = 0;
    for (int i = 0; i < 4; i++) {
        printf("%s\n", tests[i].name);
        bool ok = tests[i].fn();
        printf("  %s\n", ok ? "PASS" : "FAIL");
        pass += ok;
    }
    printf("Supplementary arithmetic checks (not counted)\n");
    bool supp = supp_arithmetic();
    printf("  %s\n", supp ? "PASS" : "FAIL");
    printf("Pass = %d of 4\n", pass);
    return (pass == 4 && supp) ? 0 : 1;
}
