# Correctness and performance after the minimum-bit change (CR1, Week 13)

The change requirement asks for evidence that the packed representation causes "no loss of required functionality and no unacceptable degradation in timing or system performance". This page collects that evidence for the final demo build (tiles `630c390`, Pico `f9d313c`, website at `feature/whack-a-mole`), 25 Nov 2026. The data representation table is `data_representation.md`, RAM is `before_after.md` (section 4 for the final build), message sizes are `message_sizes.md`.

## 1. Correctness of the packing itself (host tests)

| What | Evidence |
|---|---|
| Every field packs and unpacks to the same value, at its range limits, in C and Python | `common/proto/test` (`test_proto`, plain and ASan); `pc/tests/test_proto_layout.py`; `pc/tests/test_cross_lang.py` (Python encodes, C decodes and back, the same bytes) |
| Out-of-range values and spare codes are refused, not truncated | the same suites: both codecs refuse spare codes (`layout_table.md` sections 5.9 to 5.15) |
| Round trip of a whole asset (4 bits per channel) | `pc/tests/test_round_trip.py`; on the wall every upload is checked by CRC32 before ASSET_READY |
| Fields wider than their STATUS bits are clamped, not wrapped | `cl_count7()` and `cl_reaction11()` in `ledwall_sync/coordinator_loop.c` |

## 2. Shared packed data is accessed safely

| Shared data | Writers and readers | How it is safe | Test |
|---|---|---|---|
| Tile fault word, 32 bits (`panel_sync/main/fault_word.c`) | every task sets bits; the Network Task reports and clears them | one `_Atomic uint32_t`, `atomic_fetch_or_explicit` to set and `atomic_fetch_and_explicit` to clear: no read-modify-write window | `panel_sync/test`: F1 to F6, F5 "two setters and a reporter, 0 lost" |
| Time sync state: offset, drift, reference (`panel_sync/main/timesync.c`) | Network Task writes; display task reads every frame | seqlock: relaxed 32-bit atomic halves under one sequence counter; a reader retries a torn copy | `panel_sync/test` timesync seqlock cases A to C |
| Readiness and online bitmaps, 9 bits each in one `uint16_t` (Pico) | the Pico's single poll loop | single writer in one context (no RTOS, no ISR writes) | `ledwall_sync/test` coordinator cases |

## 3. No loss of function, no unacceptable slowdown (wall, 24 and 25 Nov)

| Requirement | Result on the wall | Record |
|---|---|---|
| Video sync between tiles (NR-07, brief: under 5 ms target) | every filmed pair of tiles 4 to 9 under 5 ms by 240 fps camera | `docs/testing/evidence/ST-01/` |
| Upload and verify of the packed asset | 9 tiles in parallel, about 2 s each, 0 chunks resent, CRC32 checked | `docs/testing/evidence/ST-09/` |
| Tap to hit feedback (NR-03, under 300 ms) | median about 171 ms, worst about 196 ms (150 ms of it is the deliberate echo wait) | `docs/testing/evidence/ST-02/` |
| Scoring | 13 rounds in a row on 3x3, 412 hits judged by the Pico from packed TAP and FEEDBACK messages (24 Nov, one tapper: 34 of 34, 0 wrong) | `docs/testing/evidence/ST-14/`, `docs/testing/evidence/ST-03/` |
| Stability (NR-06, 10 rounds without a reboot) | 13 rounds, no Pico restart | `docs/testing/evidence/ST-04/` |
| A tile lost and back during video (continue policy) | the wall keeps playing; the tile rejoins in step | `docs/testing/evidence/ST-11/` |
| Display frame work | each frame is unpacked from the 4-bit asset every tick; the RMT driver sends it by DMA (flicker fix, 12 Nov); the NR-07 camera runs found no one-tick slip | `docs/testing/evidence/ST-01/` |

## 4. Known limits (stated, not hidden)

- On-air UDP sizes were not captured with a packet sniffer; the message sizes are the encoder's lengths (`message_sizes.md`, "derived").
- Free heap "before" was never measured at the tag; the final build was measured on 2 boots, not the planned 10.
- The planned 8-bit vs packed tick-timing profile (`prep_profile_*.log`) was not run; performance is shown by the wall results in section 3 instead.
