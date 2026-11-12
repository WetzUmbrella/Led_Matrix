/**
 * @file    timesync.c
 * @brief   Time Sync Task and the corrected Pico clock (T-B4-02).
 *
 * Owner: Ahsanul (Buddy 4). See timesync.h for the time base and units, and
 * timesync_DEFENCE.md for where each rule comes from.
 *
 * DEVIATION D-1 (module-wide): C11 stdatomic.h for the seqlock (VC-1).
 * DEVIATION D-2 (module-wide): FreeRTOS and ESP-IDF names and macros in the
 * Time Sync Task section (QueueHandle_t, TaskHandle_t, pdTRUE,
 * pdMS_TO_TICKS).
 */
#include "timesync.h"

#include <stddef.h>
#include <string.h>

#ifndef TIMESYNC_HOST_TEST
#include <inttypes.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#endif

/* Signed copies of the public constants, so signed arithmetic never mixes
 * with unsigned operands (Barr-C 5.3.c). */
#define FPS_S64        INT64_C(60)
#define US_PER_S_S64   INT64_C(1000000)
#define PPB_PER_S64    INT64_C(1000000000)
#define FPS_U64        UINT64_C(60)
#define US_PER_S_U64   UINT64_C(1000000)
#define LOW_WORD_MASK  UINT64_C(0xFFFFFFFF)
#define WORD_BITS      32u
#define SEQ_STEP       1u
#define SEQ_EVEN_STEP  2u
#define SEQ_FIRST_EVEN 2u
#define SEQ_ODD_BIT    1u
#define READY_WINDOW   0x1u
#define READY_DRIFT    0x2u

/** (plan choice) 100 ms: one beacon period at 10 Hz, so samples are still
 *  drained at the beacon rate when no tick notification arrives. */
#define TIMESYNC_WAIT_MS 100u

/** Index of each 32-bit word of the published set. */
typedef enum
{
    WORD_OFFSET_LOW = 0,
    WORD_OFFSET_HIGH,
    WORD_DRIFT,
    WORD_REF_LOW,
    WORD_REF_HIGH,
    WORD_COUNT
} seqlock_word_t;

/**
 * The published set: one sequence counter over offset, drift and reference.
 * Each field is stored as relaxed 32-bit atomic words, which are lock-free on
 * Xtensa; a 64-bit _Atomic would take a lock (VC-1).
 */
typedef struct
{
    _Atomic uint32_t seq; /**< Even: stable. Odd: write in progress.
                               0: never published. */
    _Atomic uint32_t words[WORD_COUNT];
} seqlock_t;

/** Index of each 32-bit word of the SYNC_READY inputs. */
typedef enum
{
    READY_NEWEST_LOW = 0,
    READY_NEWEST_HIGH,
    READY_FLAGS,
    READY_WORD_COUNT
} ready_word_t;

/** SYNC_READY inputs (OD-10), published by the Time Sync Task with the same
 *  seqlock steps as the VC-1 set but under their own counter, so the VC-1
 *  set itself is unchanged. */
typedef struct
{
    _Atomic uint32_t seq; /**< Even: stable. Odd: write in progress. */
    _Atomic uint32_t words[READY_WORD_COUNT];
} ready_lock_t;

/** Writer state, used by the Time Sync Task only. */
typedef struct
{
    int64_t  t_p[TIMESYNC_WINDOW]; /**< Ring of the last 16 Pico times. */
    int64_t  t_l[TIMESYNC_WINDOW]; /**< Ring of the last 16 local times. */
    uint32_t head;                 /**< Next slot to write. */
    uint32_t count;                /**< 0 to TIMESYNC_WINDOW. */

    bool    b_est_valid; /**< The filter has an estimate. */
    int64_t est_offset;  /**< Smallest (t_l - t_p) in the window. */
    int64_t est_ref;     /**< t_l of that sample. */
    bool    b_drift_valid;
    int32_t drift_ppb; /**< Held until a new estimate replaces it. */

    bool                b_pub_valid; /**< Something was published. */
    timesync_snapshot_t pub;         /**< Writer's copy of what it published. */
    int8_t              last_slew;   /**< Slew of the last tick. */
    uint32_t            reseeds;     /**< Re-seeds since init (R1-2). */
    uint32_t            off_run;     /**< Off samples in a row so far. */
    uint32_t            dropped;     /**< Off samples dropped since init. */
} writer_state_t;

static seqlock_t      g_pub;
static ready_lock_t   g_ready;
static writer_state_t g_writer;

#ifdef TIMESYNC_HOST_TEST
static timesync_hook_fn_t g_p_test_hook = NULL;
static int64_t            g_test_now_us = 0;
#endif

#ifndef TIMESYNC_HOST_TEST
/** Handle of the Time Sync Task, published once the task runs. */
static _Atomic(TaskHandle_t) g_p_sync_task = NULL;
#endif

