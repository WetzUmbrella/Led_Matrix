# Tribunal 1: what was raised and how we handled it

Tribunal 1 (Week 8, 19 Oct 2026) defended the Week 6 architecture and
introduced a **change requirement**: every data field must use only the bits
its valid range needs. Three things came out of it, and this page shows, per
subsystem, what we did about each and where the proof is:

1. **The lecturer's written feedback** on the Week 6 design (11 points).
2. **The change requirement** (minimum-bit data representation), worked
   through live in the tribunal (Version B of the Tribunal 1 document).
3. **The AI-vetted gap check** after the tribunal (Version C): gaps we
   accepted and fixed, and suggestions we rejected.

The detailed evidence for the change requirement (data representation table,
message sizes and RAM before and after, correctness and performance) is in
[`change_requirement/`](change_requirement/).

## 1. The lecturer's feedback, point by point

| # | Feedback | What we did | Proof |
|---|---|---|---|
| 1 | Cap the brightness and check power: a full-white 8x8 matrix can draw several amps | Every channel is capped at 8/255 (about 3%, about 0.12 A per tile at full white), scaled once on the PC; values above 8 are refused | NFR5; `pc/tests/test_asset_builder.py`, `test_check_assets.py`; tile test D1. The 30 min heat test was not run (see section 4) |
| 2 | NFR2 cannot be met with free-running 60 Hz timers; schedule each tick at origin + n/60 s in corrected Pico time and target at most 5 ms | Each tick is a one-shot timer re-armed from its own deadline in corrected Pico time; target 5 ms skew | FR7, NFR1, NFR2; tile tests P3, D3; wall test [ST-01](../testing/evidence/ST-01/) (under 5 ms after the drift fix) |
| 3 | Transmit a prepared buffer on the tick, then prepare the next; one back-buffer owner | The Display Engine sends the prepared buffer on the tick and then unpacks the next frame; it alone owns the buffers | [`../design.md`](../design.md) section 5 |
| 4 | Specify clock sync: beacon period, filter, drift, tolerances; timestamp on receipt; WiFi power save off | Beacon every 100 ms; minimum-delay filter over 16 beacons; drift limited to 50 ppm; slew at most 100 us per tick; stamped in the receive callback; power management off | FR8; tile tests 1 to 4 and the 24 Nov cases; [ST-07](../testing/evidence/ST-07/) |
| 5 | The Pico coordinator has no loop design | Poll mode; one bounded routine per pass within 1 ms; absolute 100 ms beacon and 250 ms STATUS schedules; lateness histogram | NFR9; Pico tests C1, C2, C5, C10 |
| 6 | Numeric priorities, cores and queue lengths; Telemetry and Fault queues missing | Numeric priorities and cores for every task (Display 20 on core 1); every queue has a length and an overflow policy; faults travel in the fault word, so there is no Fault Queue | [`../design.md`](../design.md) section 8 |
| 7 | Flash access stalls non-IRAM code: load the whole asset into RAM at ASSET_READY | The tile copies the whole asset into internal RAM before ASSET_READY; the display never reads flash; the tick and RMT interrupts are IRAM-safe | NFR14; tile tests A1 to A6; RAM figures in [`change_requirement/before_after.md`](change_requirement/before_after.md) |
| 8 | Untestable wording (NFR5 "without visible flickering", NFR8, A9, "repeated" failures); FR18 contradicts the pause policy | Requirements rewritten with numbers and pass rules; the failure policy is selectable (continue or pause) | [`../requirements.md`](../requirements.md); Pico tests C13, C14 |
| 9 | On an upright wall the accelerometer alone gives rotation; state the mounting assumption | Orientation from gravity only, on a wall within 20 degrees of vertical | FR17; tile tests O1 to O3; [ST-08](../testing/evidence/ST-08/) |
| 10 | Readiness has two authorities (PC and Pico); define one | Only the Pico decides, from ASSET_READY with the session and CRC32, plus SYNC_READY | FR6; Pico tests C6 to C8 |
| 11 | No CONNECTING timeout, no STOP from PAUSED, no values for recovery, holdover and START_AT lead; freeze the protocol (types, sequence numbers, ACK and retry, stale rejection) | CONNECTING 10 s; STOP accepted from PAUSED; recovery 5 s, holdover 200 ms, pause at 1 s, START_AT 600 ms ahead x 3; one layout table with sequence numbers, ACK and resend, and stale messages rejected | Tile test P4; Pico tests C6, C11, C12, C16; [`../../protocol/layout_table.md`](../../protocol/layout_table.md) |

