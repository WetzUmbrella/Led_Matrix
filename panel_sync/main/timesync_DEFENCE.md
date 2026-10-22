# timesync: design defence (T-B4-02)

Owner: Ahsanul (Buddy 4). Files: `timesync_core.c/.h` (pure C, host-tested), `timesync.c/.h` (Time Sync Task), `panel_sync/test/test_timesync.c` (host tests, `make -C panel_sync/test`).

Each entry gives the design choice, the PostTri row it comes from (inventory ID and the row's wording, quoted from `Plan/commitment_inventory.md` or the T-B4-02 plan row), and the question an examiner is likely to ask. Numbers not in the plan are labelled (plan choice).

## 1. Input is a (t_p, t_l) pair, defined in `timesync.h`

- Source: IFM-4a, "input (t_p, t_l) pairs from the Time Sync Queue"; T-B4-02, "Input is the (t_p, t_l) pair type defined in `timesync.h` and posted by `tile_net`, so this task needs no wire decoding."
- Choice: `ts_pair_t { int64_t t_p; int64_t t_l; }`. The Time Sync Task receives the `q_timesync` queue (created by tile_net in T-B3-03) as its task parameter, so timesync holds no global reference to tile_net state.
- Examiner: "Where exactly is t_l taken, and how much of the receive latency is already inside it before the pair reaches your filter?" (the hook is OD-09, Cheston).

## 2. Minimum-delay filter over the last 16 beacons

- Source: VA-4.1, "One-way broadcast beacons with a minimum-delay filter over the last 16 beacons (1.6 s) **(proposed)**."; T-B4-02, "the offset estimate comes from the sample with the smallest (t_l - t_p)."
- Choice: a ring of 16 pairs; the estimate is the smallest (t_l - t_p) in the ring, and its reference is that sample's t_l. On a tie the older sample wins. Host test 2 checks the estimate equals the known minimum sample's offset exactly.
- Examiner: "The minimum removes the variable part of the delay. What part of the delay does it not remove, and why does that part not matter for skew between two tiles?"

## 3. Drift from the two halves of the same window

- Source: VA-4.7, "Drift estimate kept alongside the offset (holdover sized from drift: 50 ppm worst case uses a 2 ms share of the 5 ms budget in 40 s)."; T-B4-02, "Drift: estimated from the filtered offsets across the window and held with offset and reference."
- Choice: once the window is full, the minimum-delay sample of the older 8 and of the newer 8 give two filtered offsets; drift = their difference over their t_l difference, in parts per billion (ppb). Splitting into halves is a (plan choice): it adds no new constant beyond the plan's 16. ppb units are a (plan choice): 50 ppm is 50,000 ppb, which fits `int32_t` at 1 ppb resolution. If drift cannot be computed (zero span or out of range) the previous value is held.
- Examiner: "Over a 0.8 s baseline, how large is the drift error caused by a 100 us difference in the two minimum delays, and is that acceptable for a 1 s holdover?"

## 4. Slew of at most 100 us per tick, `int8_t`, never stepped

- Source: VA-4.6, "Slew offset corrections instead of stepping the clock."; IFM-4b, "slew at most 100 us per tick (2 ms correction takes 20 frames) **(proposed)**"; VC-2, "Slew field becomes `int8_t`, range -100 to +100 us ... boundary unit test at -100, 0 and +100."
- Choice: `ts_slew_clamp()` limits each request to -100..+100 and returns `int8_t`. Every tick publishes `applied + slew`. Host test 1 checks -100, 0, +100 unchanged and -101, +101 limited.
- Examiner: "Why `int8_t` and not 7 bits, and what is the largest correction rate in ppm that a 100 us per tick slew can follow at 60 fps?"

## 5. What "absorbed in exactly 20 ticks" means

- Source: T-B4-02 host test (3), "an injected 2 ms offset step is absorbed in exactly 20 ticks".
- Definition used: with drift 0 and the published offset equal to the estimate, the estimate changes by 2,000 us. Tick k (k = 1..20) moves the published offset 100 us towards the estimate, so the residual after tick k is 2,000 - 100k us: 100 us after tick 19, 0 after tick 20, and 0 on ticks 21 to 40. No tick moves corrected time by more than 100 us against local time. Checked for +2 ms and -2 ms.
- Examiner: "Your test replaces the whole window before ticking. What happens to drift and to the 20-tick count if the step arrives one beacon at a time?"

## 6. One slew step per Display Engine tick

- Source: IFM-4b, "slew at most 100 us per tick"; T-B5-01 (display_engine re-arms a one-shot every tick); PostTri SRM row "Corrected Pico time and drift estimate", "never read in the ISR".
- Choice: `de_task` calls `timesync_notify_tick()` once per tick (task context, not the ISR); the Time Sync Task wakes, drains the queue and runs one `ts_tick()`. Several pending notifications still give one step. Without notifications it wakes every 100 ms (plan choice: one beacon period at 10 Hz) and still steps at most once. This adds one call to T-B5-01's `de_task` that the T-B5-01 row does not list; Jia Jun confirms it.
- Examiner: "If the Time Sync Task is delayed past a tick by WiFi (priority 23) on core 0, does the 100 us bound still hold, and what does the delay cost?"

## 7. Reference moves to the tick time on every publication

- Source: VC-1, "One sequence counter covers offset, drift and reference."; VA-4.7 (drift held with offset).
- Choice: each publication is rebased to the local time of that tick, so a new drift value changes the slope from that instant on and causes no jump at it. The first publication sets the offset to the first estimate directly: there is no earlier estimate to correct.
- Examiner: "Is your first publication a step? What does the tile show between boot and that first publication?"

## 8. Seqlock writer and reader, exactly as VC-1

- Source: VC-1, "Writer: increment to odd, release fence, write fields, store even with release. Reader: acquire load, copy fields, acquire fence, reload; retry if odd or changed. After 3 failed retries keep the last good snapshot and set SYNC_STALE. Fields stored as relaxed 32-bit atomic halves (lock-free on Xtensa; a 64-bit `_Atomic` would take a lock)."; VB-4c, "Sequence counter with a single writer, acquire/release ordering, and no same-core higher-priority reader retry-looping."; LDC-5, CB-4.
- Choice: `ts_publish()` and `ts_read_snapshot()` follow those steps line by line with C11 `<stdatomic.h>`. Offset and reference are two `_Atomic uint32_t` halves each; drift is one 32-bit word. The even value skips 0 on wrap because 0 means "never published"; wrap needs 2^31 publications (414 days at one per tick).
- Examiner: "Why is a plain 64-bit `_Atomic` not used, and what goes wrong on the S3 if the writer's release fence is removed?"

## 9. "3 failed retries" read as 3 failed attempts in total

- Source: VC-1, "After 3 failed retries keep the last good snapshot and set SYNC_STALE."; T-B4-02 host test (4), "after 3 forced failures SYNC_STALE is set and the last good snapshot returned."
- Choice: `TS_READ_ATTEMPTS = 3`. Reading "3 retries" as 1 attempt plus 3 retries would let a read succeed on a 4th attempt after 3 forced failures, which contradicts test (4); 3 attempts in total satisfies both rows. Test 4 also checks the boundary: 2 failures then a clean third attempt returns the newest set with SYNC_STALE clear.
- Examiner: "Why bound the retries at all, rather than loop until the read is consistent?"

## 10. Last good snapshot is per reader

- Source: RAR-3, "Clock offset: sequence counter; Jia Jun must use the same reader function if the Display Engine reads it on core 1."; T-B4-02, "`now_corrected()` and `deadline_local(n)` read only through `ts_read_snapshot()`, and `display_engine` uses the same function (RAR-3)."
- Choice: each reading task owns a `ts_reader_t` that holds its own last good snapshot and SYNC_STALE count, so Playback Control (core 0) and the Display Engine (core 1) never share mutable reader state. `now_corrected(r)` and `deadline_local(r, origin, n)` therefore take the reader as a parameter. This makes the signature differ from the bare `deadline_local(n)` in the row; T-B5-01's stub copies this signature.
- Examiner: "Two readers on different cores call `ts_read_snapshot()` at once. Which data do they share, and can either corrupt the other's result?"

## 11. Forced-interleaving hook compiled out of firmware

- Source: VC-1, "Torn-read stress test extended to a core-1 reader and all three fields."; T-B4-02 host test (4), "forced writer interleaving: every snapshot equals a complete old or new set".
- Choice: `TS_TEST_HOOK(point)` sits between writer and reader steps and expands to nothing unless `TIMESYNC_HOST_TEST` is defined. Test 4 drives it: a complete writer update at each reader step; a reader run while the writer is pre-empted at each of its steps (the PostTri Version C row 1 case: "A core-1 reader can also spin while the core-0 writer is pre-empted mid-update by the WiFi task"); exactly 3 forced failures; 200,000 pseudo-random interleavings (plan choice: enough to hit every hook combination many times; the on-target count belongs to T-X-05). Each set is derived from one counter, so a mix is detectable.
- Examiner: "A host test on x86 runs on one thread. What does it prove about the ESP32-S3, and what does it leave to T-X-05?"

## 12. `frame_index(t)` in integers, and before origin

- Source: W6D-1a, "frame = floor((t - origin) x FPS) mod frame_count with FPS = 60, t in corrected Pico time."; T-B4-02, "with t and origin in microseconds, `frame_index(t)` = floor((t - origin) x 60 / 1,000,000) mod frame_count, integers only."; critic log ruling 12.
- Choice: d = t - origin is split by floor division into d = q x 10^6 + r with 0 <= r < 10^6, so frames = 60q + floor(60r / 10^6) and d x 60 is never formed. This is exact for every d that fits `int64_t`, which holds whenever t and origin are both non-negative. Before origin the floor and a non-negative modulo continue the timeline backwards (origin - 1 us gives frame_count - 1); Playback Control stays ARMED until t_start, so these frames are never shown. frame_count 0 returns 0. origin and frame_count are parameters, because the timeline belongs to Playback Control (T-B4-04).
- Examiner: "C division truncates towards zero. Show what `(t - origin) * 60 / 1000000 % frame_count` returns at t = origin - 1 us, and why yours differs."

## 13. Tick deadline computed from n every time, rounded up

- Source: VA-4.5, "Tick n at origin + n x 1,000,000 / 60 us, computed in integers every frame, re-armed as a one-shot each frame; never a free-running 60 Hz timer (rounding to 16,667 us drifts 20 us/s, about 12 ms in 10 min)."; T-B5-01, "computed in 64-bit integers each frame (never by adding 16,667 us)".
- Choice: `ts_deadline_pico(origin, n)` = origin + ceil(n x 1,000,000 / 60), from n each call, with n split as 60q + r so every 60 ticks are exactly 1,000,000 us. Rounding up is a (plan choice) so that `frame_index(ts_deadline_pico(o, n), o, fc) == n mod fc` for every n: a truncated deadline (origin + 16,666 us for n = 1) falls 0.67 us before frame 1 starts and `frame_index` returns 0. The T-B4-04 host test ("frame_index at t = origin + k x 1,000,000 / 60 equals k mod 600") passes only with the rounded-up value; this is flagged to the orchestrator.
- Examiner: "Why is a 16,667 us period wrong but a 16,666 or 16,667 us interval between consecutive deadlines right?"

## 14. Integer ranges (documented in `timesync_core.c`)

- Source: CB-4, "time stays 64-bit with justification"; PostTri Zoom 28:55 ("32-bit wraps after 71.6 minutes").
- All times are signed 64-bit microseconds, assumed in [0, 2^62) (about 146,000 years).
- `frame_index`: exact for every t - origin that fits `int64_t`.
- `ts_deadline_pico`: exact for 0 <= n <= 60 x floor((INT64_MAX - 10^6 - max(origin, 0)) / 10^6) + 59, which is n <= 553,402,322,211,239 (about 292,000 years at 60 fps) for origin 0 and n <= 276,701,161,105,619 (about 146,000 years) for origin just below 2^62; above that it saturates at INT64_MAX.
- `ts_scale_ppb` (drift x elapsed time): exact for |dt| < 2^31 x 10^9 us (about 68,000 years) at any `int32_t` drift.
- `ts_local_from_pico` is a first-order inverse: within 2 us of the exact inverse for |drift| <= 50 ppm and |t - ref| <= 400 s; in use t - ref is about one tick.
- The supplementary host checks compare `frame_index` and `ts_scale_ppb` against 128-bit references on 1,000,000 random inputs.
- Examiner: "Where would `(t - origin) * 60` overflow `int64_t`, and how did you avoid it without 128-bit arithmetic on the S3?"

## 15. SYNC_READY is a stub

- Source: OD-10, "SYNC_READY rule (W6: "stable clock estimate"; PostTri gives no number)", options "(a) filter window full (16 beacons) and drift estimate available; (b) (a) plus a spread bound measured in T-B3-03", owner Ahsanul, before T-B4-04; T-B4-02, "SYNC_READY rule per OD-10."
- Choice: `timesync_sync_ready()` returns false and cites OD-10. False because nothing in Week 10 gates on SYNC_READY (the T-B4-03 rig uses origin = 0) and false stops an early caller from treating an undecided rule as a stable clock. The rule is recorded in `timesync.h` once decided.
- Examiner: "Which of options (a) and (b) would you choose, and what number from the T-B3-03 spread log would decide it?"

## 16. `lock` and `get_render_state` stay for now

- Source: VC-1 current state, "`panel_sync.c:76` `portMUX_TYPE lock`; writer `panel_sync.c:201-210`; reader `panel_sync.c:222-231`"; T-B4-02, "removes `lock`, `get_render_state` (`panel_sync.c:76,201-210,218-232`)".
- Choice: not removed in this commit. `get_render_state` is passed to `render_start()` at `panel_sync.c:241` and called through `get_state` at `render.c:120`; `lock` guards the beacon writer at `panel_sync.c:201-210`. Removing them breaks the build until T-B5-01 replaces the render path and T-B3-03 replaces `sync_rx_task`; they are deleted with those callers.
- Examiner: "Until they are removed, which clock does the 30 fps prototype use, and does any of it touch the new seqlock?"

## Questions for Ahsanul before Tribunal 2

Answer each in your own words, without reading the code.

1. In the seqlock reader, why must the acquire fence sit between copying the fields and reloading the sequence counter, and what wrong value could the Display Engine on core 1 use for a tick deadline if that fence were missing?
2. The Pico restarts mid-show and the minimum-delay estimate jumps by several seconds. At 100 us per tick, how long would the slew take to absorb it, what would the tiles show meanwhile, and which state in statechart 4 should take over instead?
3. A broadcast beacon reaches all tiles in one transmission, yet each tile's (t_l - t_p) minimum still contains a per-tile constant. Where does that constant come from, why can the filter not remove it, and how does the two-tile skew rig in T-B4-03 expose it?