static void     hook_point_reached (timesync_hook_point_t point);
static bool     reseed_needed (int64_t t_p, int64_t t_l);
static void     window_add (int64_t t_p, int64_t t_l);
static void     ready_publish (int64_t newest_t_l, uint32_t flags);
static int64_t  floor_div (int64_t numerator, int64_t divisor);
static uint32_t low_word (int64_t value);
static uint32_t high_word (int64_t value);
static int64_t  join_words (uint32_t low, uint32_t high);
static bool     ready_read (int64_t * p_newest_us, uint32_t * p_flags);
static void     min_delay_sample (uint32_t first, uint32_t length,
                                  int64_t * p_offset, int64_t * p_ref);

/**
 * @brief  floor(dt x ppb / 10^9) without forming dt x ppb, which overflows
 *         int64_t once |dt| exceeds about 4.3 x 10^9 us at 2^31 ppb. Split
 *         dt = q x 10^9 + r with 0 <= r < 10^9: then r x ppb < 2^61 and
 *         q x ppb is exact while |q| <= 2^31. Exact for |dt| < 2^31 x 10^9 us
 *         (about 68,000 years).
 * @param  dt_us      Elapsed time, microseconds.
 * @param  drift_ppb  Drift, parts per billion.
 * @return The scaled time, microseconds.
 */
int64_t timesync_scale_ppb (int64_t dt_us, int32_t drift_ppb)
{
    int64_t const quot = floor_div(dt_us, PPB_PER_S64);
    int64_t const rem  = dt_us - (quot * PPB_PER_S64);

    /* Widening int32_t to int64_t keeps every value. */
    int64_t const ppb = (int64_t)drift_ppb;

    return (quot * ppb) + floor_div(rem * ppb, PPB_PER_S64);
}

/**
 * @brief  Seqlock writer: increment to odd, release fence, write the fields
 *         as relaxed 32-bit atomic halves, store even with release (VC-1).
 *         Single writer: the Time Sync Task.
 * @param  p_snap  Set to publish.
 */
