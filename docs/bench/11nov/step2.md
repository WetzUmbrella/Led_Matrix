# Step 2 bench, 11 Nov 2026: software tap detection

Branch `feature/whack-a-mole` @ `815a185` plus the settle fix. Coordinator Pico `…E6613008E344BF33` (172.20.10.2), tiles 4 and 5 on sponge, phone hotspot.

| Check | Result | Notes |
|---|---|---|
| Build and gate | PASS | Tile tests K1 to K8 (new detector) pass with ASan; barr_check 0 findings; pytest 423 passed; firmware builds with no warnings |
| First idle run (25 s) | FAIL: false taps | Two at every boot (27 to 46 g: the baseline seeded from samples before the IMU settled), then 9 and 12 small ones (200 to 821 mg), several at the same moment on both tiles: desk vibration (the operator was not warned to keep still). Fix: the detector ignores 200 samples (200 ms) after each IMU set-up (`OR_SETTLE_SAMPLES`). `step2_idle_node*.log` |
| Quiet run (32 s, desk untouched) | **PASS: 0 taps on both tiles** | `step2_quiet_node*.log` |
| Display with 1 kHz sampling | **PASS: missed=0** | DE ticks 60 per second, prep_max about 150 us |
| Round 10 | **PASS, partly recorded** | Score 33, never dropped. The logger stopped after the first 14 s of the round (2 min was too short with the upload). In those 14 s: node 4 detected 7 taps and node 5 8; the Pico got all 15 and scored 15 hits; no echo crossed the threshold on the other tile. TAP arrival minus tap time median 25 ms (15 to 59), was 30 ms in step 1, including the new 10 ms peak window. Tap peaks 4.3 to 22.9 g: no soft taps in this round. `round10_*.log` |

Correction to step 1: the tap to flash estimate of 70 to 90 ms added the 30 ms group window to the arrival time, but the window starts at the tap. Verdict about 30 ms after the touch, about 10 ms to the tile, then the next 60 fps frame: about 45 to 55 ms. To be measured with the GPIO4 pulses (DS-13).

## Still to check

- Wrong taps (tap the empty tile: red flash, -1): never counted in round 10.
- Soft taps near the 200 mg threshold.
- A full-length recording of a round (logger for 4 minutes).
