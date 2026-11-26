# SE15 Distributed LED Matrix Video Wall: test report

Final submission, 25 Nov 2026, branch `master`. Host results below are from commit `f3022be`. Every host test passes (C and Python, also under AddressSanitizer and UndefinedBehaviorSanitizer), and the system tests on the 3x3 wall pass except one open failure and one partial result, which are listed with their cause. Three failures found on the wall were fixed and re-tested; they stay in the table.

| Level | What runs | Cases | Result | Where |
|---|---|---|---|---|
| Unit and integration, C | 4 host suites (tile, Pico, protocol, tap logger), plain and sanitized | 117 | 117 pass | [results/2026-11-25](results/2026-11-25/SUMMARY.md) |
| Unit and integration, Python | pytest: website, GIF build, upload, protocol, tools | 519 | 518 pass, 1 skipped | [results/2026-11-25](results/2026-11-25/SUMMARY.md) |
| System, on the wall | 9 ESP32-S3 tiles, 1 Pico W, the website, router `MATRIX` | 14 | 12 pass, 1 fail, 1 partly | [section 4](#4-system-tests-on-the-wall) |

Every case with its verdict: [results/2026-11-25/CASES.md](results/2026-11-25/CASES.md). Which tests cover which requirement (FR1 to FR21, NFR1 to NFR16): [TRACEABILITY.md](TRACEABILITY.md); run one requirement's tests with `python3 tools/traceability.py FR14`.

## 1. How to run the tests

From the repository root (Linux or WSL, gcc, make, Python 3 with `pytest` and `pillow`, as in CI):

```
./tools/run_all_tests.sh
```

It runs every suite, writes each full log into `docs/testing/results/<date>/`, writes `SUMMARY.md` (command, exit code, pass count per suite) and `CASES.md` (every case and its verdict, `tools/test_cases.py`), and exits non-zero if anything failed. CI (`.github/workflows/build.yml`) runs the same suites, plus `clang-format` and the Barr-C checker, and builds both firmwares on every push.

## 2. Test levels

| Level | Purpose | How |
|---|---|---|
| Unit | One module against its contract, including limits and bad input | C suites compile the module with a test `main` on the PC, with fakes for the clock, network and hardware; pytest for the Python modules |
| Integration | Modules and boards agree with each other | C and Python must produce the same bytes for every message (`pc/tests/test_cross_lang.py`); the Pico coordinator runs against 9 simulated tiles (`ledwall_sync/test`, cases C3 to C38); the website runs against a fake Pico link (`pc/tests/test_website_backend.py`) |
| System | The requirements, on the real wall | Bench cards with a stated input and a pass rule fixed before the run; each test's record and raw logs are in `evidence/ST-xx/` |

A C case prints its name, which states the input and the expected output ("A2: 600 frames = 57 chunks, the last 256 B"), then the values it saw, then `PASS` or `FAIL`. Example from `results/2026-11-25/panel_sync.log`:

```
Test 1: slew int8_t round trip and limit at -100/+100
    request -100 -> int8_t -100
    request +101 -> int8_t +100
    through ts_tick: error +101 -> slews +100, +1; error -101 -> slews -100, -1
  PASS
```

## 3. Unit and integration results (25 Nov 2026)

| Suite | Modules under test | Cases | Plain | ASan and UBSan |
|---|---|---|---|---|
| `common/proto/test` | bit packing and every message encoder and decoder (C) | 8 | 8 pass | 8 pass |
| `panel_sync/test` | tile: time sync (incl. seqlock and drift limit), fault word, playback, telemetry book, asset receive, coordinator lock, display and orientation, tap detector, game tile | 62 (56 counted + 6 supplementary) | 62 pass | 62 pass |
| `ledwall_sync/test` | Pico: beacons, registry, heartbeats, session, failure policies, uploads readiness, game rules, whole-wall turn | 43 | 43 pass | 43 pass |
| `tools/pico_tap/test` | tap logger edge ring | 4 | 4 pass | 4 pass |
| pytest (21 files) | website and its API, GIF to tile assets, uploader, Python codec, telemetry, camera skew tools, Barr-C checker, the test case lister | 519 | 518 pass, 1 skipped | not applicable |

The skipped test (`pc/tests/test_skew_analyse.py`, the camera skew plot) needs `matplotlib`, which is not installed on the bench laptop or in CI (`pytest.importorskip`).

One flaky test was found and fixed on 25 Nov: CI drew the session ids 7, 54, 7 and `test_every_layout_after_the_first_goes_out_in_a_fresh_session` failed, because it required three different ids while the website only has to avoid the active one (about 1 run in 254). The test now checks that each publish differs from the one before (`f3022be`).

Selected cases by requirement (full list in `CASES.md`):

| Requirement | Case | Input | Expected | Result |
|---|---|---|---|---|
| Minimum-bit packing (CR1) | proto "bits: put/get round trip" | every width 1 to 64 at offsets 0 to 7 | the value read back equals the value written | PASS |
| CR1, same bytes in C and Python | `test_cross_lang.py` | every message at its boundary values | identical bytes from both codecs | PASS |
| Safe shared fault mask (CR1) | panel_sync F5 | two setter threads and a reporter | 0 fault bits lost | PASS |
| Safe shared time sync (CR1) | panel_sync Test 4 (d) | 200,000 random writer/reader interleavings | 0 torn reads (fresh or last good only) | PASS |
| Asset upload | panel_sync A2, A4 | 600 frames; chunks in any order | 57 chunks, last 256 B; complete after all 57 | PASS |
| Session safety | ledwall_sync C6 | 100 messages with a wrong session or old seq | 0 accepted | PASS |
| Heartbeat loss | ledwall_sync C4 | a tile goes silent | loss logged 2.5 to 2.6 s later, 10 of 10 | PASS |
| Continue policy | ledwall_sync C14 | tile 2 lost during playback, back with a valid asset | tile 1 keeps playing; tile 2 gets its own RESUME_AT | PASS |
| Tap detection | panel_sync K1 | 199 mg, then 200 mg | no tap, then a tap timed at the spike | PASS |
| One slap, one tap | panel_sync K3 | a slap ringing for 480 ms | counted once | PASS |
| Failure policy on the website | `test_website_backend.py::test_policy_option_reaches_the_session_config` | `--policy continue` | SESSION_CONFIG carries the continue code | PASS |

## 4. System tests on the wall

Pass rules were fixed before each run. Failures are kept in the table with what was done about them. Each test's folder in [`evidence/`](evidence/) holds its record (set-up, procedure, pass rule, result) and the raw logs and analysis scripts behind it.

| ID | Requirement | Input or procedure | Expected | Actual | Verdict | Record |
|---|---|---|---|---|---|---|
| ST-01 | NFR1 video sync between tiles | film pairs of tiles at 240 fps during playback, decode each tile's frame steps | every real step under 5 ms | tiles 7 and 8: 7 steps of 5.0 to 8.6 ms (drift estimate). After the drift-limit fix `cbe7763`: every pair of tiles 4 to 9 under 5 ms, medians -0.40 to +0.25 ms | FAIL, fixed, then **PASS** | [evidence/ST-01](evidence/ST-01/) |
| ST-02 | NFR3 tap to hit feedback | one-tile round on tile 5, 39 taps; Pico logs each tap's arrival, 240 fps video of the squash | under 300 ms | median about 171 ms, worst about 196 ms | **PASS** | [evidence/ST-02](evidence/ST-02/) |
| ST-03 | FR14, NFR4 scoring | 9-tile round, hard level, one tapper | every mole hit counted once, no echo judged as a tap | before the echo fix: 20 hits and 38 wrong in one round. After `GAME_COLLECT_MS` 150 ms and 8 remembered groups: 34 of 34, 0 wrong | FAIL, fixed, then **PASS** | [evidence/ST-03](evidence/ST-03/) |
| ST-04 | NFR8 stability | rounds back to back on 3x3 | 10 rounds without a Pico restart | 13 rounds (8 easy, 5 hard), Pico counters never reset | **PASS** | [evidence/ST-04](evidence/ST-04/) |
| ST-05 | NFR8 Pico under tap load | 6 min of TAP bursts (`tap_burst.py`, 8 tiles, 0.7 s apart) | no freeze for 6 min | run 1 (router `MATRIX`, firmware not confirmed at head, busy spot): froze after 88 s. Pico reflashed at head, quieter spot: runs 2 and 4, 514 and 511 bursts, no freeze. The other bench router froze in about 2.5 min (19 Nov) | FAIL, changed, then **PASS** on the demo setup | [evidence/ST-05](evidence/ST-05/) |
| ST-06 | FR21, NFR8 Pico restart recovery | `picotool reboot -f` during playback | wall back without anyone touching it | all 9 re-uploaded, READY 27 s after the restart | **PASS** | [evidence/ST-06](evidence/ST-06/) |
| ST-07 | FR8 time sync on a WiFi stall | 9 min playback on all tiles at `630c390` | no clock jump, no re-seed | tile 8: no re-seed, no jump; replay of the 0.65 s stall: skew 665 ms before the fix, 1.26 ms after | **PASS** | [evidence/ST-07](evidence/ST-07/) |
| ST-08 | whole-wall rotation | 10 board turns (cw, ccw, 180), then 10 single-tile turns | every board turn detected with the right turn; no single-tile turn moves the wall | 10 of 10 detected within 1.8 s, playing again within 7.9 s; 0 of 10 single-tile turns moved the wall | **PASS** | [evidence/ST-08](evidence/ST-08/) |
| ST-09 | FR5, NFR6 upload of the packed asset | website uploads to 9 tiles at once | every tile ready, chunks checked by CRC32 | about 2 s per tile, 60 of 60 chunks ACKed, 0 resent | **PASS** | [evidence/ST-09](evidence/ST-09/) |
| ST-10 | NFR7 setup time | website restart; then Pico and tiles power-cycled | READY within 3 min | website restart: 3 s. Cold start: each tile ready 2 to 3 s after it joins, wall READY 2 s after the last tile | **PASS** | [evidence/ST-10](evidence/ST-10/) |
| ST-11 | FR20 continue policy | video playing; tile 9 unplugged 3 s, then 10 s | the others keep playing; tile 9 rejoins in step | wall never left playback; tile 9 back 7 s and 10 s after it was marked lost, in step by eye | **PASS** | [evidence/ST-11](evidence/ST-11/) |
| ST-12 | pause policy | video playing; tile 9 unplugged 3 s | wall pauses, returns to PAUSED when the tile is back | wall paused, then the Pico went to FAULT after 5 s, re-joined the router and was restarted by its watchdog (`wd_step=0x0a`, JOIN); about 30 s outage | **FAIL** (open; the demo uses the continue policy) | [evidence/ST-12](evidence/ST-12/) |
| ST-13 | FR20 tile drops out mid-round | game round; tile 9 unplugged at 14:57:55, back at 14:58:10 | the round goes on; the tile takes part again when back | round went on (30 of 30 moles); tile 9 left out of the mole choice while away; back, it showed nothing until the results | **PARTLY** (open: a rejoined tile is not sent the round state) | [evidence/ST-13](evidence/ST-13/) |
| ST-14 | demo run from the website | video play, pause, resume, stop; game mode; an easy round | no error, no manual fix | no issue seen | **PASS** | [evidence/ST-14](evidence/ST-14/) |

## 5. Open failures

| ID | Cause | Fix planned |
|---|---|---|
| ST-12 | `cl_scan_heartbeats()` moves RECOVERING to FAULT 5 s after a loss; in FAULT the loop only re-joins the router, though the link is up, and the join outlasts the 8 s watchdog | a tile loss should not re-join a router that is still up |
| ST-13 | the round command goes to each tile once, at the start; a tile that restarts never learns the round is running, but the Pico puts it back in the mole choice | resend the round state to a returning tile, or keep it out of the choice until the round ends |

## 6. Evidence gaps

What was planned or required but not measured, so the claim rests on weaker evidence than its pass criterion asks for:

| Requirement | What is missing | What there is instead |
|---|---|---|
| NFR1 | a 10-minute skew run of every pair of tiles; tiles 1 to 3 never filmed | 15 s camera runs of pairs among tiles 4 to 9 (ST-01); the time-sync replay of 19 min (ST-07) |
| NFR4 | the 50-tap cards for detection rate and false taps on every tile | 34 of 34 hits with one player (ST-03); soft taps of 370 to 1,348 mg all detected on 19 Nov (8 taps); host tests K1 to K8 |
| FR11 | the abort time (500 ms) was never timed | abort used on the wall; host tests C25 |
| FR16 | the greyscale photo check that hit, wrong and miss differ by shape | the shapes differ by design (host tests G1, G4) |
| FR18 | the 500 ms panel and 3 s offline display times were never timed | STATUS goes out every 250 ms (C10); heartbeat loss in 2.5 to 2.6 s (C4) |
| NFR9 | beacon lateness on the hardware for the final build | host tests C1, C2, C5; the October build was measured |
| NFR13 | the planned hardware stress tests (fault word set a million times from both cores; torn-read stress on the clock) | host tests F5 and Test 4 (200,000 interleavings), every C suite under ASan and UBSan |
| Change requirement | on-air packet captures before and after; free heap at the "before" tag | message sizes from the encoder ([`../tribunal1/change_requirement/message_sizes.md`](../tribunal1/change_requirement/message_sizes.md)); heap on the final build, 2 boots |
| Several players | how often simultaneous players' taps are misjudged | 121 wrong against 412 hits in 13 rounds with several players (ST-14); 0 wrong with one player (ST-03) |