void timesync_publish (timesync_snapshot_t const * p_snap)
{
    /* Only this task writes seq, so a relaxed load reads its own last store. */
    uint32_t const seq = atomic_load_explicit(&g_pub.seq, memory_order_relaxed);

    atomic_store_explicit(&g_pub.seq, seq + SEQ_STEP, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    hook_point_reached(TIMESYNC_HOOK_WRITER_ODD);

    atomic_store_explicit(&g_pub.words[WORD_OFFSET_LOW],
                          low_word(p_snap->offset_us), memory_order_relaxed);
    atomic_store_explicit(&g_pub.words[WORD_OFFSET_HIGH],
                          high_word(p_snap->offset_us), memory_order_relaxed);
    hook_point_reached(TIMESYNC_HOOK_WRITER_MID);

    /* Two's complement bit pattern of the drift; the reader casts it back. */
    atomic_store_explicit(&g_pub.words[WORD_DRIFT], (uint32_t)p_snap->drift_ppb,
                          memory_order_relaxed);
    atomic_store_explicit(&g_pub.words[WORD_REF_LOW], low_word(p_snap->ref_us),
                          memory_order_relaxed);
    atomic_store_explicit(&g_pub.words[WORD_REF_HIGH],
                          high_word(p_snap->ref_us), memory_order_relaxed);
    hook_point_reached(TIMESYNC_HOOK_WRITER_BEFORE_EVEN);

    /* The even value skips 0 on wrap, because 0 means "never published".
     * Wrap needs 2^31 publications: 414 days at one per 60 fps tick. */
    uint32_t even = seq + SEQ_EVEN_STEP;

    if (0u == even)
    {
        even = SEQ_FIRST_EVEN;
    }

    atomic_store_explicit(&g_pub.seq, even, memory_order_release);
}

/**
 * @brief  Seqlock reader: acquire load, copy fields, acquire fence, reload;
 *         retry if odd or changed. After TIMESYNC_READ_ATTEMPTS failed
 *         attempts keep the reader's last good snapshot and set SYNC_STALE.
 *         The loop is bounded, so a reader never spins for long on a
 *         pre-empted writer (VB-4c, PostTri Version C row 1).
 * @param  p_reader  This task's reader.
 * @return false when the read ended in SYNC_STALE.
 */
bool timesync_read_snapshot (timesync_reader_t * p_reader)
{
    bool     b_done  = false;
    uint32_t attempt = 0u;

    while ((!b_done) && (attempt < TIMESYNC_READ_ATTEMPTS))
    {
        uint32_t const seq_1 =
            atomic_load_explicit(&g_pub.seq, memory_order_acquire);

        hook_point_reached(TIMESYNC_HOOK_READER_SEQ1);

        uint32_t const off_low = atomic_load_explicit(
            &g_pub.words[WORD_OFFSET_LOW], memory_order_relaxed);
        uint32_t const off_high = atomic_load_explicit(
            &g_pub.words[WORD_OFFSET_HIGH], memory_order_relaxed);

        hook_point_reached(TIMESYNC_HOOK_READER_MID);

        uint32_t const drift   = atomic_load_explicit(&g_pub.words[WORD_DRIFT],
                                                      memory_order_relaxed);
        uint32_t const ref_low = atomic_load_explicit(
            &g_pub.words[WORD_REF_LOW], memory_order_relaxed);
        uint32_t const ref_high = atomic_load_explicit(
            &g_pub.words[WORD_REF_HIGH], memory_order_relaxed);

        atomic_thread_fence(memory_order_acquire);
        hook_point_reached(TIMESYNC_HOOK_READER_BEFORE_SEQ2);

        uint32_t const seq_2 =
            atomic_load_explicit(&g_pub.seq, memory_order_relaxed);

        if ((0u == (seq_1 & SEQ_ODD_BIT)) && (seq_1 == seq_2))
        {
            if (0u == seq_1)
            {
                /* Nothing published yet: not valid, not stale. */
                p_reader->snap.flags = 0u;
            }
            else
            {
                p_reader->snap.offset_us = join_words(off_low, off_high);

                /* Inverse of the writer's cast: the same 32-bit pattern. */
                p_reader->snap.drift_ppb = (int32_t)drift;
                p_reader->snap.ref_us    = join_words(ref_low, ref_high);
                p_reader->snap.flags     = TIMESYNC_SNAP_VALID;
            }

            b_done = true;
        }

        attempt++;
    }

    if (!b_done)
    {
        /* The fields keep the last good set. */
        p_reader->snap.flags |= TIMESYNC_SYNC_STALE;
        p_reader->stale_count++;
    }

    return b_done;
}

/**
 * @brief  Pico time at local time t_l: the offset at t_l is
 *         offset + drift x (t_l - ref).
 * @param  p_snap  Snapshot.
 * @param  t_l     Local time, microseconds.
 * @return Pico time, microseconds.
 */
int64_t timesync_pico_from_local (timesync_snapshot_t const * p_snap,
                                  int64_t                     t_l)
{
    int64_t const offset =
        p_snap->offset_us +
        timesync_scale_ppb(t_l - p_snap->ref_us, p_snap->drift_ppb);

    return t_l - offset;
}

/**
 * @brief  First-order inverse of timesync_pico_from_local(). The error against
 *         the exact inverse is drift^2 x (t - ref) plus 1 us from each floor:
 *         under 2 us for |drift| <= 50 ppm (VA-4.7) and |t - ref| <= 400 s. In
 *         use t - ref is about one tick, because the reference moves on every
 *         tick.
 * @param  p_snap  Snapshot.
 * @param  t_p     Pico time, microseconds.
 * @return Local time, microseconds.
 */
int64_t timesync_local_from_pico (timesync_snapshot_t const * p_snap,
                                  int64_t                     t_p)
{
    int64_t const approx = t_p + p_snap->offset_us;

    return approx +
           timesync_scale_ppb(approx - p_snap->ref_us, p_snap->drift_ppb);
}

/**
 * @brief  Pico-time deadline of tick n: origin + n x 1,000,000 / 60 us,
 *         computed from n every time (never by adding 16,667 us). Rounded up
 *         (plan choice) so that frame_index(deadline_pico(o, n), o, fc) is
 *         n mod fc for every n: the deadline of tick n always lies inside
 *         frame n. Split n = 60q + r (0 <= r < 60): n x 10^6 / 60 =
 *         q x 10^6 + r x 10^6 / 60, and r x 10^6 < 6 x 10^7, so nothing
 *         overflows before the final add. Exact for 0 <= n <=
 *         60 x floor((INT64_MAX - 10^6 - max(origin, 0)) / 10^6) + 59; above
 *         that the result saturates at INT64_MAX.
 * @param  origin_us  Timeline origin in Pico time, microseconds.
 * @param  tick       Tick number n.
 * @return The deadline, microseconds.
 */
int64_t timesync_deadline_pico (int64_t origin_us, uint64_t tick)
{
    int64_t        deadline   = INT64_MAX;
    uint64_t const quot       = tick / FPS_U64;
    uint64_t const rem        = tick % FPS_U64;
    int64_t const  positive   = (origin_us > 0) ? origin_us : 0;
    int64_t const  headroom   = INT64_MAX - US_PER_S_S64 - positive;
    int64_t const  max_quot_s = headroom / US_PER_S_S64;

    /* headroom >= 0 because origin < 2^62, so the value fits uint64_t. */
    if (quot <= (uint64_t)max_quot_s)
    {
        /* quot <= max_quot_s, which is an int64_t value. */
        int64_t const whole = (int64_t)quot * US_PER_S_S64;

        /* rem < 60, so the ceiling is below 10^6 and fits int64_t. */
        int64_t const part =
            (int64_t)(((rem * US_PER_S_U64) + FPS_U64 - 1u) / FPS_U64);

        deadline = origin_us + whole + part;
    }

    return deadline;
}

/**
 * @brief  The first tick strictly after t. With d = t - origin >= 0 and
 *         n0 = floor(d x 60 / 10^6): n0 x 10^6 / 60 <= d, so the rounded-up
 *         deadline of n0 is still <= t (d is an integer), while that of
 *         n0 + 1 is > t. So the answer is n0 + 1, computed with the same split
 *         as timesync_frame_index().
 * @param  origin_us  Timeline origin, microseconds.
 * @param  t_us       Corrected Pico time, microseconds.
 * @return Tick number.
 */
uint64_t timesync_next_tick (int64_t origin_us, int64_t t_us)
{
    uint64_t tick = 0u;

    if (t_us >= origin_us)
    {
        int64_t const delta = t_us - origin_us;
        int64_t const quot  = delta / US_PER_S_S64;
        int64_t const rem   = delta - (quot * US_PER_S_S64);
        int64_t const frames =
            (quot * FPS_S64) + ((rem * FPS_S64) / US_PER_S_S64);

        /* delta >= 0, so frames >= 0 and fits uint64_t. */
        tick = (uint64_t)frames + 1u;
    }

    return tick;
}

/**
 * @brief  floor((t - origin) x 60 / 1,000,000) mod frame_count, integers
 *         only. Split d = t - origin = q x 10^6 + r (0 <= r < 10^6, floor
 *         division) so the product never forms: frames = 60q +
 *         floor(60r / 10^6). Exact for every d that fits int64_t, which holds
 *         whenever t and origin are both >= 0. Before origin (d < 0) the floor
 *         and a non-negative modulo continue the timeline backwards: t =
 *         origin - 1 us gives frame_count - 1. Playback Control (T-B4-04)
 *         stays ARMED until t_start, so it never shows these. frame_count 0
 *         (no asset) returns 0.
 * @param  t_us         Corrected Pico time, microseconds.
 * @param  origin_us    Timeline origin, microseconds.
 * @param  frame_count  Frames in the asset.
 * @return Frame index.
 */
uint32_t timesync_frame_index (int64_t t_us, int64_t origin_us,
                               uint32_t frame_count)
{
    uint32_t index = 0u;

    if (0u != frame_count)
    {
        int64_t const delta = t_us - origin_us;
        int64_t const quot  = floor_div(delta, US_PER_S_S64);
        int64_t const rem   = delta - (quot * US_PER_S_S64);
        int64_t const frames =
            (quot * FPS_S64) + ((rem * FPS_S64) / US_PER_S_S64);

        /* Widening uint32_t to int64_t keeps every value. */
        int64_t const count = (int64_t)frame_count;
        int64_t       mod   = frames % count;

        if (mod < 0)
        {
            mod += count;
        }

        /* 0 <= mod < frame_count, which fits uint32_t. */
        index = (uint32_t)mod;
    }

    return index;
}

/**
 * @brief  Reset everything; call before any task starts.
 */
void timesync_init (void)
{
    (void)memset(&g_writer, 0, sizeof(g_writer)); /* Result not needed. */
    atomic_store_explicit(&g_pub.seq, 0u, memory_order_relaxed);

    /* WORD_COUNT is a small positive enum constant (5): it fits uint32_t. */
    for (uint32_t word = 0u; word < (uint32_t)WORD_COUNT; word++)
    {
        atomic_store_explicit(&g_pub.words[word], 0u, memory_order_relaxed);
    }

    atomic_store_explicit(&g_ready.seq, 0u, memory_order_relaxed);

    /* READY_WORD_COUNT is a small positive enum constant (3). */
    for (uint32_t word = 0u; word < (uint32_t)READY_WORD_COUNT; word++)
    {
        atomic_store_explicit(&g_ready.words[word], 0u, memory_order_relaxed);
    }
}

/**
 * @brief  Add one (t_p, t_l) pair. The filter offset comes from the sample
 *         with the smallest (t_l - t_p). Drift is the slope between the
 *         filtered offsets of the older and newer half of the full window; it
 *         is held (not cleared) when it cannot be computed. A sample more
 *         than TIMESYNC_RESEED_US off the estimate is dropped, unless it is
 *         the TIMESYNC_RESEED_SAMPLES-th off sample in a row: then the
 *         window is emptied and it starts a new one (R1-2).
 * @param  t_p  Pico time from the beacon, microseconds.
 * @param  t_l  Local receive time, microseconds.
 */
void timesync_add_sample (int64_t t_p, int64_t t_l)
{
    bool const b_off = reseed_needed(t_p, t_l);

    g_writer.off_run = b_off ? (g_writer.off_run + 1u) : 0u;

    if ((b_off) && (g_writer.off_run < TIMESYNC_RESEED_SAMPLES))
    {
        /* One or two off samples: a late beacon, not another clock. Kept
         * out of the window, so the estimate does not move. */
        g_writer.dropped++;
    }
    else if (b_off)
    {
        /* Another Pico clock (R1-2): forget the window, the estimates and
         * the published offset, so this sample starts a new window and the
         * next tick publishes it directly. SYNC_READY stays false until the
         * window is full again (1.6 s). */
        g_writer.head          = 0u;
        g_writer.count         = 0u;
        g_writer.b_est_valid   = false;
        g_writer.b_drift_valid = false;
        g_writer.drift_ppb     = 0;
        g_writer.b_pub_valid   = false;
        g_writer.off_run       = 0u;
        g_writer.reseeds++;
    }
    else
    {
        /* In line with the estimate. */
    }

    if ((!b_off) || (0u == g_writer.off_run))
    {
        window_add(t_p, t_l);
    }
}

/**
 * @brief  Put one sample in the window and update the estimates and the
 *         SYNC_READY state (the body of timesync_add_sample()).
 * @param  t_p  Pico time from the beacon, microseconds.
 * @param  t_l  Local receive time, microseconds.
 */
static void window_add (int64_t t_p, int64_t t_l)
{
    g_writer.t_p[g_writer.head] = t_p;
    g_writer.t_l[g_writer.head] = t_l;
    g_writer.head               = (g_writer.head + 1u) % TIMESYNC_WINDOW;

    if (g_writer.count < TIMESYNC_WINDOW)
    {
        g_writer.count++;
    }

    min_delay_sample(0u, g_writer.count, &g_writer.est_offset,
                     &g_writer.est_ref);
    g_writer.b_est_valid = true;

    if (TIMESYNC_WINDOW == g_writer.count)
    {
        int64_t off_old = 0;
        int64_t ref_old = 0;
        int64_t off_new = 0;
        int64_t ref_new = 0;

        min_delay_sample(0u, TIMESYNC_HALF, &off_old, &ref_old);
        min_delay_sample(TIMESYNC_HALF, TIMESYNC_HALF, &off_new, &ref_new);

        int64_t const diff  = off_new - off_old;
        int64_t const span  = ref_new - ref_old;
        int64_t const limit = INT64_MAX / PPB_PER_S64;

        if ((span > 0) && (diff <= limit) && (diff >= -limit))
        {
            int64_t const ppb = floor_div(diff * PPB_PER_S64, span);

            if ((ppb >= INT32_MIN) && (ppb <= INT32_MAX))
            {
                /* Range checked on the line above. */
                g_writer.drift_ppb     = (int32_t)ppb;
                g_writer.b_drift_valid = true;
            }
        }
    }

    uint32_t flags = 0u;

    if (TIMESYNC_WINDOW == g_writer.count)
    {
        flags |= READY_WINDOW;
    }

    if (g_writer.b_drift_valid)
    {
        flags |= READY_DRIFT;
    }

    ready_publish(t_l, flags);
}

/**
 * @brief  Limit a request to -100 to +100 us and store it as int8_t.
 * @param  request_us  Requested correction, microseconds.
 * @return The limited correction.
 */
int8_t timesync_slew_clamp (int64_t request_us)
{
    int64_t limited = request_us;

    if (limited > TIMESYNC_SLEW_MAX_US)
    {
        limited = TIMESYNC_SLEW_MAX_US;
    }

    if (limited < -TIMESYNC_SLEW_MAX_US)
    {
        limited = -TIMESYNC_SLEW_MAX_US;
    }

    /* limited is in -100 to +100, which fits int8_t. */
    return (int8_t)limited;
}

/**
 * @brief  One tick. The published offset moves towards the filter's estimate
 *         by at most 100 us and is never stepped, except after a re-seed
 *         (R1-2), which is handled as a first publication. Each
 *         publication is rebased to t_local, so a new drift value changes the
 *         slope from t_local on but causes no jump at t_local itself. The
 *         very first publication sets the offset directly: it is the first
 *         estimate, not a correction of an earlier one.
 * @param  t_local  Local time of the tick, microseconds.
 */
void timesync_tick (int64_t t_local)
{
    if (g_writer.b_est_valid)
    {
        int32_t const drift = g_writer.b_drift_valid ? g_writer.drift_ppb : 0;
        int64_t const target =
            g_writer.est_offset +
            timesync_scale_ppb(t_local - g_writer.est_ref, drift);

        timesync_snapshot_t next = {
            .drift_ppb = drift,
            .ref_us    = t_local,
            .flags     = TIMESYNC_SNAP_VALID,
        };

        if (!g_writer.b_pub_valid)
        {
            next.offset_us     = target;
            g_writer.last_slew = 0;
        }
        else
        {
            int64_t const applied =
                g_writer.pub.offset_us +
                timesync_scale_ppb(t_local - g_writer.pub.ref_us,
                                   g_writer.pub.drift_ppb);
            int8_t const slew = timesync_slew_clamp(target - applied);

            next.offset_us     = applied + slew;
            g_writer.last_slew = slew;
        }

        timesync_publish(&next);
        g_writer.pub         = next;
        g_writer.b_pub_valid = true;
    }
}

/**
 * @brief  Corrected Pico time now, read only through
 *         timesync_read_snapshot() (RAR-3). With nothing published yet it
 *         returns local time unchanged.
 * @param  p_reader  This task's reader.
 * @return Corrected Pico time, microseconds.
 */
int64_t timesync_now_corrected (timesync_reader_t * p_reader)
{
    int64_t const t_l   = timesync_platform_now_us();
    int64_t       t_now = t_l;

    /* A stale read leaves the last good set in the reader; it is used. */
    (void)timesync_read_snapshot(p_reader);

    if (0u != (p_reader->snap.flags & TIMESYNC_SNAP_VALID))
    {
        t_now = timesync_pico_from_local(&p_reader->snap, t_l);
    }

    return t_now;
}

/**
 * @brief  Local alarm time for tick n, read only through
 *         timesync_read_snapshot() (RAR-3). With nothing published yet it
 *         returns the Pico-time deadline unchanged, matching the T-B5-01 stub
 *         behaviour (local time). The signature is a proposal to be agreed
 *         with Jia Jun (T-B5-01).
 * @param  p_reader   This task's reader.
 * @param  origin_us  Timeline origin in Pico time, microseconds.
 * @param  tick       Tick number n.
 * @return Local deadline, microseconds.
 */
int64_t timesync_deadline_local (timesync_reader_t * p_reader,
                                 int64_t origin_us, uint64_t tick)
{
    int64_t const t_p      = timesync_deadline_pico(origin_us, tick);
    int64_t       deadline = t_p;

    /* A stale read leaves the last good set in the reader; it is used. */
    (void)timesync_read_snapshot(p_reader);

    if (0u != (p_reader->snap.flags & TIMESYNC_SNAP_VALID))
    {
        deadline = timesync_local_from_pico(&p_reader->snap, t_p);
    }

    return deadline;
}

/**
 * @brief  Read the SYNC_READY inputs under their sequence counter: acquire
 *         load, copy, acquire fence and reload, at most
 *         TIMESYNC_READ_ATTEMPTS times.
 * @param  p_newest_us  Receives the local time of the newest sample.
 * @param  p_flags      Receives READY_WINDOW and READY_DRIFT.
 * @return true when a stable, published set was read.
 */
static bool ready_read (int64_t * p_newest_us, uint32_t * p_flags)
{
    bool     b_done  = false;
    bool     b_valid = false;
    uint32_t attempt = 0u;

    while ((!b_done) && (attempt < TIMESYNC_READ_ATTEMPTS))
    {
        uint32_t const seq_1 =
            atomic_load_explicit(&g_ready.seq, memory_order_acquire);
        uint32_t const newest_low = atomic_load_explicit(
            &g_ready.words[READY_NEWEST_LOW], memory_order_relaxed);
        uint32_t const newest_high = atomic_load_explicit(
            &g_ready.words[READY_NEWEST_HIGH], memory_order_relaxed);
        uint32_t const flags = atomic_load_explicit(&g_ready.words[READY_FLAGS],
                                                    memory_order_relaxed);

        atomic_thread_fence(memory_order_acquire);

        uint32_t const seq_2 =
            atomic_load_explicit(&g_ready.seq, memory_order_relaxed);

        if ((0u == (seq_1 & SEQ_ODD_BIT)) && (seq_1 == seq_2))
        {
            *p_newest_us = join_words(newest_low, newest_high);
            *p_flags     = flags;
            b_valid      = (0u != seq_1);
            b_done       = true;
        }

        attempt++;
    }

    return b_valid;
}

/**
 * @brief  SYNC_READY rule (OD-10): window full, drift available and the
 *         newest sample no older than TIMESYNC_HOLDOVER_US. See timesync.h.
 *         A read that never sees a stable set returns false.
 * @return true when the tile may report SYNC_READY.
 */
bool timesync_sync_ready (void)
{
    int64_t const  now_us = timesync_platform_now_us();
    int64_t        newest = 0;
    uint32_t       flags  = 0u;
    uint32_t const needed = READY_WINDOW | READY_DRIFT;
    bool const     b_read = ready_read(&newest, &flags);

    return (b_read) && (needed == (flags & needed)) &&
           ((now_us - newest) <= TIMESYNC_HOLDOVER_US);
}

/**
 * @brief  Age of the newest beacon sample (T-B4-04 holdover input).
 * @return Local microseconds since the newest sample, or INT64_MAX when no
 *         sample is published or no stable read was possible.
 */
int64_t timesync_newest_age_us (void)
{
    int64_t  newest = 0;
    uint32_t flags  = 0u;
    int64_t  age_us = INT64_MAX;

    if (ready_read(&newest, &flags))
    {
        age_us = timesync_platform_now_us() - newest;
    }

    return age_us;
}

/**
 * @brief  Current filter estimate, for the host tests.
 * @param  p_offset_us  Receives the estimated offset.
 * @param  p_ref_us     Receives its reference time.
 * @return false until the first sample.
 */
bool timesync_filter_estimate (int64_t * p_offset_us, int64_t * p_ref_us)
{
    if (g_writer.b_est_valid)
    {
        *p_offset_us = g_writer.est_offset;
        *p_ref_us    = g_writer.est_ref;
    }

    return g_writer.b_est_valid;
}

/**
 * @brief  Current drift estimate, for the host tests.
 * @param  p_drift_ppb  Receives the drift.
 * @return false until a drift is available.
 */
bool timesync_drift_estimate (int32_t * p_drift_ppb)
{
    if (g_writer.b_drift_valid)
    {
        *p_drift_ppb = g_writer.drift_ppb;
    }

    return g_writer.b_drift_valid;
}

/**
 * @brief  Number of re-seeds since timesync_init() (R1-2).
 * @return The count.
 */
uint32_t timesync_reseed_count (void)
{
    return g_writer.reseeds;
}

/**
 * @brief  Off samples dropped since timesync_init() (see timesync.h).
 * @return The count.
 */
uint32_t timesync_dropped_count (void)
{
    return g_writer.dropped;
}

/**
 * @brief  The int8_t slew of the last tick, for the host tests.
 * @return Slew in -100 to +100 us.
 */
int8_t timesync_last_slew (void)
{
    return g_writer.last_slew;
}

#ifdef TIMESYNC_HOST_TEST
/**
 * @brief  Install the forced-interleaving hook (host test build only).
 * @param  p_hook  Hook, or NULL for none.
 */
void timesync_test_set_hook (timesync_hook_fn_t p_hook)
{
    g_p_test_hook = p_hook;
}

/**
 * @brief  Set the fake local clock (host test build only).
 * @param  now_us  Local time timesync_platform_now_us() returns, microseconds.
 */
void timesync_test_set_now_us (int64_t now_us)
{
    g_test_now_us = now_us;
}

/**
 * @brief  Local time from the fake clock (host test build only).
 * @return Local time, microseconds.
 */
int64_t timesync_platform_now_us (void)
{
    return g_test_now_us;
}
#endif

#ifndef TIMESYNC_HOST_TEST
/**
 * @brief  Local time from esp_timer.
 * @return Local time, microseconds.
 */
int64_t timesync_platform_now_us (void)
{
    return esp_timer_get_time();
}

/**
 * @brief  Wake the Time Sync Task for one slew step. Called once per tick
 *         from de_task (task context, not the ISR).
 */
void timesync_notify_tick (void)
{
    TaskHandle_t const p_task =
        atomic_load_explicit(&g_p_sync_task, memory_order_acquire);

    if (NULL != p_task)
    {
        xTaskNotifyGive(p_task);
    }
}

/**
 * @brief  Time Sync Task. Wakes once per Display Engine tick, or after
 *         TIMESYNC_WAIT_MS without one, drains the Time Sync Queue into the
 *         filter and runs one slew step. Several pending ticks still give one
 *         step, so no published update moves the offset by more than 100 us.
 * @param  p_queue  QueueHandle_t of q_timesync.
 */
void timesync_task (void * p_queue)
{
    /* FreeRTOS passes the queue handle through the task parameter. */
    QueueHandle_t const p_sync_queue = (QueueHandle_t)p_queue;

    timesync_init();
    atomic_store_explicit(&g_p_sync_task, xTaskGetCurrentTaskHandle(),
                          memory_order_release);

    for (;;)
    {
        timesync_pair_t pair;

        /* The notification count is not needed: one step per wake. */
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TIMESYNC_WAIT_MS));

        while (pdTRUE == xQueueReceive(p_sync_queue, &pair, 0))
        {
            uint32_t const before  = g_writer.reseeds;
            uint32_t const dropped = g_writer.dropped;

            timesync_add_sample(pair.t_p, pair.t_l);

            if (before != g_writer.reseeds)
            {
                /* Bench evidence for R1-2 (coordinator switch). */
                printf("TS reseed n=%" PRIu32 " t_p=%" PRId64 " t_l=%" PRId64
                       "\n",
                       g_writer.reseeds, pair.t_p, pair.t_l);
            }
            else if (dropped != g_writer.dropped)
            {
                /* A late (or early) beacon, kept out of the window. */
                printf("TS dropped n=%" PRIu32 " t_p=%" PRId64 " t_l=%" PRId64
                       "\n",
                       g_writer.dropped, pair.t_p, pair.t_l);
            }
            else
            {
                /* A normal sample. */
            }
        }

        timesync_tick(esp_timer_get_time());
    }
}
#endif /* TIMESYNC_HOST_TEST */