The feedback also asked for a two-tile skew rig early. We built a GPIO pulse
rig with a logic-analyser logger (`tools/pico_tap/`, `pc/tools/skew_analyse.py`);
the final skew measurement used a 240 fps camera instead
([ST-01](../testing/evidence/ST-01/)).

## 2. Per subsystem

### B1: PC website, GIF to assets, dashboard (`pc/`)

| Raised | What we did | Proof |
|---|---|---|
| Change requirement: the brightness cap means a channel only holds 0 to 8 | Assets packed at 4 bits per channel: 115,200 B to 57,600 B per tile, 113 to 57 chunks. Base-9 (needs a divide per pixel) and delta encoding (breaks direct access to any frame) rejected; palette deferred | [`change_requirement/before_after.md`](change_requirement/before_after.md); `test_asset_builder.py::test_600_frame_asset_is_57600_bytes_with_zlib_crc` |
| The rounding rule and the format must be one specification | round(v x 8 / 255), nibble order, format version and CRC32 over the packed bytes fixed in the layout table | `test_round_trip.py`, `test_cross_lang.py` |
| Gap check: the 10-bit frame index caps assets at 1,023 frames, but the PC accepted any GIF length | GIFs needing more than 1,023 frames are refused with a message | `test_asset_builder.py::test_18s_gif_refused_with_limit_message` |
| Gap check: brightness applied on both the PC and the tile would round every value to 0 | Scaling happens once, on the PC; the dashboard refuses brightness above 8; the tile does no scaling | `test_asset_builder.py::test_brightness_above_8_refused`, `test_round_trip.py::test_double_scaling_is_caught` |
| Every hop is binary, as MQTT would be | The PC encodes every message with the same layout table as the C code | `test_cross_lang.py`, `test_layout_table.py` |

### B2: Pico W coordinator and protocol (`ledwall_sync/`, `common/proto/`)

| Raised | What we did | Proof |
|---|---|---|
| Feedback 5: no loop design | Poll mode, one routine per pass within 1 ms, absolute schedules | Pico tests C1, C2, C5 |
| Change requirement: one bit-exact layout shared by Python and C | Explicit shifts and masks, little-endian, one table, cross-language vectors; C bit-fields kept off the wire (their layout is compiler-defined) | `common/proto/test` (8 cases), `test_cross_lang.py` |
| The readiness bitmap can lose an update if a network callback interrupts the loop | Poll mode: callbacks run only inside the loop's own poll call, so the bitmap has one writer in one context | [`../design.md`](../design.md) section 9 |
| Feedback 10 and 11: one readiness authority; freeze the protocol | The Pico alone decides readiness; sequence numbers, ACKs and stale rejection on every important message | Pico tests C6, C7, C8, C16 |
| Gap check: the re-upload risk named the wrong path | Chunks go PC to tile, never through the Pico; the PC rate-limits uploads | `test_uploader.py::test_rate_limiter_spaces_sends` |

### B3: tile communication and storage (`panel_sync/main/tile_net.c`, `asset_store.c`, `asset_rx.c`, `fault_word.c`)

| Raised | What we did | Proof |
|---|---|---|
| Feedback 7: load the whole asset into RAM | The asset is copied into internal RAM before ASSET_READY, with a 32 KB margin (90,368 B); the copy shrank from 147,968 B with the 4-bit format | NFR14; tile tests A1 to A6; [`change_requirement/before_after.md`](change_requirement/before_after.md) |
| Change requirement: the 32-bit fault word is written from tasks on both cores | One atomic word: fetch-or to set, fetch-and to clear; one-off event bits and ongoing condition bits are treated differently (a condition still active must not flicker off the dashboard) | FR19; tile tests F1 to F6 |
| Gap check: UDP `sendto()` does not confirm delivery, so clearing faults "after a successful send" can lose them | TELEMETRY carries a sequence number; the PC ACKs it; event bits are cleared only on that ACK, and unACKed bits ride in the next report | tile tests T1 to T5 (T5 drops every third report: 100% still delivered); `test_telemetry_ack.py` |
| Gap check: the format-version field had no rule for an unknown value | An unknown version or a payload length other than frame count x 96 B gets a NACK and the ASSET_FORMAT fault, and never ASSET_READY | FR20; tile tests A1, A3; `test_upload_faults.py::test_nack_reasons`; `test_cross_lang.py::test_c_decoder_refuses_the_115200_payload_by_length` |
| Feedback 6: queues need lengths and overflow policies | Time Sync 4 (drop oldest), Command 3 (refuse and NACK), Upload 4 (refuse and NACK), Reply 8 (drop oldest), Telemetry 2 (drop oldest); a full queue raises QUEUE_OVERFLOW | [`../design.md`](../design.md) section 8 |

