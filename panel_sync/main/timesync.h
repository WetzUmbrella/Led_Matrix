// timesync (T-B4-02): Time Sync Task and the clock interface used by
// Playback Control (T-B4-04) and the Display Engine (T-B5-01).
//
// The filter, drift, slew, seqlock and frame arithmetic are in
// timesync_core.h (pure C, host-tested). This header adds the (t_p, t_l) pair
// that tile_net posts and the FreeRTOS task. It includes no FreeRTOS header,
// so tile_net and the host tests can include it.
//
// Reader functions (from timesync_core.h), each reading only through
// timesync_read_snapshot() (RAR-3). Each reading task owns one timesync_reader_t.
// Proposed interface, to be agreed with Jia Jun (T-B5-01); see
// timesync_DEFENCE.md, "Open interface items":
//   int64_t  timesync_now_corrected(timesync_reader_t *r);
//   int64_t  timesync_deadline_local(timesync_reader_t *r, int64_t origin_us, uint64_t n);
//   uint32_t timesync_frame_index(int64_t t_us, int64_t origin_us, uint32_t frame_count);
//   bool     timesync_sync_ready(void);
// Never call them from an ISR (PostTri SRM row "Corrected Pico time").
//
// Clock filter, drift, slew, seqlock and frame arithmetic are pure C11; the
// FreeRTOS side (Time Sync Task, queue intake, esp_timer) is compiled only
// when TIMESYNC_HOST_TEST is not defined, so the host tests in
// panel_sync/test/ build this module with plain gcc.
//
// Time base: every time value is a signed 64-bit count of microseconds.
//   Pico time  t_p : the Pico's clock, carried in each beacon.
//   local time t_l : esp_timer_get_time() on this tile.
//   offset         : t_l - t_p (local minus Pico), so Pico time = local - offset.
//   drift          : rate of change of the offset, in parts per billion (ppb)
//                    of elapsed local time (plan choice: 50 ppm worst case,
//                    VA-4.7, is 50,000 ppb, well inside int32_t at 1 ppb steps).
//   reference      : local time at which the published offset holds exactly.
// Range assumed for all times: 0 <= t < 2^62 us (about 146,000 years), so
// differences of two times never overflow int64_t. Each function below states
// the range in which its result is exact.
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

/* ---- constants (source in brackets) ---------------------------------- */
#define TIMESYNC_WINDOW        16    // minimum-delay filter window, last 16 beacons (1.6 s) (proposed) [T-B4-02, VA-4.1]
#define TIMESYNC_HALF          (TIMESYNC_WINDOW / 2)  // (plan choice) drift uses the two halves of the same window, so no new constant
#define TIMESYNC_SLEW_MAX_US   100   // at most 100 us per tick, int8_t -100..+100 (proposed) [T-B4-02, IFM-4b, VC-2]
#define TIMESYNC_READ_ATTEMPTS 3     // reader gives up after 3 failed attempts, then SYNC_STALE [T-B4-02, VC-1]
#define TIMESYNC_FPS           60    // 60 fps [T-B5-01, W6D-1a]
#define TIMESYNC_US_PER_S      1000000
#define TIMESYNC_PPB_PER_UNIT  1000000000   // ppb: parts per 10^9

/* ---- published snapshot (the seqlock-protected set, VC-1) ------------- */
#define TIMESYNC_SNAP_VALID  0x1u    // a published set was copied at least once
#define TIMESYNC_SYNC_STALE  0x2u    // last read failed TIMESYNC_READ_ATTEMPTS times; fields are the last good set

typedef struct {
    int64_t  offset_us;   // applied (slewed) offset at ref_us
    int32_t  drift_ppb;   // drift held with offset and reference
    int64_t  ref_us;      // local time the offset refers to
    uint32_t flags;       // TIMESYNC_SNAP_VALID, TIMESYNC_SYNC_STALE
} timesync_snapshot_t;

// One per reading task (Playback Control, Display Engine). It holds that
// reader's last good snapshot, so two readers never share mutable state.
// Never shared between tasks.
typedef struct {
    timesync_snapshot_t snap;
    uint32_t      stale_count;   // number of reads that ended in SYNC_STALE (T-X-05 reports it)
} timesync_reader_t;

