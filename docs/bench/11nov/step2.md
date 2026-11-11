# Step 2 bench, 11 Nov 2026: software tap detection

Branch `feature/whack-a-mole` @ `815a185` plus the settle fix. Coordinator Pico `…E6613008E344BF33` (172.20.10.2), tiles 4 and 5 on sponge, phone hotspot.

| Check | Result | Notes |
|---|---|---|
| Build and gate | PASS | Tile tests K1 to K8 (new detector) pass with ASan; barr_check 0 findings; pytest 423 passed; firmware builds with no warnings |
| First idle run (25 s) | FAIL: false taps | Two at every boot (27 to 46 g: the baseline seeded from samples before the IMU settled), then 9 and 12 small ones (200 to 821 mg), several at the same moment on both tiles: desk vibration (the operator was not warned to keep still). Fix: the detector ignores 200 samples (200 ms) after each IMU set-up (`OR_SETTLE_SAMPLES`). `step2_idle_node*.log` |
| Quiet run (32 s, desk untouched) | **PASS: 0 taps on both tiles** | `step2_quiet_node*.log` |
| Display with 1 kHz sampling | **PASS: missed=0** | DE ticks 60 per second, prep_max about 150 us |
| Round 10 | **PASS, partly recorded** | Score 33, never dropped. The logger stopped after the first 14 s of the round (2 min was too short with the upload). In those 14 s: node 4 detected 7 taps and node 5 8; the Pico got all 15 and scored 15 hits; no echo crossed the threshold on the other tile. TAP arrival minus tap time median 25 ms (15 to 59), was 30 ms in step 1, including the new 10 ms peak window. Tap peaks 4.3 to 22.9 g: no soft taps in this round. `round10_*.log` |
| Round 11 | Wrong taps work; **bug: one firm tap counted twice** | 31 moles, 35 TAPs, 18 hits, 10 misses. 4 were deliberate wrong taps (red, -1); 2 were timing (a second tap 270 ms after a hit; a tap 30 ms after the mole hid). 3 were one tap scored twice, a HIT on the tapped tile and a MISS on the other: the two TAPs were stamped 3 to 5 ms apart, but WiFi delivered them 61 and 75 ms late; the Pico judged a group 30 ms after its stamp, so the echo was judged alone, and the real tap, stamped before the closed group, formed a new one. The tiles' clocks agree to 1 to 3 ms, so it is not clock drift. `round11_*.log` |
| Fix (Pico, game_rules) | Groups wait for arrivals | A group is judged no sooner than GAME_COLLECT_MS (20 ms) after its first TAP arrives, as well as 30 ms after its stamp; the late-duplicate check looks both sides of a closed group. Host test C18c replays round 11. The coordinator passes each TAP's arrival time (`game_rules_tap_rx()`). |
| Round 12 | Not a test | The operator did not tap (the round started before they were ready). `round12_*.log` |
| Tap check (no game) | PASS | 3 firm taps per tile: 20.9 to 26.6 g on the tapped tile, 263 to 390 mg echo on the other: about 100 to 1 on sponge, upright against the box. `step2_tapcheck2_node*.log` (`step2_tapcheck_*` was an idle run: the operator did not tap). |
| Round 13 (grouping fix) | **PASS: no tap counted on both tiles** | 30 moles, 25 TAPs, 12 hits, 6 misses, none paired with a verdict on the other tile. 3 misses deliberate; one a tap before the first mole showed. **Two limits found:** (a) a 16.9 g slap rebounded with 652 mg 146 ms later after a short quiet spell and counted again: fixed with a 250 ms dead time before re-arming (`TAP_DETECT_DEAD_US`; test K4 replays it); (b) a soft tap gave 555 mg on node 4 and 644 mg on node 5, so "highest peak wins" picked node 5: with soft taps the echo can match the tap. No clean software fix at this sensitivity; raise with the professor (capacitive touch removes it). `round13_*.log` |

Correction to step 1: the tap to flash estimate of 70 to 90 ms added the 30 ms group window to the arrival time, but the window starts at the tap. Verdict about 30 ms after the touch, about 10 ms to the tile, then the next 60 fps frame: about 45 to 55 ms. To be measured with the GPIO4 pulses (DS-13).

## Still to check

- A round with the 250 ms dead time (firm slaps: one green, no second red).
- Soft taps: attribution between tiles is unreliable when the tap is near the echo's size (limit (b) above).
