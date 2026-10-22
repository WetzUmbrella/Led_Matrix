// timesync core (T-B4-02). See timesync_core.h for the time base and units,
// and timesync_DEFENCE.md for where each rule comes from.
#include "timesync_core.h"

#include <stddef.h>
#include <string.h>

/* ---- forced-interleaving hook: host test build only -------------------- */
#ifdef TIMESYNC_HOST_TEST
ts_test_hook_fn ts_test_hook = NULL;
#define TS_TEST_HOOK(p) do { if (ts_test_hook) ts_test_hook(p); } while (0)
#else
#define TS_TEST_HOOK(p) ((void)0)   // compiles out of the firmware build
#endif

/* ---- integer helpers --------------------------------------------------- */
// floor(a / b) for b > 0. C division truncates towards zero, so correct it
// for negative a.
static int64_t floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b) < 0) q--;
    return q;
}

// floor(dt x ppb / 10^9) without forming dt x ppb, which overflows int64_t
// once |dt| exceeds about 4.3 x 10^9 us at 2^31 ppb. Split dt = q x 10^9 + r
// with 0 <= r < 10^9: then r x ppb < 2^61 and q x ppb is exact while
// |q| <= 2^31. Exact for |dt| < 2^31 x 10^9 us (about 68,000 years).
int64_t ts_scale_ppb(int64_t dt_us, int32_t ppb)
{
    int64_t q = floor_div(dt_us, TS_PPB_PER_UNIT);
    int64_t r = dt_us - q * TS_PPB_PER_UNIT;            // 0 <= r < 10^9
    return q * (int64_t)ppb + floor_div(r * (int64_t)ppb, TS_PPB_PER_UNIT);
}

/* ---- seqlock (VC-1) ---------------------------------------------------- */
// One sequence counter over offset, drift and reference. Each field is stored
// as relaxed 32-bit atomic words, which are lock-free on Xtensa; a 64-bit
// _Atomic would take a lock (VC-1).
enum { W_OFF_LO, W_OFF_HI, W_DRIFT, W_REF_LO, W_REF_HI, W_COUNT };

static struct {
    _Atomic uint32_t seq;          // even = stable, odd = write in progress, 0 = never published
    _Atomic uint32_t w[W_COUNT];
} s_pub;

static uint32_t lo32(int64_t v) { return (uint32_t)((uint64_t)v & 0xFFFFFFFFu); }
static uint32_t hi32(int64_t v) { return (uint32_t)((uint64_t)v >> 32); }
static int64_t  join64(uint32_t lo, uint32_t hi) { return (int64_t)(((uint64_t)hi << 32) | lo); }

// Writer: increment to odd, release fence, write the fields as relaxed 32-bit
// atomic halves, store even with release. Single writer: the Time Sync Task.
void ts_publish(const ts_snapshot_t *s)
{
    uint32_t seq = atomic_load_explicit(&s_pub.seq, memory_order_relaxed);   // only this task writes it
    atomic_store_explicit(&s_pub.seq, seq + 1u, memory_order_relaxed);      // odd
    atomic_thread_fence(memory_order_release);
    TS_TEST_HOOK(TS_HOOK_WRITER_ODD);

    atomic_store_explicit(&s_pub.w[W_OFF_LO], lo32(s->offset_us), memory_order_relaxed);
    atomic_store_explicit(&s_pub.w[W_OFF_HI], hi32(s->offset_us), memory_order_relaxed);
    TS_TEST_HOOK(TS_HOOK_WRITER_MID);
    atomic_store_explicit(&s_pub.w[W_DRIFT], (uint32_t)s->drift_ppb, memory_order_relaxed);
    atomic_store_explicit(&s_pub.w[W_REF_LO], lo32(s->ref_us), memory_order_relaxed);
    atomic_store_explicit(&s_pub.w[W_REF_HI], hi32(s->ref_us), memory_order_relaxed);
    TS_TEST_HOOK(TS_HOOK_WRITER_BEFORE_EVEN);

    // Even value. It skips 0 on wrap, because 0 means "never published".
    // Wrap needs 2^31 publications: 414 days at one per 60 fps tick.
    uint32_t even = seq + 2u;
    if (even == 0u) even = 2u;
    atomic_store_explicit(&s_pub.seq, even, memory_order_release);
}

