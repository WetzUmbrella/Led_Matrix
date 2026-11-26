# ST-07: Time sync through a WiFi stall

**Requirements.** FR8, NFR1

**Set-up.** Tiles 7, 8 and 9 on USB, logging every beacon they receive (`BCN,seq,t_pico,t_local`) with `tools/tile_capture.py`.

**Procedure.** `tools/ts_replay.py` replays the firmware's time-sync filter on the logged beacons and compares each tile's estimate with the true offset (each tile's lowest-delay envelope). During the 19 min log a 0.65 s WiFi stall delivered nine beacons 95 to 648 ms late in a row. Then the fixed firmware ran on the wall for 9 minutes with all 9 tiles playing.

**Pass rule.** No clock jump and no re-seed on a stall; skew between tiles under 5 ms in the replay.

**Result.** With the old rule the stall made every tile re-seed and the whole video jumped 667 ms (replay: skew max 665 ms). With the fix (re-seed only on a Pico restart, `630c390`): **no re-seed, skew max 1.26 ms, p99 0.35 ms, 0 ticks over 5 ms.** On the wall, tile 8 had no re-seed and no clock jump in 9 minutes, 1 missed tick in 36,000. The replay also found the drift estimate problem fixed in ST-01. **Verdict: PASS.**

**Limits.** No stall happened during the 9-minute wall run (longest beacon delay 338 ms), so the stall case rests on the replay and the host test "24 Nov WiFi stall".

## Files

| File | What it is |
|---|---|
| `ts_tile7.log, ts_tile8.log, ts_tile9.log` | every beacon each tile received (19 min) |
| `ts_replay.log` | the replay, old and new rules |
| `long_monitor.log, long_pico.log, long_tile8.log` | the 9-minute wall run on the fixed firmware, and the Pico restart at its end (ST-06) |
| `tools/ts_replay.py, tools/tile_capture.py` | the replay and the serial logger |
