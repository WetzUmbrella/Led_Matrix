# game_tile: design defence (whack-a-mole on the tile)

Owner: Cheston (Buddy 3) for the comms side: message handling, ACKs, seq order, TAP and ACK encoding, the seqlock. Jia Jun (Buddy 5) owns the drawing (sprites and layers). Cheston should still be able to explain section 6. Files: `panel_sync/main/game_tile.c/.h`, called from `panel_sync/main/tile_net.c` (`tn_on_game()`, `tn_game_service()`, `tn_pico_restarted()`) and `panel_sync/main/display_engine.c` (reads the state each tick). The sprites live in `game_sprites.c/.h`. Host tests: `panel_sync/test/test_game_tile.c`, cases G1 to G7 (`make -C panel_sync/test`; pass on 25 Nov, `docs/testing/results/2026-11-25/panel_sync.log`). Coding standard: Barr-C:2018, deviation D-1 (C11 `stdatomic.h`, as in `playback_control.c`). Written 25 Nov 2026 against `a6d4902`.

**Names.** Public functions have the `game_tile_` prefix. The state is `game_tile_state_t`, owned by tile_net as `g_game`. A "target" is what MOLE_ON puts on a tile: a mole, or the start sprite (kind START, `up_ms` 0). The "picture list" is the 4 WALL_PICTURE entries used for the countdown and the results.

Each entry gives the design choice, the requirement or design row it comes from (`docs/plan/game_plan_v2.md`, `protocol/layout_table.md`), and a question an examiner is likely to ask.

## 1. Pure C, and a seqlock from the Network Task to the Display Engine

- Source: NR-10 (host gate); W6 task model (one writer per shared object); `playback_control.c`'s seqlock pattern.
- Choice: the module makes no ESP-IDF call except C11 atomics, so all of it runs in the host test. Only the Network Task (tile_net) writes the state: it calls `game_tile_on_pico()` and then `game_tile_publish()`. The Display Engine (core 1, every 60 Hz tick) and the Orientation Task read it with `game_tile_read()`. The seqlock counter is odd while a publish is in progress. A reader retries until it sees the same even value before and after copying the words. The state is all 32-bit and 64-bit integers, padded to a whole number of 32-bit words (`_Static_assert`), so each word is copied atomically. There is no mutex, so the display tick never blocks on the network task.
- Examiner: "The network task publishes in the middle of a display tick. What does the Display Engine draw, and why can it never draw half a MOLE_ON?"

## 2. ACK everything addressed to the tile, apply it only once

- Source: layout_table.md section 10 item 13 (one Pico seq counter for the game messages); re-plan (acked game messages); G2.
- Choice: every MOLE_ON, FEEDBACK, GAME_CONTROL and WALL_PICTURE with this tile's Node ID is ACKed, including a repeat or an older seq. A repeat means the Pico lost the earlier ACK, and the Pico stops resending only when an ACK arrives. The tile sends each ACK once. The Pico resends every 50 ms (`CL_ACK_RETRY_US`) until the message's lifetime ends: FEEDBACK 300 ms, GAME_CONTROL 1 s, a MOLE_ON until its `t_show` (a late mole is worse than none). A message for another tile is neither ACKed nor applied. `game_tile_on_pico()` returns true only for the four game types, and tile_net routes every other message elsewhere.
- Examiner: "Why ACK a message you are about to ignore as old?"

## 3. Three seq baselines, so a lost ARM still arms

- Source: header of `game_tile.h`; G3 ("a lost ARM, seq 30, resent after its start sprite, seq 31, still arms the round, and the sprite stays").
- Choice: seqs are 16-bit and compared modulo 2^16. A seq is newer when it is 1 to 32,767 ahead (`game_tile_ahead()`). The tile keeps three baselines:
  - `seq`: MOLE_ON and FEEDBACK act only when newer than every message accepted, because a stale mole or flash is wrong.
  - `ctl_seq`: GAME_CONTROL acts when newer than the last GAME_CONTROL, even if a newer MOLE_ON has already arrived. The case: ARM (30) is lost, the start sprite (31) arrives, then ARM is resent. A single baseline would drop the resent ARM, and the tile would show the sprite with no holes and the wrong tap threshold.
  - `clear_seq`: ARM and START clear only the target and picture entries *older* than themselves, and remember their own seq. A WALL_PICTURE is stored when it is newer than its own entry and newer than the last clear. Countdown entries may therefore arrive in any order, and a results picture from the last round that arrives late is dropped.
