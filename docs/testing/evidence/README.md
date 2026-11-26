# Wall test evidence

One folder per system test on the wall. Each folder's `README.md` is the
test record: the requirement, set-up, procedure, the pass rule fixed before
the run, the result and its verdict. The logs and analysis scripts beside it
are the raw evidence. The summary table is section 4 of
[`../TEST_REPORT.md`](../TEST_REPORT.md).

| Test | What | Verdict |
|---|---|---|
| [ST-01](ST-01/) | video sync between tiles, 240 fps camera | FAIL, fixed, then PASS |
| [ST-02](ST-02/) | tap to hit feedback | PASS |
| [ST-03](ST-03/) | scoring with one tapper | FAIL, fixed, then PASS |
| [ST-04](ST-04/) | 13 rounds without a Pico restart | PASS |
| [ST-05](ST-05/) | Pico under tap load for 6 minutes | FAIL, changed, then PASS |
| [ST-06](ST-06/) | Pico restart during playback | PASS |
| [ST-07](ST-07/) | time sync through a WiFi stall | PASS |
| [ST-08](ST-08/) | whole-wall rotation | PASS |
| [ST-09](ST-09/) | upload of the packed asset to 9 tiles | PASS |
| [ST-10](ST-10/) | set-up time from power-on | PASS |
| [ST-11](ST-11/) | continue policy, tile unplugged during video | PASS |
| [ST-12](ST-12/) | pause policy, tile unplugged during video | FAIL (open) |
| [ST-13](ST-13/) | tile unplugged mid-round | PARTLY (open) |
| [ST-14](ST-14/) | whole demo run from the website | PASS |

The phone videos of ST-01 and ST-02 are not in the repository (about 1 GB);
the logs are their analysis.
