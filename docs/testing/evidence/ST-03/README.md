# ST-03: Scoring with one tapper

**Requirements.** FR14, NFR4 (each hit scores once), NFR15

**Set-up.** 3x3 wall, game mode, hard level, threshold 620 mg, one player tapping hard.

**Procedure.** Play rounds with the Pico's serial log open. Join the Pico's `GAME_TAP` events with the website's tap log (`tools/echo_sim.py`) to see which TAP each verdict was based on.

**Pass rule.** Every mole hit counts once, and no echo on a neighbouring tile is judged as a tap.

**Result.** Before the fix (`pico_round_echo.log`): 20 hits and 38 wrong in one round. A hard tap shakes the neighbours; their echo TAPs reach the Pico with a delivery delay of median 50 to 64 ms and p95 285 to 385 ms, so with a 20 ms collect wait a group was often judged on an echo. A replay of the Pico's rules on 84 taps judged 36 on an echo; a 150 ms collect wait and remembering the last 8 closed groups left 5. After the fix (`pico_round_fix.log`): **34 of 34 moles hit, 0 wrong, 0 missed, 3 stars.** Host test C18d replays the case. **Verdict: FAIL, fixed, then PASS.**

**Limits.** The radio was calmer during the fixed round (p95 delivery 160 ms), so not all of the gain is the fix. Several players tapping at once are not covered (see ST-14).

## Files

| File | What it is |
|---|---|
| `pico_round1.log, pico_round2.log` | rounds with hard taps before the fix |
| `pico_round_echo.log` | the two rounds analysed for echoes |
| `pico_round_fix.log` | the round after the fix: 34 of 34 |
| `tools/echo_sim.py` | joins Pico events with the tap log and replays the rules |
