/**
 * @file    timesync.h
 * @brief   Time Sync Task and the corrected Pico clock (T-B4-02).
 *
 * Owner: Ahsanul (Buddy 4). Plan task: T-B4-02. Readers: Playback Control
 * (T-B4-04) and the Display Engine (T-B5-01). Design defence:
 * timesync_DEFENCE.md.
 *
 * The filter, drift, slew, seqlock and frame arithmetic are pure C. The
 * FreeRTOS part (Time Sync Task, queue intake, esp_timer) is compiled only
 * when TIMESYNC_HOST_TEST is not defined, so the host tests in panel_sync/test
 * build this module with plain gcc. This header includes no FreeRTOS header,
 * so tile_net and the host tests can include it.
 *
 * Time base: every time value is a signed 64-bit count of microseconds.
 * - Pico time t_p is the Pico's clock, carried in each beacon.
 * - Local time t_l is esp_timer_get_time() on this tile.
 * - The offset is t_l - t_p (local minus Pico), so Pico time is local time
 *   minus the offset.
 * - The drift is the rate of change of the offset in parts per billion (ppb)
 *   of elapsed local time. The 50 ppm worst case (VA-4.7) is 50,000 ppb, well
 *   inside int32_t at 1 ppb steps (plan choice).
 * - The reference is the local time at which the published offset holds.
 * All times are assumed in 0 <= t < 2^62 us (about 146,000 years), so the
 * difference of two times never overflows int64_t. Each function states the
 * range in which its result is exact.
 *
 * Reader functions read the clock only through timesync_read_snapshot()
 * (RAR-3), and each reading task owns one timesync_reader_t. Never call them
 * from an ISR (PostTri shared-resource row "Corrected Pico time"). Their
 * signatures are the proposed interface to be agreed with Jia Jun (T-B5-01);
 * see timesync_DEFENCE.md, "Open interface items".
 *
 * DEVIATION D-1 (module-wide): C11 stdatomic.h for the seqlock (VC-1).
 */
#ifndef TIMESYNC_H
#define TIMESYNC_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/** Minimum-delay filter window: the last 16 beacons, 1.6 s (proposed,
 *  T-B4-02, VA-4.1). */
#define TIMESYNC_WINDOW 16u

/** Drift uses the two halves of the same window (plan choice), so it adds no
 *  constant of its own. */
#define TIMESYNC_HALF (TIMESYNC_WINDOW / 2u)

/** Slew at most 100 us per tick, stored as int8_t in -100 to +100 (proposed,
 *  T-B4-02, IFM-4b, VC-2). */
#define TIMESYNC_SLEW_MAX_US INT64_C(100)

/** A read gives up after 3 failed attempts and reports SYNC_STALE (T-B4-02,
 *  VC-1). */
#define TIMESYNC_READ_ATTEMPTS 3u

/** Frames per second of the shared timeline (T-B5-01, W6D-1a). */
#define TIMESYNC_FPS 60u

/** Microseconds per second. */
#define TIMESYNC_US_PER_S 1000000u

/** Parts per billion: the drift unit. */
#define TIMESYNC_PPB_PER_UNIT 1000000000u

/** Clock holdover: 1 s (proposed, VA-4.3, W6D-2b). SYNC_READY needs a beacon
 *  sample no older than this, and Playback Control (T-B4-04) pauses after
 *  this long without beacons. Changed only by T-B4-05 (OD-19). */
#define TIMESYNC_HOLDOVER_US INT64_C(1000000)

/** Snapshot flag: a published set was copied at least once. */
#define TIMESYNC_SNAP_VALID 0x1u

/** Snapshot flag: the last read failed TIMESYNC_READ_ATTEMPTS times, so the
 *  fields are the reader's last good set. */
#define TIMESYNC_SYNC_STALE 0x2u

/** The seqlock-protected set (VC-1). */
typedef struct
{
    int64_t  offset_us; /**< Applied (slewed) offset at ref_us. */
    int32_t  drift_ppb; /**< Drift held with offset and reference. */
    int64_t  ref_us;    /**< Local time the offset refers to. */
    uint32_t flags;     /**< TIMESYNC_SNAP_VALID, TIMESYNC_SYNC_STALE. */
} timesync_snapshot_t;

/**
 * One per reading task (Playback Control, Display Engine). It holds that
 * reader's last good snapshot, so two readers never share mutable state. It is
 * never shared between tasks.
 */
typedef struct
{
    timesync_snapshot_t snap;        /**< Last good snapshot of this reader. */
    uint32_t            stale_count; /**< Reads that ended in SYNC_STALE
                                          (T-X-05 reports it). */
} timesync_reader_t;

/**
 * One beacon observation, posted by tile_net (T-B3-03) to the Time Sync Queue.
 */