- Examiner: "Draw the seq timeline for a lost ARM with a single baseline, and show what the child sees."

## 4. A restarted Pico starts from 0

- Source: wall test 18 Nov 2026 (fix `77e3165`, `game_tile_new_pico()`); G7.
- Choice: a restarted Pico numbers its messages from 0 again. On 18 Nov only `seq` was reset, so ARM, START and every picture from the new Pico were dropped as older than `ctl_seq` and `clear_seq`. Tiles that stayed up through the restart then showed no holes and kept the old results. Now, when tile_net sees a new Pico (`tn_pico_restarted()`), `tn_game_new_pico()` calls `game_tile_new_pico()`. That clears the round, the target, the picture list and all three baselines, and tile_net then republishes.
- Examiner: "How does the tile know the Pico restarted, as opposed to a burst of loss?" (Answered in `tile_net_DEFENCE.md`. This module only defines what is forgotten.)

## 5. Two clocks: shared times on the Pico clock, the flash on the local clock

- Source: DS-01 (local hit burst); NR-03; timesync (each tile's estimate of the Pico clock).
- Choice: everything that must line up across tiles is drawn against the Pico's corrected clock: the target's show and hide times and the picture entries' `t_show`. The countdown digits and the results therefore change on every tile in the same frame. FEEDBACK has no time field. Its flash starts when the message arrives, on the tile's local clock (`flash_start_us`), and lasts 250 ms for a hit or 300 ms for a miss. That way a time-sync correction in the middle of the flash cannot stretch or cut it. Times are `int64_t` microseconds, which is about 292,000 years, so they never wrap.
- Examiner: "Why not stamp FEEDBACK with a Pico time like MOLE_ON?" (Nothing on another tile has to line up with it, and a future show time would only add latency to NR-03.)

## 6. What the tile draws, and in which order

- Source: DS-11 (visuals), D-rows of section 4.4, NR-09 (every channel value at most 8).
- Choice: `game_tile_fill_wire()` builds an upright 8x8 canvas in layers, from the bottom up:
  1. The hole, while a round or the tap test is on. It is not drawn under the start sprite (bench 12 Nov: the sprite over a hole read as clutter).
  2. The target. A mole rises 2 rows every 40 ms (160 ms in all), stays until `hide`, then sinks back. The start sprite pulses until tapped.
  3. The picture showing, which covers the tile.
  4. The hit burst (the mole is squashed if it was up) or the red X.

  It then writes the 192 wire bytes: each physical LED takes its source pixel through `display_frame_source_led(led, orientation)`, so the picture stays upright on a turned tile. The Display Engine draws the game instead of the video only while `game_tile_active()` is true (a round, the tap test, a picture or a flash). All sprite values are 0 to 8, so NR-09 holds without scaling on the tile.
- Examiner: "A tile is turned 90 degrees during the results. Why does the score digit stay upright?"

## 7. The picture list (countdown and results)

- Source: DS-09 (wall pictures); B1 (3-2-1 countdown), B5 (results held until the next ARM).
- Choice: 4 entries, one WALL_PICTURE each (a 2-bit slot). The entry showing is the one that started most recently and has not ended. `dur_ms` 0 means "until replaced". An entry with `repeat` loops the list back to entry 1 when it ends: the results show a sparkle, then score and stars in turn, until the next ARM. END keeps the list, and ARM, START or effect CLEAR empties it. Each entry carries a 64-bit mask (this tile's piece of the whole-wall picture) and a colour, so one 23 B message draws one tile's share of a digit.
- Examiner: "The results loop forever on the wall. What message stops them, and what if it is lost?" (The next ARM, resent every 50 ms for up to 1 s, applied by `ctl_seq`.)

## 8. TAP: from the accelerometer to the Pico (tile_net side)

- Source: DS-02 (detector), DS-07 (TAP sent at the spike, resent every 50 ms until ACKed).
- Choice: `tap_detect` runs in the Orientation Task at 1 kHz and puts each tap (local time, peak in mg) on a queue. In each housekeeping pass, `tn_game_service()` converts the local time to Pico time with the time-sync snapshot and encodes a TAP (`game_tile_encode_tap()`: session, seq, Node ID, peak, `t_tap`). It adds the TAP to an outbox that resends every 50 ms (`TN_TAP_RETRY_US`) until the Pico ACKs it, for up to 1 s (`TN_TAP_LIFE_US`). If the tile has no clock sync yet, the tap is dropped and counted (`g_tap_unsynced`), because a local time would mis-score it. ARM's threshold (20 mg steps) is passed to the detector (`orientation_set_tap_threshold()`, website A12). The ACK the tile sends uses its own counter, because nothing ACKs an ACK.
- Examiner: "Why is the tap stamped on the tile and converted to Pico time there, instead of the Pico stamping it on arrival?" (Arrival spreads 15 to 100+ ms on WiFi. The tile stamp is within about 10 ms of contact, and the Pico groups taps by these stamps.)

## 9. Wire sizes (Week 13 minimum-bit packing)

From `docs/week13/message_sizes.md`, each row against the earlier byte-aligned layout:

| Message | Packed | Was |
|---|---|---|
| TAP | 15 B (113 bits) | 23 B |
| MOLE_ON | 14 B (112 bits) | 24 B |
| FEEDBACK | 5 B (35 bits) | 14 B |
| GAME_CONTROL | 6 B (47 bits) | 17 B |
| WALL_PICTURE | 23 B (182 bits) | 37 B |
| ACK | 7 B (49 bits) | 17 B |

These sizes are derived from the bit layout. No packet-sniffer capture was taken, and the Week 13 doc says so.

## 10. GPIO4 event pulses (measuring NR-03)

- Source: DS-13 (measuring the targets).
- Choice: in game mode the Display Engine raises GPIO4 for 10 ms in a mole's first frame and for 6 ms in a hit burst's first frame (`game_tile_pulse_us()`). The Orientation Task raises it for 2 ms when it detects a tap. Each event has its own width, so a logic analyser can tell them apart on one pin. In the end NR-03 was measured another way: the Pico's `GAME_TAP` arrival log plus a 240 fps video (`docs/bench/25nov/nr03.md`). The pulses were never captured on the bench, so say that if asked.

## 11. Tests

| Case | What it pins |
|---|---|
| G1 | The mole rises and sinks; a hit squashes it with the green burst; a miss blinks the red X twice and the mole stays; an unknown colour draws white |
| G2 | Only this tile's messages are ACKed; a repeat is ACKed and not applied; an older seq is ACKed and not applied |
| G3 | ARM shows holes and carries the threshold; the start sprite; a lost ARM resent after the sprite still arms; START; END clears the holes; tap test on and off |
| G4 | Countdown entries out of order all show at their times; ARM drops a late older entry; results sparkle then loop entries 1 and 2; END keeps the list, CLEAR empties it; rotation lights the right LED |
| G5 | GPIO4: 10 ms mole, 6 ms hit, none for the start sprite or a miss |
| G6 | TAP and ACK encodings round-trip; a publish reads back whole |
| G7 | After `game_tile_new_pico()`, a restarted Pico's ARM and pictures are taken |

## Known limits

1. **A tile restarted mid-round** comes back with an empty state and draws nothing until the results (ST-13). The Pico sends the round's START once and does not resend it to a returning tile. See `game_rules_DEFENCE.md` known limit 2 for the fix.
2. A FEEDBACK lost on every copy (300 ms of resends) means no flash. The score on the Pico is still right, because only the visual is lost.

## Questions to practise before the demo

Answer each in your own words, without reading the code.

1. MOLE_ON seq 40 arrives, then a repeat of FEEDBACK seq 39. What does the tile do with each, what does it ACK, and why?
2. Explain the seqlock in four steps on the writer side and three on the reader side. Why must the struct be a whole number of 32-bit words?
3. Why does the countdown "2" change on all nine tiles in the same frame, when the WALL_PICTUREs reach the tiles at different times?
4. The Pico restarts in the middle of the results. List what each tile forgets and why it has to.