/* ---- platform hook --------------------------------------------------- */
// Local time in microseconds. timesync.c implements it with esp_timer_get_time();
// the host test implements it with a fake clock.
int64_t timesync_platform_now_us(void);

/* ---- writer side (Time Sync Task only, single writer) ----------------- */
void   timesync_init(void);                         // reset everything; call before any task starts
void   timesync_add_sample(int64_t t_p, int64_t t_l);    // one (t_p, t_l) pair into the filter
void   timesync_tick(int64_t t_local);                   // one slew step at local time t_local, then timesync_publish()
void   timesync_publish(const timesync_snapshot_t *s);         // seqlock writer (VC-1)
int8_t timesync_slew_clamp(int64_t request_us);          // limit a request to -100..+100 and store as int8_t

/* ---- reader side (any task, never an ISR) ----------------------------- */
bool     timesync_read_snapshot(timesync_reader_t *r);         // seqlock reader (VC-1); false = SYNC_STALE
int64_t  timesync_now_corrected(timesync_reader_t *r);            // corrected Pico time now
int64_t  timesync_deadline_local(timesync_reader_t *r, int64_t origin_us, uint64_t n);   // local alarm time for tick n
uint32_t timesync_frame_index(int64_t t_us, int64_t origin_us, uint32_t frame_count);

/* ---- pure arithmetic (exposed for tests and for display_engine) ------- */
int64_t timesync_deadline_pico(int64_t origin_us, uint64_t n);          // origin + ceil(n x 1,000,000 / 60)
int64_t timesync_scale_ppb(int64_t dt_us, int32_t ppb);                  // floor(dt x ppb / 10^9)
int64_t timesync_pico_from_local(const timesync_snapshot_t *s, int64_t t_l);
int64_t timesync_local_from_pico(const timesync_snapshot_t *s, int64_t t_p);

/* ---- SYNC_READY (OD-10, stub) ----------------------------------------- */
bool timesync_sync_ready(void);

/* ---- inspection, used by host tests ----------------------------------- */
bool   timesync_filter_estimate(int64_t *offset_us, int64_t *ref_us);   // false until the first sample
bool   timesync_drift_estimate(int32_t *drift_ppb);                      // false until a drift is available
int8_t timesync_last_slew(void);                                         // the int8_t slew of the last tick

/* ---- forced-interleaving hook (host test build only) ------------------ */
#ifdef TIMESYNC_HOST_TEST
typedef enum {
    TIMESYNC_HOOK_WRITER_ODD = 0,      // writer: after the odd store and release fence, before any field
    TIMESYNC_HOOK_WRITER_MID,          // writer: offset written, drift and reference not yet
    TIMESYNC_HOOK_WRITER_BEFORE_EVEN,  // writer: all fields written, even store not yet
    TIMESYNC_HOOK_READER_SEQ1,         // reader: after the acquire load, before copying
    TIMESYNC_HOOK_READER_MID,          // reader: offset copied, drift and reference not yet
    TIMESYNC_HOOK_READER_BEFORE_SEQ2,  // reader: after the acquire fence, before the reload
    TIMESYNC_HOOK_COUNT
} timesync_hook_point_t;
typedef void (*timesync_hook_fn_t)(timesync_hook_point_t point);
extern timesync_hook_fn_t timesync_test_hook;
#endif

// One beacon observation, posted by tile_net (T-B3-03) to the Time Sync Queue.
// t_p: Pico time carried in the beacon, microseconds.
// t_l: local esp_timer_get_time() at the receive hook chosen in OD-09.
typedef struct {
    int64_t t_p;
    int64_t t_l;
} timesync_pair_t;

// SYNC_READY rule: open decision OD-10 (Ahsanul, before T-B4-04). Record the
// rule here once it is decided. Until then timesync_sync_ready() is a stub.

// Time Sync Task (priority 16 on core 0, (proposed) W6D-4a, created by T-B5-04).
// pvParameters is the QueueHandle_t of q_timesync, created by tile_net in
// T-B3-03 with item size sizeof(timesync_pair_t). The task is the single writer of
// the published clock.
void timesync_task(void *q_timesync);

// Proposed (to be agreed with Jia Jun, T-B5-01): called by the Display
// Engine task (de_task, not the ISR) once per tick, so the Time Sync Task
// runs one slew step per tick. Safe before the task starts.
void timesync_notify_tick(void);