typedef struct
{
    int64_t t_p; /**< Pico time carried in the beacon, microseconds. */
    int64_t t_l; /**< Local esp_timer_get_time() at the receive hook chosen
                      in OD-09, microseconds. */
} timesync_pair_t;

/** Points in the seqlock where the host test may force an interleaving. The
 *  firmware build never calls a hook at them. */
typedef enum
{
    TIMESYNC_HOOK_WRITER_ODD = 0,     /**< After the odd store and fence. */
    TIMESYNC_HOOK_WRITER_MID,         /**< Offset written, the rest not. */
    TIMESYNC_HOOK_WRITER_BEFORE_EVEN, /**< All fields written, no even store. */
    TIMESYNC_HOOK_READER_SEQ1,        /**< After the acquire load. */
    TIMESYNC_HOOK_READER_MID,         /**< Offset copied, the rest not. */
    TIMESYNC_HOOK_READER_BEFORE_SEQ2, /**< After the acquire fence. */
    TIMESYNC_HOOK_COUNT
} timesync_hook_point_t;

/** Host test hook called at each timesync_hook_point_t. */
typedef void (*timesync_hook_fn_t)(timesync_hook_point_t point);

/**
 * @brief  Local time in microseconds: esp_timer_get_time() in the firmware
 *         build, a fake clock set by timesync_test_set_now_us() in the host
 *         test build.
 * @return Local time, microseconds.
 */
int64_t timesync_platform_now_us (void);

/**
 * @brief  Reset the filter, the writer state and the published set. Call
 *         before any task starts. Writer side.
 */
void timesync_init (void);

/**
 * @brief  Add one (t_p, t_l) pair to the minimum-delay filter. Writer side.
 * @param  t_p  Pico time from the beacon, microseconds.
 * @param  t_l  Local receive time, microseconds.
 */
void timesync_add_sample (int64_t t_p, int64_t t_l);

/**
 * @brief  One slew step at local time t_local, then publish. Writer side.
 * @param  t_local  Local time of the tick, microseconds.
 */
void timesync_tick (int64_t t_local);

/**
 * @brief  Seqlock writer (VC-1). Only the Time Sync Task calls it.
 * @param  p_snap  Set to publish; must not be NULL.
 */
void timesync_publish (timesync_snapshot_t const * p_snap);

/**
 * @brief  Limit a slew request to -100 to +100 us and store it as int8_t.
 * @param  request_us  Requested correction, microseconds.
 * @return The limited correction.
 */
int8_t timesync_slew_clamp (int64_t request_us);

/**
 * @brief  Seqlock reader (VC-1). Any task, never an ISR.
 * @param  p_reader  This task's reader; must not be NULL.
 * @return false when the read ended in SYNC_STALE; the reader then holds its
 *         last good snapshot.
 */
bool timesync_read_snapshot (timesync_reader_t * p_reader);

/**
 * @brief  Corrected Pico time now. Returns local time unchanged while nothing
 *         is published.
 * @param  p_reader  This task's reader; must not be NULL.
 * @return Corrected Pico time, microseconds.
 */
int64_t timesync_now_corrected (timesync_reader_t * p_reader);

/**
 * @brief  Local alarm time for tick n. Returns the Pico-time deadline unchanged
 *         while nothing is published (the T-B5-01 stub behaviour).
 * @param  p_reader   This task's reader; must not be NULL.
 * @param  origin_us  Timeline origin in Pico time, microseconds.
 * @param  tick       Tick number n.
 * @return Local time of the deadline of tick n, microseconds.
 */
int64_t timesync_deadline_local (timesync_reader_t * p_reader,
                                 int64_t origin_us, uint64_t tick);

/**
 * @brief  floor((t - origin) x 60 / 1,000,000) mod frame_count, integers
 *         only. Exact for every t - origin that fits int64_t.
 * @param  t_us         Corrected Pico time, microseconds.
 * @param  origin_us    Timeline origin in Pico time, microseconds.
 * @param  frame_count  Frames in the asset; 0 (no asset) returns 0.
 * @return Frame index in 0 to frame_count - 1.
 */
uint32_t timesync_frame_index (int64_t t_us, int64_t origin_us,
                               uint32_t frame_count);

/**
 * @brief  Pico-time deadline of tick n: origin + ceil(n x 1,000,000 / 60).
 * @param  origin_us  Timeline origin in Pico time, microseconds.
 * @param  tick       Tick number n.
 * @return The deadline, or INT64_MAX above the exact range.
 */
int64_t timesync_deadline_pico (int64_t origin_us, uint64_t tick);