// Reader: acquire load, copy fields, acquire fence, reload; retry if odd or
// changed. After TS_READ_ATTEMPTS failed attempts keep the reader's last good
// snapshot and set SYNC_STALE. The loop is bounded, so a reader never spins
// for long on a pre-empted writer (VB-4c, PostTri Version C row 1).
bool ts_read_snapshot(ts_reader_t *r)
{
    for (int attempt = 0; attempt < TS_READ_ATTEMPTS; attempt++) {
        uint32_t s1 = atomic_load_explicit(&s_pub.seq, memory_order_acquire);
        TS_TEST_HOOK(TS_HOOK_READER_SEQ1);
        uint32_t w0 = atomic_load_explicit(&s_pub.w[W_OFF_LO], memory_order_relaxed);
        uint32_t w1 = atomic_load_explicit(&s_pub.w[W_OFF_HI], memory_order_relaxed);
        TS_TEST_HOOK(TS_HOOK_READER_MID);
        uint32_t w2 = atomic_load_explicit(&s_pub.w[W_DRIFT], memory_order_relaxed);
        uint32_t w3 = atomic_load_explicit(&s_pub.w[W_REF_LO], memory_order_relaxed);
        uint32_t w4 = atomic_load_explicit(&s_pub.w[W_REF_HI], memory_order_relaxed);
        atomic_thread_fence(memory_order_acquire);
        TS_TEST_HOOK(TS_HOOK_READER_BEFORE_SEQ2);
        uint32_t s2 = atomic_load_explicit(&s_pub.seq, memory_order_relaxed);

        if ((s1 & 1u) == 0u && s1 == s2) {
            if (s1 == 0u) {                 // nothing published yet: not valid, not stale
                r->snap.flags = 0u;
                return true;
            }
            r->snap.offset_us = join64(w0, w1);
            r->snap.drift_ppb = (int32_t)w2;
            r->snap.ref_us    = join64(w3, w4);
            r->snap.flags     = TS_SNAP_VALID;
            return true;
        }
    }
    r->snap.flags |= TS_SYNC_STALE;         // fields keep the last good set
    r->stale_count++;
    return false;
}

/* ---- conversions -------------------------------------------------------- */
// Offset at local time t_l = offset + drift x (t_l - ref).
int64_t ts_pico_from_local(const ts_snapshot_t *s, int64_t t_l)
{
    return t_l - (s->offset_us + ts_scale_ppb(t_l - s->ref_us, s->drift_ppb));
}

// First-order inverse of ts_pico_from_local. The error against the exact
// inverse is drift^2 x (t - ref) plus 1 us from each floor: under 2 us for
// |drift| <= 50 ppm (VA-4.7 worst case) and |t - ref| <= 400 s. In use
// t - ref is about one tick, because the reference moves on every tick.
int64_t ts_local_from_pico(const ts_snapshot_t *s, int64_t t_p)
{
    int64_t approx = t_p + s->offset_us;
    return approx + ts_scale_ppb(approx - s->ref_us, s->drift_ppb);
}

/* ---- frame arithmetic --------------------------------------------------- */
// Pico-time deadline of tick n: origin + n x 1,000,000 / 60 us, computed from
// n every time (never by adding 16,667 us). Rounded up (plan choice) so that
// frame_index(ts_deadline_pico(o, n), o, fc) == n mod fc for every n: the
// deadline of tick n always lies inside frame n.
// Split n = 60q + r (0 <= r < 60): n x 10^6 / 60 = q x 10^6 + r x 10^6 / 60,
// and r x 10^6 < 6 x 10^7, so nothing overflows before the final add.
// Exact for 0 <= n <= 60 x floor((INT64_MAX - 10^6 - max(origin, 0)) / 10^6) + 59;
// above that the result saturates at INT64_MAX.
int64_t ts_deadline_pico(int64_t origin_us, uint64_t n)
{
    uint64_t q = n / TS_FPS;
    uint64_t r = n % TS_FPS;
    int64_t headroom = INT64_MAX - TS_US_PER_S - (origin_us > 0 ? origin_us : 0);
    if (q > (uint64_t)(headroom / TS_US_PER_S)) return INT64_MAX;
    int64_t whole = (int64_t)q * TS_US_PER_S;
    int64_t part  = (int64_t)((r * TS_US_PER_S + TS_FPS - 1u) / TS_FPS);   // ceil
    return origin_us + whole + part;
}

