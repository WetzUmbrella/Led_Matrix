# game_rules: design defence (whack-a-mole rules on the Pico)

Owner: Cheston (Buddy 3). The file headers still say Benjamin (B2), from the 5 Nov re-plan table. Since 9 Nov Cheston and Claude have built the game, and Cheston defends this module. Files: `ledwall_sync/game_rules.c/.h`, called from `ledwall_sync/coordinator_loop.c` (`cl_game_routine()`, `cl_game_start()`, `cl_game_end()`, `cl_round_arm()`, `cl_round_play()`, the TAP case of the dispatcher). Host tests: `ledwall_sync/test/test_game_rules.c`, cases C17, C18, C18b, C18c, C18d (`make -C ledwall_sync/test`; 43 of 43 pass on 25 Nov, `docs/testing/results/2026-11-25/ledwall_sync.log`). Coding standard: Barr-C:2018, `docs/coding_standard.md`. Written 25 Nov 2026 against `a6d4902`.

**Names.** Every public function has the `game_rules_` prefix (Barr-C 6.1.i). The engine is one struct, `game_t`, owned by the coordinator as `g_game`. Statics use `game_` (`game_pick_node()`, `game_resolve_window()`, `game_tap_better()`, `game_plan_mole()`). A "group" or "window" is the set of TAPs that the rules judge as one tap.

Each entry gives the design choice, the requirement or design row it comes from (`docs/plan/game_plan_v2.md`), and a question an examiner is likely to ask. Numbers that come from no requirement are labelled (plan choice).

## 1. Pure C with no clock of its own, one event per poll