/**
 * @brief  True when a sample's offset is more than TIMESYNC_RESEED_US away
 *         from the current estimate, so it comes from another Pico clock
 *         (R1-2). False before the first estimate.
 * @param  t_p  Pico time from the beacon, microseconds.
 * @param  t_l  Local receive time, microseconds.
 * @return true when the window must be emptied.
 */
static bool reseed_needed (int64_t t_p, int64_t t_l)
{
    bool b_needed = false;

    if (g_writer.b_est_valid)
    {
        int64_t const diff = (t_l - t_p) - g_writer.est_offset;

        b_needed = (diff > TIMESYNC_RESEED_US) || (diff < -TIMESYNC_RESEED_US);
    }

    return b_needed;
}

/**
 * @brief  Call the host test hook at a seqlock step. The firmware build has
 *         no hook, so the call compiles to nothing there.
 * @param  point  Where the writer or reader is.
 */
static void hook_point_reached (timesync_hook_point_t point)
{
#ifdef TIMESYNC_HOST_TEST
    if (NULL != g_p_test_hook)
    {
        g_p_test_hook(point);
    }
#else
    (void)point; /* No hook in the firmware build. */
#endif
}

/**
 * @brief  Publish the SYNC_READY inputs: increment to odd, release fence,
 *         relaxed word stores, store even with release (the VC-1 steps).
 *         Single writer: the Time Sync Task.
 * @param  newest_t_l  Local time of the newest sample, microseconds.
 * @param  flags       READY_WINDOW and READY_DRIFT bits.
 */
