// timesync core (T-B4-02): clock filter, drift, slew, seqlock and frame arithmetic.
//
// Pure C11: no FreeRTOS and no ESP-IDF headers, so the host tests in
// panel_sync/test/ build it with plain gcc. The FreeRTOS side (Time Sync Task,
// queue intake, esp_timer) lives in timesync.c.
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
#define TS_WINDOW        16    // minimum-delay filter window, last 16 beacons (1.6 s) (proposed) [T-B4-02, VA-4.1]
#define TS_HALF          (TS_WINDOW / 2)  // (plan choice) drift uses the two halves of the same window, so no new constant
#define TS_SLEW_MAX_US   100   // at most 100 us per tick, int8_t -100..+100 (proposed) [T-B4-02, IFM-4b, VC-2]
#define TS_READ_ATTEMPTS 3     // reader gives up after 3 failed attempts, then SYNC_STALE [T-B4-02, VC-1]
#define TS_FPS           60    // 60 fps [T-B5-01, W6D-1a]
#define TS_US_PER_S      1000000
#define TS_PPB_PER_UNIT  1000000000   // ppb: parts per 10^9

/* ---- published snapshot (the seqlock-protected set, VC-1) ------------- */
#define TS_SNAP_VALID  0x1u    // a published set was copied at least once
#define TS_SYNC_STALE  0x2u    // last read failed TS_READ_ATTEMPTS times; fields are the last good set

typedef struct {
    int64_t  offset_us;   // applied (slewed) offset at ref_us
    int32_t  drift_ppb;   // drift held with offset and reference
    int64_t  ref_us;      // local time the offset refers to
    uint32_t flags;       // TS_SNAP_VALID, TS_SYNC_STALE
} ts_snapshot_t;

// One per reading task (Playback Control, Display Engine). It holds that
// reader's last good snapshot, so two readers never share mutable state.
// Never shared between tasks.
typedef struct {
    ts_snapshot_t snap;
    uint32_t      stale_count;   // number of reads that ended in SYNC_STALE (T-X-05 reports it)
} ts_reader_t;

/* ---- platform hook --------------------------------------------------- */
// Local time in microseconds. timesync.c implements it with esp_timer_get_time();
// the host test implements it with a fake clock.
int64_t ts_platform_now_us(void);

/* ---- writer side (Time Sync Task only, single writer) ----------------- */
void   ts_core_init(void);                         // reset everything; call before any task starts
void   ts_add_sample(int64_t t_p, int64_t t_l);    // one (t_p, t_l) pair into the filter
void   ts_tick(int64_t t_local);                   // one slew step at local time t_local, then ts_publish()
void   ts_publish(const ts_snapshot_t *s);         // seqlock writer (VC-1)
int8_t ts_slew_clamp(int64_t request_us);          // limit a request to -100..+100 and store as int8_t

/* ---- reader side (any task, never an ISR) ----------------------------- */
bool     ts_read_snapshot(ts_reader_t *r);         // seqlock reader (VC-1); false = SYNC_STALE
int64_t  now_corrected(ts_reader_t *r);            // corrected Pico time now
int64_t  deadline_local(ts_reader_t *r, int64_t origin_us, uint64_t n);   // local alarm time for tick n
uint32_t frame_index(int64_t t_us, int64_t origin_us, uint32_t frame_count);

/* ---- pure arithmetic (exposed for tests and for display_engine) ------- */
int64_t ts_deadline_pico(int64_t origin_us, uint64_t n);          // origin + ceil(n x 1,000,000 / 60)
int64_t ts_scale_ppb(int64_t dt_us, int32_t ppb);                  // floor(dt x ppb / 10^9)
int64_t ts_pico_from_local(const ts_snapshot_t *s, int64_t t_l);
int64_t ts_local_from_pico(const ts_snapshot_t *s, int64_t t_p);

/* ---- SYNC_READY (OD-10, stub) ----------------------------------------- */
bool timesync_sync_ready(void);

/* ---- inspection, used by host tests ----------------------------------- */
bool   ts_filter_estimate(int64_t *offset_us, int64_t *ref_us);   // false until the first sample
bool   ts_drift_estimate(int32_t *drift_ppb);                      // false until a drift is available
int8_t ts_last_slew(void);                                         // the int8_t slew of the last tick

/* ---- forced-interleaving hook (host test build only) ------------------ */
#ifdef TIMESYNC_HOST_TEST
typedef enum {
    TS_HOOK_WRITER_ODD = 0,      // writer: after the odd store and release fence, before any field
    TS_HOOK_WRITER_MID,          // writer: offset written, drift and reference not yet
    TS_HOOK_WRITER_BEFORE_EVEN,  // writer: all fields written, even store not yet
    TS_HOOK_READER_SEQ1,         // reader: after the acquire load, before copying
    TS_HOOK_READER_MID,          // reader: offset copied, drift and reference not yet
    TS_HOOK_READER_BEFORE_SEQ2,  // reader: after the acquire fence, before the reload
    TS_HOOK_COUNT
} ts_hook_point_t;
typedef void (*ts_test_hook_fn)(ts_hook_point_t point);
extern ts_test_hook_fn ts_test_hook;
#endif
