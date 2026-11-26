# fault_word: design defence (T-B3-06)

Subsystem: tile fault reporting (ESP32-S3 tile). Files: `panel_sync/main/fault_word.c/.h`; host tests `panel_sync/test/test_fault_word.c/.h`, run by `make -C panel_sync/test` (line "Fault word: Pass = 6 of 6", 25 Nov). Coding standard: Barr-C:2018, `docs/coding_standard.md` (deviation D-1 only). Updated 25 Nov 2026 against `a6d4902`: the design below is unchanged since October; section 8 now records what was built after it.

**Names.** Barr-C 6.1.i requires the module prefix on every public function, and the module file is `fault_word.c`, so the plan's names are implemented as: `fault_set(bits)` = `fault_word_set()`, `fault_clear_condition(bits)` = `fault_word_clear_condition()`, `fault_snapshot()` = `fault_word_snapshot()`. The plan describes the ACK clear in words ("clears (sent bits AND `EVENT_MASK`) with an atomic fetch-and") without a function name; it is `fault_word_clear_events(sent_bits)`. The plan's `EVENT_MASK` is `FAULT_EVENT_MASK`; `FAULT_CONDITION_MASK` is added as its complement over the bits in use. Bit names are `FAULT_BIT_<source>`. Quotations below keep the plan's wording.

Each entry gives the design choice, the PostTri row it comes from (inventory ID and wording, quoted from `Plan/commitment_inventory.md` or the T-B3-06 plan row), and the question an examiner is likely to ask. Numbers not in the plan are labelled (plan choice).

## 1. One 32-bit atomic word, never a bit-field

- Source: LDC-2, "One 32-bit fault word: atomic fetch-or to set; snapshot, send, clear only the sent bits; event and condition faults classified."; T-B3-06, "One 32-bit fault word, never a bit-field."
- Choice: `static _Atomic uint32_t g_fault_word`, private to `fault_word.c`. Bits are `uint32_t` masks (`FAULT_BIT_*`), so their order is fixed by the code, not by the compiler's bit-field layout, and the same value can go on the wire through `common/proto` with explicit shifts and masks.
- Examiner: "Why is a bit-field struct unsafe here even if every task only ever writes its own member?"

## 2. Set with an atomic fetch-or

- Source: VB-3a, "Fault word set with atomic fetch-or."; CB-3, "atomic set and clear-after-ACK"; RAR-1, "Ahsanul and Jia Jun raise faults only through the shared set/clear functions."
- Choice: `fault_word_set()` is `atomic_fetch_or_explicit(..., memory_order_release)`. A plain `word |= bit` is a load, an OR and a store; two tasks on the two cores can both load the old word and one store then wipes the other's bit. The fetch-or is one indivisible read-modify-write.
- Examiner: "Display Engine on core 1 and Network Task on core 0 set different bits in the same microsecond. Walk through what `|=` would do and what the fetch-or does instead."

## 3. Memory order: release on set, acquire on snapshot, relaxed on clears

- Source: VB-3a and LDC-2 (atomic fetch-or); D-1 in `docs/coding_standard.md`.
- Choice: all read-modify-writes on one atomic object are in a single modification order, so no bit is lost whatever order is chosen. The order only matters for other memory. Release on set and acquire on the snapshot mean that a write a task made before raising a fault is visible to the Network Task once its snapshot sees the bit. That pairing orders only writes made before the set. A counter the raising task keeps incrementing after the set (a drop counter, the missed-deadline counter) is still read while it is being written, so every count that another task reads is itself a C11 atomic; the pairing does not make a plain counter safe. The clears use relaxed because a clear publishes nothing else, and the telemetry seq and sent bits stay inside the Network Task (plan choice).
- Examiner: "If every operation were relaxed, could a bit still be lost? What could go wrong instead?"

## 4. Event bits clear only on the TELEMETRY_ACK

- Source: VC-4, "the Network Task clears sent bits only on that ACK and only bits in EVENT_MASK"; R4, "Atomic operations; clear only on the telemetry ACK (not on `sendto()`); EVENT_MASK bits only; condition faults latched and resent."; T-B3-06, "On TELEMETRY_ACK with that seq it clears (sent bits AND `EVENT_MASK`) with an atomic fetch-and."
- Choice: `fault_word_clear_events(sent_bits)` does `fetch_and(~(sent_bits & FAULT_EVENT_MASK))`. Bits not in the acknowledged packet are untouched, so an event raised after the snapshot rides in the next packet. Host test F2 checks this.
- Examiner: "Why is clearing after a successful `sendto()` not enough on a broadcast-heavy WiFi network?"

## 5. Repeats between send and ACK merge into one report

- Source: VC-4, "repeats between send and ACK merge into one report; counts, if kept, go in a separate counter."
- Choice: an event bit that was sent and is raised again before its ACK is cleared by that ACK, because the word holds one bit per source, not a count. The repeat is therefore reported once. Counts stay in each source's own counter (for example the missed-deadline counter in `display_engine`, the Time Sync Queue drop counter in `tile_net`). Host test F4 checks the merge and that a first-time event after the snapshot survives.
- Examiner: "The dashboard shows MISSED_DEADLINE once, but the tile missed three ticks. Where does the team read the real number from?"