static void ready_publish (int64_t newest_t_l, uint32_t flags)
{
    /* Only this task writes seq, so a relaxed load reads its own last store. */
    uint32_t const seq =
        atomic_load_explicit(&g_ready.seq, memory_order_relaxed);

    atomic_store_explicit(&g_ready.seq, seq + SEQ_STEP, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&g_ready.words[READY_NEWEST_LOW],
                          low_word(newest_t_l), memory_order_relaxed);
    atomic_store_explicit(&g_ready.words[READY_NEWEST_HIGH],
                          high_word(newest_t_l), memory_order_relaxed);
    atomic_store_explicit(&g_ready.words[READY_FLAGS], flags,
                          memory_order_relaxed);

    /* The even value skips 0 on wrap, because 0 means "never published". */
    uint32_t even = seq + SEQ_EVEN_STEP;

    if (0u == even)
    {
        even = SEQ_FIRST_EVEN;
    }

    atomic_store_explicit(&g_ready.seq, even, memory_order_release);
}

/**
 * @brief  floor(numerator / divisor) for divisor > 0. C division truncates
 *         towards zero, so the quotient is corrected for a negative remainder.
 * @param  numerator  Dividend.
 * @param  divisor    Divisor, > 0.
 * @return The floor of the quotient.
 */
