# ST-05: Pico under tap load

**Requirements.** NFR8, FR13 (TAP resent until ACKed)

**Set-up.** 3x3 wall on the router `MATRIX`, fixed channel 11, game mode with the tap test on.

**Procedure.** `tools/tap_burst.py` sends bursts of TAPs as if 8 tiles were tapped, every 0.7 s, for 6 minutes, while the Pico's serial log runs.

**Pass rule.** No Pico freeze (no lost STATUS, no watchdog restart) for 6 minutes.

**Result.**

| Run | Set-up | Result |
|---|---|---|
| 1 | Pico firmware not confirmed current, busy WiFi spot | **froze after 88 s** |
| 2 | Pico reflashed, quieter spot | 514 bursts (about 4,100 TAPs) in 6 min, no freeze, `send_fail=0 tx_drop=0` |
| 4 | same, TAP numbers continued from run 2 | 511 bursts in 6 min, no freeze |

(Run 3 was invalid: it reused run 2's TAP numbers, so the Pico rightly dropped them as old.) **Verdict: FAIL, changed, then PASS** on the demo set-up. The 8 s hardware watchdog recovers a freeze in about 20 s if one happens.

## Files

| File | What it is |
|---|---|
| `tap_burst_run1.log, tap_burst_run2.log, tap_burst_run4.log` | the load generator's output |
| `pico_run2.log, pico_run4.log` | Pico serial logs of the passing runs |
| `tools/tap_burst.py` | the load generator |