// floor((t - origin) x 60 / 1,000,000) mod frame_count, integers only.
// Split d = t - origin = q x 10^6 + r (0 <= r < 10^6, floor division) so the
// product never forms: frames = 60q + floor(60r / 10^6). Exact for every d
// that fits int64_t, which holds whenever t and origin are both >= 0.
// Before origin (d < 0) the floor and a non-negative modulo continue the
// timeline backwards: t = origin - 1 us gives frame_count - 1. Playback
// Control (T-B4-04) stays ARMED until t_start, so it never shows these.
// frame_count = 0 (no asset) returns 0.
uint32_t frame_index(int64_t t_us, int64_t origin_us, uint32_t frame_count)
{
    if (frame_count == 0u) return 0u;
    int64_t d = t_us - origin_us;
    int64_t q = floor_div(d, TS_US_PER_S);
    int64_t r = d - q * TS_US_PER_S;
    int64_t frames = q * TS_FPS + (r * TS_FPS) / TS_US_PER_S;
    int64_t m = frames % (int64_t)frame_count;
    if (m < 0) m += (int64_t)frame_count;
    return (uint32_t)m;
}

/* ---- writer state (Time Sync Task only) --------------------------------- */
typedef struct {
    int64_t  t_p[TS_WINDOW], t_l[TS_WINDOW];   // ring of the last 16 pairs
    unsigned head;                             // next slot to write
    unsigned count;                            // 0..TS_WINDOW

    bool     est_valid;                        // filter has an estimate
    int64_t  est_offset;                       // smallest (t_l - t_p) in the window
    int64_t  est_ref;                          // t_l of that sample
    bool     drift_valid;
    int32_t  drift_ppb;                        // held until a new estimate replaces it

    bool          pub_valid;                   // something was published
    ts_snapshot_t pub;                         // writer's copy of what it published
    int8_t        last_slew;                   // slew of the last tick, -100..+100
} ts_writer_t;

static ts_writer_t s_w;

void ts_core_init(void)
{
    memset(&s_w, 0, sizeof(s_w));
    atomic_store_explicit(&s_pub.seq, 0u, memory_order_relaxed);
    for (int i = 0; i < W_COUNT; i++)
        atomic_store_explicit(&s_pub.w[i], 0u, memory_order_relaxed);
}

// Minimum-delay sample among the window entries i0 .. i0+len-1 (oldest = 0).
static void min_delay(unsigned i0, unsigned len, int64_t *off, int64_t *ref)
{
    unsigned oldest = (s_w.count == TS_WINDOW) ? s_w.head : 0u;
    for (unsigned k = 0; k < len; k++) {
        unsigned i = (oldest + i0 + k) % TS_WINDOW;
        int64_t o = s_w.t_l[i] - s_w.t_p[i];
        if (k == 0 || o < *off) { *off = o; *ref = s_w.t_l[i]; }   // first smallest wins on a tie
    }
}

void ts_add_sample(int64_t t_p, int64_t t_l)
{
    s_w.t_p[s_w.head] = t_p;
    s_w.t_l[s_w.head] = t_l;
    s_w.head = (s_w.head + 1u) % TS_WINDOW;
    if (s_w.count < TS_WINDOW) s_w.count++;

    // Filter: the offset comes from the sample with the smallest (t_l - t_p).
    min_delay(0, s_w.count, &s_w.est_offset, &s_w.est_ref);
    s_w.est_valid = true;

    // Drift: slope between the filtered offsets of the older and newer half of
    // the full window. Held (not cleared) when it cannot be computed.
    if (s_w.count == TS_WINDOW) {
        int64_t off_a, ref_a, off_b, ref_b;
        min_delay(0, TS_HALF, &off_a, &ref_a);
        min_delay(TS_HALF, TS_HALF, &off_b, &ref_b);
        int64_t diff = off_b - off_a;
        int64_t span = ref_b - ref_a;
        if (span > 0 && diff <= INT64_MAX / TS_PPB_PER_UNIT && diff >= -(INT64_MAX / TS_PPB_PER_UNIT)) {
            int64_t ppb = floor_div(diff * TS_PPB_PER_UNIT, span);
            if (ppb >= INT32_MIN && ppb <= INT32_MAX) {
                s_w.drift_ppb = (int32_t)ppb;
                s_w.drift_valid = true;
            }
        }
    }
}