## 6. Condition bits clear only through their owner

- Source: VB-3b, "Event faults read-and-clear; condition faults latched until the owning task clears them."; T-B3-06, "Condition bits are cleared only by their owning task."
- Choice: `fault_word_clear_condition()` masks its argument with `FAULT_CONDITION_MASK`, so a condition owner can never drop an unreported event, and `fault_word_clear_events()` masks with `FAULT_EVENT_MASK`, so an ACK can never drop a condition. The owner of each condition is named in `fault_word.h`. Host test F3 checks three ACKs leave a condition set and the owner's clear ignores an event bit.
- How "owner only" is checked: the function cannot know its caller, so ownership is a review check, not a run-time one. `grep -rn "fault_word_clear_condition" panel_sync/main` must list only calls whose `FAULT_BIT_*` argument is owned by the file the call sits in (WIFI in `tile_net.c`, BEACON_LOSS in `playback_control.c`, IMU in `orientation.c`, ASSET_FORMAT in `asset_store.c`, DISPLAY in `display_engine.c`). The pull-request reviewer runs the grep and records the count of calls outside their owner's file, which must be 0 (plan choice: a grep is the cheapest check that covers every caller).
- Examiner: "Telemetry packets are being lost. Why does a held condition such as ASSET_FORMAT still show continuously on the dashboard, with no off/on flicker?"

## 7. Bit map and EVENT_MASK membership (PROVISIONAL, OD-12)

- Source: OD-12, "One bit per source in CR1 disc. section 3 (WiFi loss, CRC failure, queue overflow, beacon loss, IMU fault) plus MISSED_DEADLINE, ASSET_FORMAT and DISPLAY", owner Cheston; fixed classes: T-B5-03 "raise the MISSED_DEADLINE event bit", "sets the DISPLAY condition bit"; T-B5-05 "the IMU condition bit"; T-B3-05 "the WIFI condition bit"; VC-5 "raises an ASSET_FORMAT condition fault".
- Choice: bits 0 to 8: WIFI (condition), CRC_FAIL (event), QUEUE_OVERFLOW (event), BEACON_LOSS (condition), IMU (condition), MISSED_DEADLINE (event), ASSET_FORMAT (condition), DISPLAY (condition), SYNC_STALE (event). 23 bits stay free.
- Examiner: "Is a CRC failure an event or a condition? What changes on the dashboard if you pick the other?"

## 8. Built since, and used on the wall

- `tn_telemetry_send()` and `tn_on_telemetry_ack()` in `tile_net.c` do exactly the cycle in section 4: snapshot, send TELEMETRY {seq, fault word, orientation, display state} every 1 s, remember (seq, sent bits) in the 4-entry `telemetry_book`, and call `fault_word_clear_events()` on the ACK with that seq (host tests T1 to T5; T5 drops every third packet: 100% of events reported, 0 flickers of a held condition).
- The website shows each tile's fault bits. The bench scripts refuse to arm a round while a tile reports a fault (12 Nov 2026, after a DISPLAY fault went unnoticed and froze the LEDs). Since 24 Nov they warn, but still arm, on one-off event bits (user's decision 2).
- The Telemetry Queue is length 2, drop-oldest, with its drop count carried in each item. There is no separate Fault Queue: the fault word is the only path, so its length question is closed.
- Host test F5 (two POSIX setter threads, 100,000 sets each, one reporter: 0 lost) stands in for T-X-02. The 1,000,000-set run on the two cores (T-X-02) and the fault-word stress log of Week 13 were **not run on hardware**. Say so if asked.

## Settled values (25 Nov 2026)

1. Bits 0 to 8: WIFI (condition), CRC_FAIL (event), QUEUE_OVERFLOW (event), BEACON_LOSS (condition), IMU (condition), MISSED_DEADLINE (event), ASSET_FORMAT (condition), DISPLAY (condition), SYNC_STALE (event). 23 bits free.
2. CRC_FAIL, QUEUE_OVERFLOW and SYNC_STALE are events: each is one occurrence, and the count lives in the source's own counter. QUEUE_OVERFLOW is one bit for all queues.
3. BEACON_LOSS is a condition owned by `playback_control`. It stays set until beacons return.
4. Release on set, acquire on snapshot, relaxed on clears (section 3).

## Questions to practise before the demo

Answer each in your own words, without reading the code.

1. Explain why a fetch-and that clears only `(sent bits AND EVENT_MASK)` cannot lose an event raised after the snapshot, and what would be lost if the Network Task cleared the whole snapshot.
2. A condition bit and an event bit are raised together, and the TELEMETRY packet carrying both is dropped twice before an ACK arrives. What does the website show for each bit over those three packets?
3. Why C11 atomics (deviation D-1) and not a FreeRTOS critical section around `word |= bit`, when the Display Engine on core 1 raises MISSED_DEADLINE from its tick path?
