# Review log

## Round 0: self-review of patches 09 to 15 (30 Oct 2026, Jia Jun's session)

**Not the independent review of continuation plan step 9.** The handoff prompt asks for two fresh reviewers (Tribunal fidelity; Barr-C and integration). This round is the implementing session checking its own work; it does not replace them. Tags as in the prompt: BLOCKING, SHOULD FIX, NIT.

### Found and fixed during the work (no longer open)

| Where | Finding | Fix |
|---|---|---|
| `tile_net.c` `tn_poll_pc()` (patch 11) | Upload replies went to the last sender on UDP 4214; a TELEMETRY_ACK from the dashboard's telemetry socket could redirect a CHUNK_ACK | only MANIFEST and ASSET_CHUNK set the reply address (patch 13) |
| `playback_control.c` `playback_control_state()` | read as a plain variable by `asset_store` and `tile_net` in other tasks, against coding standard interpretation 1 | atomic copy written with release, read with acquire (patch 15) |
| `tile_net.c` `tn_on_upload()` | T-X-08 case 3 (another session's manifest) was accepted | uploads for a session other than the beacons' are dropped and counted (patch 14) |
| `pc/` | no way to build or upload the OD-15 8-bit asset T-X-03 needs | `asset_builder.py --format 8bit`, `uploader.py --format 1` (patch 14) |
| `pc/tools/gen_week13_docs.py` | bit counts doubled the header | uses `body_bits()` alone; pytest added (patch 14) |
| `pc/tools/evidence_index.py` | procedure templates counted as evidence, so a readiness row turned Green with no run | documents that say "Nothing has run yet", and the index itself, are not evidence; pytest added (patch 14) |
| `tile_net.c` `tn_on_beacon()` (bench log) | beacons and commands accepted from any sender; two Picos on one SSID were mixed | coordinator lock: first sender kept, others dropped and counted (`foreign=`), switch after 2 s of silence (patch 17, PROVISIONAL; the bench must confirm with two Picos) |
| `.github/workflows/build.yml` | the new pytest needs Pillow, which CI did not install | `pillow==11.0.0` added (patch 14) |
| `panel_sync/test/test_telemetry_book.c` | T5 used a condition bit as an event, so it could not fail | event bits only; the end check now requires every event cleared (patch 09) |

### Open

| Tag | Where | Finding | Suggested fix |
|---|---|---|---|
| SHOULD FIX | `orientation.h` | QMI8658 register values come from the datasheet map and have never run | check `ORIENT imu ready` and the axes on a board before T-B5-06 |
| SHOULD FIX | `display_frame.h`, `orient_filter.h` | LED numbering, rotation direction and quadrant mapping are assumptions | settle in the T-B5-06 trials |
| SHOULD FIX | `asset_store.c` | the BUSY burst of T-B3-04 (1) relies on the Network Task draining the 8-slot Reply Queue at least every 20 ms; at the default 100 chunks/s that is about 2 replies per pass, but a faster `--rate` could drop BUSY replies (counted in `reply_drops`) | keep the T-B3-04 run at the default rate or raise the Reply Queue length |
| NIT | `pc/tools/skew_analyse.py`, `pc/tests/test_skew_analyse.py` | `pycodestyle` reports long lines and `l` names (existing files, not changed here) | owner's choice; CI does not run pycodestyle |
| NIT | `asset_store.c` | flash counters print only when no upload item arrives for 60 s | enough for the 10 min PLAYING check, where nothing arrives |

### Checks at the end of the round

`python3 tools/barr_check.py`: 64 files, 0 findings. `clang-format --dry-run --Werror`: exit 0. `make -C panel_sync/test`: timesync 4 of 4, fault word 5 of 5, playback 6 of 6, telemetry 5 of 5, asset 5 of 5, display 6 of 6. `make -C common/proto/test`: 7 of 7. `make -C ledwall_sync/test`: 14 of 14. `pytest -q tools/tests pc/tests`: 223 passed. ESP-IDF v5.3: the shipped build, `-DTN_RX_HOOK=2`, and the `FAULT_STRESS`, `TS_STRESS`, `DE_DUMP_FRAMES`, `DE_PROFILE`, `DE_TEST_SLOW_PREP`, `DE_TEST_FLASH_ERASE` and `TN_TEST_INJECT_FAULTS` builds all complete with 0 warnings; IRAM 16,383 of 16,384 B. Pico SDK 2.3.1 build complete. Nothing has run on a board since patch 09.