int8_t ts_slew_clamp(int64_t request_us)
{
    if (request_us > TS_SLEW_MAX_US)  request_us = TS_SLEW_MAX_US;
    if (request_us < -TS_SLEW_MAX_US) request_us = -TS_SLEW_MAX_US;
    return (int8_t)request_us;
}

// One tick. The published offset moves towards the filter's estimate by at
// most 100 us and is never stepped. Each publication is rebased to t_local,
// so a new drift value changes the slope from t_local on but causes no jump
// at t_local itself. The very first publication sets the offset directly: it
// is the first estimate, not a correction of an earlier one.
void ts_tick(int64_t t_local)
{
    if (!s_w.est_valid) return;
    int32_t drift  = s_w.drift_valid ? s_w.drift_ppb : 0;
    int64_t target = s_w.est_offset + ts_scale_ppb(t_local - s_w.est_ref, drift);

    ts_snapshot_t next = { .drift_ppb = drift, .ref_us = t_local, .flags = TS_SNAP_VALID };
    if (!s_w.pub_valid) {
        next.offset_us = target;
        s_w.last_slew = 0;
    } else {
        int64_t applied = s_w.pub.offset_us + ts_scale_ppb(t_local - s_w.pub.ref_us, s_w.pub.drift_ppb);
        int8_t slew = ts_slew_clamp(target - applied);
        next.offset_us = applied + slew;
        s_w.last_slew = slew;
    }
    ts_publish(&next);
    s_w.pub = next;
    s_w.pub_valid = true;
}

/* ---- reader helpers ------------------------------------------------------ */
// Corrected Pico time now, read only through ts_read_snapshot() (RAR-3).
// With nothing published yet it returns local time unchanged.
int64_t now_corrected(ts_reader_t *r)
{
    int64_t t_l = ts_platform_now_us();
    (void)ts_read_snapshot(r);
    if (!(r->snap.flags & TS_SNAP_VALID)) return t_l;
    return ts_pico_from_local(&r->snap, t_l);
}

// Local alarm time for tick n, read only through ts_read_snapshot() (RAR-3).
// With nothing published yet it returns the Pico-time deadline unchanged,
// which is what the T-B5-01 stub returns.
int64_t deadline_local(ts_reader_t *r, int64_t origin_us, uint64_t n)
{
    int64_t t_p = ts_deadline_pico(origin_us, n);
    (void)ts_read_snapshot(r);
    if (!(r->snap.flags & TS_SNAP_VALID)) return t_p;
    return ts_local_from_pico(&r->snap, t_p);
}

/* ---- SYNC_READY ------------------------------------------------------------ */
// STUB. The SYNC_READY rule is open decision OD-10 (owner Ahsanul, due before
// T-B4-04 in Week 13; options: (a) window full and drift available, (b) (a)
// plus a spread bound from T-B3-03). No rule is invented here.
// Returns false: nothing in Week 10 gates on SYNC_READY (the T-B4-03 rig uses
// origin = 0), and false keeps any early caller from treating an undecided
// rule as a stable clock.
bool timesync_sync_ready(void)
{
    return false;
}

/* ---- inspection ------------------------------------------------------------- */
bool ts_filter_estimate(int64_t *offset_us, int64_t *ref_us)
{
    if (!s_w.est_valid) return false;
    *offset_us = s_w.est_offset;
    *ref_us = s_w.est_ref;
    return true;
}

bool ts_drift_estimate(int32_t *drift_ppb)
{
    if (!s_w.drift_valid) return false;
    *drift_ppb = s_w.drift_ppb;
    return true;
}

int8_t ts_last_slew(void)
{
    return s_w.last_slew;
}