static int64_t floor_div (int64_t numerator, int64_t divisor)
{
    int64_t quot = numerator / divisor;

    if ((numerator % divisor) < 0)
    {
        quot--;
    }

    return quot;
}

/**
 * @brief  Low 32 bits of a 64-bit value.
 * @param  value  Value.
 * @return Bits 0 to 31.
 */
static uint32_t low_word (int64_t value)
{
    /* The mask keeps 32 bits, which fit uint32_t. */
    return (uint32_t)((uint64_t)value & LOW_WORD_MASK);
}

/**
 * @brief  High 32 bits of a 64-bit value.
 * @param  value  Value.
 * @return Bits 32 to 63.
 */
static uint32_t high_word (int64_t value)
{
    /* The shift leaves 32 bits, which fit uint32_t. */
    return (uint32_t)((uint64_t)value >> WORD_BITS);
}

/**
 * @brief  Join two 32-bit words into the 64-bit value they came from.
 * @param  low   Bits 0 to 31.
 * @param  high  Bits 32 to 63.
 * @return The value.
 */
static int64_t join_words (uint32_t low, uint32_t high)
{
    /* Widening casts; the final cast restores the two's complement value
     * that low_word() and high_word() split (GCC defines it as modulo). */
    return (int64_t)(((uint64_t)high << WORD_BITS) | (uint64_t)low);
}

/**
 * @brief  Minimum-delay sample among window entries first to
 *         first + length - 1, counted from the oldest (0). The first smallest
 *         wins on a tie.
 * @param  first     First entry, counted from the oldest.
 * @param  length    Number of entries, at least 1.
 * @param  p_offset  Receives the smallest (t_l - t_p).
 * @param  p_ref     Receives t_l of that sample.
 */
static void min_delay_sample (uint32_t first, uint32_t length,
                              int64_t * p_offset, int64_t * p_ref)
{
    uint32_t const oldest =
        (TIMESYNC_WINDOW == g_writer.count) ? g_writer.head : 0u;

    for (uint32_t step = 0u; step < length; step++)
    {
        uint32_t const slot   = (oldest + first + step) % TIMESYNC_WINDOW;
        int64_t const  offset = g_writer.t_l[slot] - g_writer.t_p[slot];

        if ((0u == step) || (offset < *p_offset))
        {
            *p_offset = offset;
            *p_ref    = g_writer.t_l[slot];
        }
    }
}

/*** end of file ***/