/**
 * @brief  The first tick strictly after t: the smallest n with
 *         timesync_deadline_pico(origin, n) > t, 0 when t < origin. Lets the
 *         Display Engine start (or restart after missed ticks) on the next
 *         boundary of the shared timeline.
 * @param  origin_us  Timeline origin in Pico time, microseconds.
 * @param  t_us       Corrected Pico time, microseconds.
 * @return Tick number.
 */
uint64_t timesync_next_tick (int64_t origin_us, int64_t t_us);

/**
 * @brief  floor(dt x ppb / 10^9) without forming dt x ppb.
 * @param  dt_us      Elapsed time, microseconds.
 * @param  drift_ppb  Drift, parts per billion.
 * @return The scaled time, microseconds.
 */
int64_t timesync_scale_ppb (int64_t dt_us, int32_t drift_ppb);

/**
 * @brief  Pico time at local time t_l under a snapshot.
 * @param  p_snap  Snapshot; must not be NULL.
 * @param  t_l     Local time, microseconds.
 * @return Pico time, microseconds.
 */
int64_t timesync_pico_from_local (timesync_snapshot_t const * p_snap,
                                  int64_t                     t_l);

/**
 * @brief  Local time at Pico time t_p under a snapshot (first-order inverse).
 * @param  p_snap  Snapshot; must not be NULL.
 * @param  t_p     Pico time, microseconds.
 * @return Local time, microseconds.
 */
int64_t timesync_local_from_pico (timesync_snapshot_t const * p_snap,
                                  int64_t                     t_p);

/**
 * @brief  SYNC_READY rule (OD-10, decided by Ahsanul). True only when all
 *         three hold:
 *         1. the minimum-delay window is full (TIMESYNC_WINDOW samples);
 *         2. a drift estimate is available;
 *         3. the newest (t_p, t_l) sample is no older than
 *            TIMESYNC_HOLDOVER_US (1 s, VA-4.3) at the local time of the
 *            call, so a tile that stops receiving beacons stops reporting
 *            ready.
 *         The inputs are published by the Time Sync Task under their own
 *         sequence counter, separate from the VC-1 set; a read that fails
 *         TIMESYNC_READ_ATTEMPTS times returns false.
 *         Offline confirmation (plan choice): on the T-B3-03 10 min
 *         (t_p, t_l) log, the p99 window-to-window change of the 16-sample
 *         minimum must be at most 1 ms. If it is not, OD-10 option (b), a
 *         spread bound, is reopened.
 *         Any task, never an ISR.
 * @return true when the tile may report SYNC_READY.
 */
bool timesync_sync_ready (void);

/**
 * @brief  Age of the newest (t_p, t_l) sample at the local time of the call
 *         (T-B4-04: CLOCK HOLDOVER and the 1 s holdover pause). Read like
 *         timesync_sync_ready(). Any task, never an ISR.
 * @return Microseconds, or INT64_MAX before the first sample.
 */
int64_t timesync_newest_age_us (void);

/**
 * @brief  Current filter estimate, for the host tests.
 * @param  p_offset_us  Receives the estimated offset; must not be NULL.
 * @param  p_ref_us     Receives its reference time; must not be NULL.
 * @return false until the first sample.
 */
bool timesync_filter_estimate (int64_t * p_offset_us, int64_t * p_ref_us);

/**
 * @brief  Current drift estimate, for the host tests.
 * @param  p_drift_ppb  Receives the drift; must not be NULL.
 * @return false until a drift is available.
 */
bool timesync_drift_estimate (int32_t * p_drift_ppb);

/**
 * @brief  The int8_t slew of the last tick, for the host tests.
 * @return Slew in -100 to +100 us.
 */
int8_t timesync_last_slew (void);

#ifdef TIMESYNC_HOST_TEST
/**
 * @brief  Install the forced-interleaving hook (host test build only).
 * @param  p_hook  Hook, or NULL for none.
 */
void timesync_test_set_hook (timesync_hook_fn_t p_hook);

/**
 * @brief  Set the fake local clock (host test build only).
 * @param  now_us  Local time timesync_platform_now_us() returns, microseconds.
 */
void timesync_test_set_now_us (int64_t now_us);
#endif

/**
 * @brief  Time Sync Task (priority 16 on core 0, proposed, W6D-4a). It is the
 *         single writer of the published clock.
 * @param  p_queue  QueueHandle_t of q_timesync, created by tile_net (T-B3-03)
 *                  with item size sizeof(timesync_pair_t).
 */
void timesync_task (void * p_queue);

/**
 * @brief  Called by the Display Engine task (de_task, not the ISR) once per
 *         tick, so the Time Sync Task runs one slew step per tick. Safe before
 *         the task starts. Proposed, to be agreed with Jia Jun (T-B5-01).
 */
void timesync_notify_tick (void);

#endif /* TIMESYNC_H */

/*** end of file ***/