### B4: time sync and playback (`panel_sync/main/timesync.c`, `playback_control.c`)

| Raised | What we did | Proof |
|---|---|---|
| Feedback 2 and 4: tick scheduling and clock sync | Ticks from corrected Pico time; minimum-delay filter, drift, slew, holdover with numbers | FR7, FR8; tile tests 1 to 4, P1 to P6; [ST-01](../testing/evidence/ST-01/), [ST-07](../testing/evidence/ST-07/) |
| Change requirement: the 64-bit clock offset can be read torn between tasks | One sequence counter (seqlock) covers offset, drift and reference together; a reader retries a torn copy | NFR13; tile test 4 (200,000 forced interleavings, 0 torn reads) |
| Pressure to shrink time fields | Timestamps kept at 64 bits: 32 bits wraps after 71.6 minutes to save 40 B/s, and a running clock has no small valid range (no change) | [`change_requirement/data_representation.md`](change_requirement/data_representation.md) |
| Gap check: the clock correction was sized 7 bits, but a slew of -100 to +100 us needs a sign | The correction is an `int8_t`, -100 to +100 us | tile test 1 (limits at -100 and +100) |
| Gap check: the seqlock must cover a reader on the other core too | One reader function for every task on both cores; after 3 failed retries the last good snapshot is kept and SYNC_STALE raised | tile test 4 |

### B5: display and orientation (`panel_sync/main/display_engine.c`, `orientation.c`)

| Raised | What we did | Proof |
|---|---|---|
| Feedback 3: transmit, then prepare | The prepared buffer is sent on the tick; the next frame is unpacked after | [`../design.md`](../design.md) section 5 |
| Feedback 6: numeric priorities and cores | Display Engine 20 on core 1, Orientation 5 on core 1; WiFi and lwIP on core 0 | `panel_sync/main/tile_tasks.h` |
| Change requirement: 192 nibble extractions per frame, but the LED buffer cannot shrink (WS2812B needs 24 bits per LED) | Unpack after the send starts, inside the 14 ms of slack; the 192 B LED buffer kept (no change) | Wall tests: no one-tick slip on the camera runs ([ST-01](../testing/evidence/ST-01/)); 1 missed tick in 36,000 ([ST-07](../testing/evidence/ST-07/)) |
| Gap check: brightness was still a display input | Brightness removed from the Display Engine; the nibble is written straight to the LED value | tile test D1 (every value 0 to 8) |
| Feedback 9: accelerometer only, mounting assumption | Orientation from gravity, wall upright within 20 degrees; 26 equal readings before a change | FR17; tile tests O1 to O3; [ST-08](../testing/evidence/ST-08/) |

## 3. Gap-check suggestions we rejected

| Suggestion | Why rejected |
|---|---|
| `__attribute__((packed))` structs with bit-fields as the wire format | `packed` removes padding but not the compiler-defined order of bit-fields; Python has no bit-fields; a pointer to a packed member is unaligned and faults on the Pico's Cortex-M0+ |
| Protect the clock offset by disabling interrupts around each read | On the dual-core ESP32-S3 it masks only the calling core, so it does not protect a reader on core 1; a cross-core lock would delay the display tick |
| Remove the cycle-count logging from the unpack measurement | It already collects histograms, not per-frame prints |

## 4. Tests promised at Tribunal 1, and their status

| Test promised | Status | Evidence instead, or still missing |
|---|---|---|
| Packed round trip: a known pattern from the PC to the LED buffer | Host test passes; the hardware buffer dump was not run | `test_round_trip.py` (600 of 600 frames); every upload is CRC-checked on the wall ([ST-09](../testing/evidence/ST-09/)) |
| Fault-word stress: tasks on both cores set bits a million times | Host test passes; not run on the hardware | tile test F5 (two setters and a reporter, 0 lost) |
| Tick timing, 8-bit against packed asset, 10 min each | Not run | the wall results: no missed frames visible on the camera runs; 1 missed tick in 36,000 |
| Re-upload of 57 chunks while 2 tiles play: beacon lateness under 5 ms | Not run as specified | uploads take about 2 s per tile ([ST-09](../testing/evidence/ST-09/)) |
| Torn-read stress on the clock offset | Host test passes; not run on the hardware | tile test 4 (200,000 forced interleavings) |
| 30 min at full white: current and heat | Not run | the brightness cap of 8/255 by design |

These gaps are also listed in section 6 of
[`../testing/TEST_REPORT.md`](../testing/TEST_REPORT.md).
