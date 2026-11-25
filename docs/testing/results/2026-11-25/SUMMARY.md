# Host test run

- Date: 2026-11-25 15:34:35 +08
- Commit: `f3022be` (0 tracked files changed and not committed)
- Host: Linux 7.2.8-9-omarchy x86_64; gcc (GCC) 16.2.1 20260810; Python 3.14.8

| Suite | Command | Exit | Result |
|---|---|---|---|
| common_proto | `make -C common/proto/test run` | 0 | Pass = 8 of 8 |
| common_proto_asan | `make -C common/proto/test asan` | 0 | Pass = 8 of 8 |
| panel_sync | `make -C panel_sync/test run` | 0 | Pass = 4 of 4; Fault word: Pass = 6 of 6; Playback: Pass = 7 of 7; Telemetry: Pass = 5 of 5; Asset: Pass = 6 of 6; Coordinator lock: Pass = 4 of 4; Display: Pass = 9 of 9; Tap: Pass = 15 of 15 |
| panel_sync_asan | `make -C panel_sync/test asan` | 0 | Pass = 4 of 4; Fault word: Pass = 6 of 6; Playback: Pass = 7 of 7; Telemetry: Pass = 5 of 5; Asset: Pass = 6 of 6; Coordinator lock: Pass = 4 of 4; Display: Pass = 9 of 9; Tap: Pass = 15 of 15 |
| ledwall_sync | `make -C ledwall_sync/test run` | 0 | Coordinator: Pass = 43 of 43 |
| ledwall_sync_asan | `make -C ledwall_sync/test asan` | 0 | Coordinator: Pass = 43 of 43 |
| tools_pico_tap | `make -C tools/pico_tap/test run` | 0 | tap_ring: Pass = 4 of 4 |
| tools_pico_tap_asan | `make -C tools/pico_tap/test asan` | 0 | tap_ring: Pass = 4 of 4 |
| pytest | `.venv/bin/python -m pytest -v tools/tests pc/tests` | 0 | 518 passed, 1 skipped in 20.89s |

Every case with its verdict: [CASES.md](CASES.md).

All suites passed.