- Source: NR-10 (Barr-C, host gate); DS-08, "all decided on the Pico"; coordinator_loop section 2, "one bounded routine per loop pass".
- Choice: the module makes no SDK call, reads no timer and does no I/O. Every function takes `now_us` (or the tap's times) as an argument, and the RNG seed is injected, so the host test can run a whole 30 s round on a fake clock in 1 ms steps. `game_rules_poll()` returns at most one event (MOLE_ON, FEEDBACK or ROUND_END). The coordinator turns that event into messages and sends them, so each loop pass does a bounded amount of work. `game_rules_next_due()` gives the earliest time anything can change, and the coordinator only calls `poll` from then on. The host test checks that no quiet poll ever happens at or after `next_due` (`tst_run()`), so a waiting round never takes loop passes from the receive path.
- Inside one poll the order is fixed. A closed tap group is judged first. Then a mole may time out, the next mole may be planned, and the round may end. Judging taps before the timeout means a tap made before the hide time still counts when the mole expires in the same pass.
- Examiner: "Why does the rules module not just read `time_us_64()` itself?" (Testability: C17 runs 30,000 polls in milliseconds, and C18d replays bench timings exactly.)

## 2. Choosing the next mole

- Source: B3, "one target at a time, on a random active tile, never the same tile twice in a row (unless one tile is active)".
- Choice: the RNG is xorshift32 (`game_next_rand()`), seeded with the Pico's boot time in `coordinator_loop.c`. A seed of 0 is replaced by 1, because xorshift stays at 0 forever. The candidates are the active bitmap (9 bits, bit n-1 is Node ID n) minus the last mole's tile, unless that leaves nothing. The pick is `rand % count`, and the code then walks to the pick-th set bit. With 9 tiles the modulo bias is at most 9 in 2^32 (negligible). `game_rules_set_active()` takes a lost tile out of the choice (`cl_game_sync_active()` calls it when a heartbeat times out or the tile comes back).
- Examiner: "Two tiles are active and one is unplugged. What does B3 do, and why does the code not loop forever looking for a different tile?"

## 3. Mole timing: up time, lead, gap, the last mole

- Source: B2 (30 s round), B4 (Easy 2000 to 1200 ms, Normal 1500 to 800 ms, Hard 1200 to 500 ms, linear; 300 ms gap).
- Choice: `game_rules_up_ms()` is `first - (first - last) * elapsed_ms / 30000` in integers, rounded down. The worst product is 800 x 30,000 = 24,000,000, which fits easily. Hard at the first mole (elapsed 300 ms) gives 1200 - 700 x 300 / 30000 = 1193 ms, the value C17 pins.
- MOLE_ON goes out `GAME_LEAD_MS` = 200 ms before `t_show` (plan choice). The Pico resends every 50 ms until it gets an ACK (`CL_ACK_RETRY_US`), so 4 sends fit before the mole is due. The tile draws the mole at `t_show` on the synced clock, so radio delay does not shift the show time.
- The gap is `GAME_GAP_MS` = 300 ms. It runs from the hit tap's time, or from the hide time of a mole that timed out. If the plan point (next show minus 200 ms) has already passed, the show moves to now + 200 ms. Moving the show is safer than sending a MOLE_ON that is already late.
- A mole is planned only if it hides before the round ends. Otherwise no more moles are planned, and the round ends once the last mole and every waiting tap are judged (B2: "no target shows after 30 s").
- Examiner: "Why is the lead 200 ms and not 50 ms? What happens if every copy of a MOLE_ON is lost?" (The copy expires at `t_show`, so the tile never shows it. The Pico still counts the mole as up, and it times out as a miss. The weakness is accepted: at 1% loss, four copies all fail about once in 10^8.)

## 4. Which tile was tapped: highest peak wins

- Source: C4, "taps from several tiles within 50 ms count once; the highest peak wins"; DS-03 (sponge mounts, F-10 40 of 40); DS-07 changed the window to 30 ms.
- Choice: a hard tap shakes the board, so up to 9 tiles send a TAP for one tap. TAPs whose tap times (each tile's stamp in the Pico's corrected clock, not their arrival) fall within `GAME_DEDUPE_MS` = 30 ms of the group's earliest TAP form one group. The highest `peak` wins. A tie goes to the earlier stamp, and a tie on that goes to the lower Node ID (plan choice, so the result is deterministic). Only the winner is judged and gets a FEEDBACK. The other TAPs in the group are dropped as echoes.
- Why 30 ms: on the 11 Nov bench, echoes came 2 to 3 ms after the real tap on the tile clocks, so 30 ms is a wide margin. A shorter window lets a child's second tap count sooner.
- Rejected: "target wins", meaning the mole's tile wins if it reported at all. In the bench replay (F-05, F-10) that gave a free hit for every slap next to the mole.
- Examiner: "Why group by tap stamp and not by arrival time?" (WiFi delivers out of order and 15 to 100 ms late. The stamps are on one synced clock, so they show which taps belong to the same physical tap.)

## 5. The collect wait: 150 ms (accuracy over latency)

- Source: NR-03, revised 24 Nov 2026 by Cheston: "accuracy over latency", target under 300 ms; bench 11 Nov round 11; bench 24 Nov, 84 taps.
- Choice: a group is judged at whichever comes later: 30 ms after its earliest stamp, or `GAME_COLLECT_MS` = 150 ms after its first TAP arrived at the Pico (`game_group_due()`). The stamp window says which taps belong together. The arrival wait gives the slower TAPs time to arrive before the group is judged.
- Evidence: on 11 Nov the echo arrived 61 ms after its stamp and the real tap 10 ms later. Judging 30 ms after the stamp scored the echo alone as a miss. On 24 Nov the strongest TAP of a hard tap arrived up to 494 ms after the first echo. With a 20 ms wait, 25 of 84 taps were judged on an echo. With 150 ms, 5 of 84 were. Cheston chose 150 ms.
- Cost: tap to hit burst = arrival (median 17 ms, max 32 ms on 25 Nov) + 150 ms + about 4 ms to draw. Median about 171 ms, worst about 196 ms, and the 24 Nov worst case about 267 ms. NR-03 is met under 300 ms (`docs/bench/25nov/nr03.md`).
- Examiner: "Your first NR-03 said 100 ms. Why did you make it slower on purpose, and what data did you base the change on?"

## 6. Late duplicates and capacity: 8 windows, 32 slots

- Source: bench 24 Nov 2026, "a TAP can arrive up to 1 s late, after the next group closed".
- Choice: when a group closes, its start stamp goes into a ring of `GAME_WINDOWS` = 8 closed windows. A new TAP stamped within 30 ms either side of any of those starts is a late duplicate. It is dropped and counted in `dedupe_drops`, so it cannot score as a new wrong tap. 8 groups cover at least 2 s of play (plan choice). Up to `GAME_TAP_SLOTS` = 32 TAPs can wait at once: a hard tap reaches all 9 tiles, and two taps can wait inside one 150 ms collect wait, so 18 is the expected peak. A TAP beyond 32 is dropped and counted in `tap_drops`. TAPs from a tile that is not active, or that arrive outside a round, are ignored.
- The "either side" check matters: on 11 Nov an echo stamped 3 ms *before* the real tap arrived after the group had closed.
- Examiner: "A TAP arrives 1 s late. Walk through why it does not count as a wrong tap. What if it were 3 s late?" (By then its window has left the ring. It is judged on its own, and with no mole up at that stamp it counts as one wrong tap. That only adds to the wrong count, never takes away a point.)

## 7. Tap grace: a late TAP still hits

- Source: C5, "a tap stamped before the hide time still hits if it arrives up to 100 ms late".
- Choice: a mole is retired at `hide + GAME_TAP_GRACE_MS` (100 ms), and later if a waiting TAP was stamped before the hide (`game_mole_expired()`). A hit is judged on the winner's stamp: `show <= t_tap < hide`. The arrival time does not matter. A `_Static_assert` checks that the grace (100 ms) ends by the next mole's plan point (gap 300 minus lead 200). The grace therefore never delays the next mole.
- Examiner: "Show with the numbers that the grace cannot push the next mole later."

## 8. Scoring and results

- Source: C1 (+1, the mole ends at once), C2 (the score never goes down; a wrong tap counts one wrong action), C3 (a timed-out mole counts one missed target), C6 (stars), NR-11 (score consistency), layout_table STATUS score field.
- Choice: a hit adds 1 to the score and to `hits`, and the reaction time (stamp minus show) is added to `reaction_us`. A wrong tap (wrong tile, or no mole up at that stamp) adds 1 to `wrong`, sends FEEDBACK miss (the red X) and leaves the score unchanged. That is "no penalty for children", decided in build step 3; v1 had -1. A timeout adds 1 to `missed` and sends nothing. Stars are hits x 100 / shown: 3 at 80% or more, 2 at 50% or more, else 1. A child always gets at least 1 star, even with no mole shown. The score is clamped to -99 to 99 because STATUS carries it in 8 bits. With no penalty it cannot go below 0, but the clamp protects the field. The average reaction is reported in ms for the website.
- Examiner: "Where does the website's score come from, and how do you know the wall shows the same one?" (`game_rules_score()` goes into STATUS every 250 ms and into the results WALL_PICTUREs; NR-11.)

## 9. How the coordinator drives a round

- Source: DS-08, round states GAME IDLE, ARMED, COUNTDOWN, ROUND, RESULTS.
- Choice: the round states live in `coordinator_loop.c` (`g_round`), not in the rules. ARM calls `game_rules_clear()`, so STATUS shows no counts from the last round, and the start sprite goes up on the centre tile. A tap on that sprite (at least `CL_START_MIN_US` = 400 ms after it shows, which filters out a leftover tap) starts the 3 s countdown. The countdown's end calls `game_rules_start()` with the online tiles of the round. Taps go to `game_rules_tap_rx()` only in COORDINATING_GAME with the active session id. ROUND_END, or an abort from the website, calls `cl_game_end()`: END goes to the tiles, the score is drawn as WALL_PICTUREs, and the coordinator returns to READY. A lost router stops the round (`game_rules_stop()` in the FAULT path).
- Examiner: "A child taps the start sprite twice. Why does the second tap not start a second countdown?"

## 10. Tests

| Case | What it pins |
|---|---|
| C17 | 30 s round, no taps: first mole at 300 ms with 1193 ms up time, the curves per level at 0, 15, 30 and 45 s, a 300 ms gap after every timeout, no tile twice in a row, at least 20 moles, ROUND_END at 30 s with score 0, `next_due` never late |
| C18 | Hit +1 ending the mole; a tap before the show and a tap on the other tile both count as wrong with no penalty; dedupe by highest peak (peak 900 beats 100); a late duplicate dropped; a stopped round ignores taps |
| C18b | A tap stamped 10 ms before the hide and delivered late still hits; a tap after the hide misses; a timeout keeps the score |
| C18c | The 11 Nov round 11 case: a tap and its echo stamped 3 ms apart, arriving 61 and 75 ms after their stamps, give one HIT |
| C18d | The 24 Nov case: a real TAP 140 ms behind its echoes wins; two hard taps waiting at once all fit; an echo 1 s late, after two more groups closed, is a duplicate |

On the wall: NR-03 (25 Nov, about 171 ms median), rehearsal of 13 rounds with no Pico restart (NR-06), and 24 Nov with one tapper: 34 of 34 rounds with 0 wrong (`docs/bench/24nov/`, `docs/bench/25nov/`).

## Known limits (state them before you are asked)

1. **Several tappers at once** (rehearsal 25 Nov: 121 wrong to 412 hits; one tapper on 24 Nov: 0 wrong). The rules assume one tapper. Two taps less than 30 ms apart are one group and only the stronger one is judged. With overlapping taps the echoes of one tap can also win groups of their own. For the demo, one child taps at a time.
2. **A tile restarted mid-round shows nothing until the results** (ST-13, accepted as a known limit). `cl_game_sync_active()` puts it back in the mole choice, but the round state is not resent to it, so a mole sent to it stays invisible and times out as a miss. Fix: resend the round state to a returning tile, or keep it out of the choice until the round ends.
3. **A lost TAP from the tapped tile lets an echo win.** That is about 1 in 100 taps at 1% loss, and the 50 ms resend makes it rarer. It shows up as a wrong tap, never as a free hit.
4. DS-02's turn check (a twist that reads as a tap) is parked with E3. Any touch on a mole whacks it (option A).

## Questions to practise before the demo

Answer each in your own words, without reading the code.

1. A child taps tile 5, the mole tile. Tiles 4, 5 and 6 all send TAPs, and tile 5's TAP arrives last, 120 ms after tile 4's. Walk through `game_rules_tap_rx()`, `game_group_due()` and `game_resolve_window()` until FEEDBACK goes out. Give the time at each step.
2. Why are the stamp window (30 ms) and the collect wait (150 ms) two separate numbers? What would break if you used one number for both?
3. An examiner says: "Just make the mole's own tile win if it reported." Give the bench evidence against it.
4. Show why the score sent in STATUS cannot disagree with the score drawn on the wall at the end of a round.
