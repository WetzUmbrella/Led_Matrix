# Step 2

Started 2026-11-05T17:30:48Z.
Quota before step: mode BRIDGE_OK, used 136, limit 1000, remaining 864.

## Report

**Build step 2's code is committed as `045b8d2`, but the firmware has never been compiled or run, and `barr_check` and `clang-format` couldn't run here.** The host tests pass. `quota.txt` said `MODE=BRIDGE_OK` (136 of 1,000 used), so there was no switch to Claude only.

## Check first on a tile
- **Interrupt pin:** INT1 on GPIO10 is from my memory of the board, and web search was blocked. If it's wrong, no `TAP` lines print; orientation should keep working. Check it against the schematic first.
- **Tap registers:** the tap register values are also from memory of the datasheet, so check them before tuning. They're marked PROVISIONAL in `orientation.h`, and test K6 only pins the bytes I chose, not that they're correct.
- **Latency:** the tap windows take about 70 ms before the interrupt, which uses most of the 100 ms tap-to-flash target.

## Requirements
| Requirement | Status |
|---|---|
| QMI8658 tap on an interrupt | Code done, never run on a board |
| Tune thresholds on the cardboard board | Not done: hardware, excluded as you asked |
| Diff read before tests | Met: the delegate changed only its 4 spec files, none of them build files or scripts |
| Build files done by me | Met: `Makefile`, `CMakeLists.txt`, `barr_scope.txt` and `test_main.c` |
| Commit authored by you only, not pushed | Met: no trailers, `.github/` untouched, tree clean |
| Only `:free` models | Met: one bridge run on `nemotron-3.5-super-vl:free`, reported cost 0 |

## Test results
- **Tile host tests:** `make run` and `asan` both pass, with `Tap: Pass = 6 of 6` and `Display: Pass = 9 of 9`.
- **Python:** pytest shows 264 passed, 1 skipped. I ran this before the final style fixes, which only touched C files.
- **Not run by me:** the other three `make` suites, because nothing they build changed. `overnight.sh` runs them.
- **Not compiled:** `orientation.c`, because `gcc` and `idf.py` were blocked. Build it with `idf.py build` before flashing.
- **Style checks:** I checked by hand against the rules in `barr_check.py` and fixed four findings that would have stopped the overnight run. `clang-format` could still flag layout, so run this:
  ```
  clang-format --dry-run --Werror panel_sync/main/tap_detect.c panel_sync/main/tap_detect.h panel_sync/main/orientation.c panel_sync/main/orientation.h panel_sync/main/orient_filter.h panel_sync/test/test_tap_detect.c panel_sync/test/test_tap_detect.h panel_sync/test/test_main.c panel_sync/test/test_display.c
  ```

## Decisions (pre-approved, logged here)
1. **Range ±8 g instead of ±2 g:** a tap would clip at 2 g, and every tile would report the same peak. The 0.5 g orientation threshold therefore goes from 8,192 to 2,048 LSB, and tests O1 to O3 and D5 now use the new scale.
2. **Sampling at 100 Hz:** this is the fastest whole-tick rate at `CONFIG_FREERTOS_HZ=100`. Every second sample goes to `orient_filter`, so orientation stays at 50 Hz.
3. **Peak from those samples, not the IMU FIFO:** this is simpler, but 100 Hz can miss the true peak of a short tap. If dedupe picks the wrong tile on the board, switch to reading the FIFO.
4. **Interrupt on core 0:** it only records the time, and it's installed from `app_main`, so it shouldn't delay the display tick on core 1.
5. **Failures don't stop orientation:** if the tap set-up fails, it's logged and orientation carries on.
6. **Tap queue:** `orientation_tap_take()` holds the last 4 taps for step 3, in local time rather than the Pico's clock. The queue has no consumer yet.

Each interrupt prints a `TAP result=` line for tuning; the procedure is in `docs/orientation_DEFENCE.md`, section 7. I didn't add the GPIO4 tap-latency pulse from the plan's test section.

## Other things to know
- **Review:** the delegate's result was over the size that normally goes to the reviewer. I read all of it and made the fixes myself instead.
- **Bridge paths:** my narrower `writablePaths` weren't applied; the run reported the config's wider paths. The delegate stayed in scope anyway, but only the check after the run enforces this.
- **Skill and memory files:** reading `LEARNINGS.md` and `runs.csv` was denied, so I applied no past lessons and logged nothing for this run. A memory note about these headless limits was also denied.

**Tiers:** Tier 1: 1 call, Tier 3: 0, reviewer: 0, plus my own driver and wiring edits.

**OpenRouter:** the bridge reported $0, but I didn't run the `curl` check because it was blocked in step 1, so spend isn't independently verified here; the `overnight.sh` check covers it. 864 free requests were left at the start, less what this run used.

## Checks

Claude exit code 0. Claude subagent calls 0. Bridge dispatches 1.
```
PASS make -C common/proto/test run
PASS make -C common/proto/test asan
PASS make -C panel_sync/test run
PASS make -C panel_sync/test asan
PASS make -C ledwall_sync/test run
PASS make -C ledwall_sync/test asan
PASS make -C tools/pico_tap/test run
PASS make -C tools/pico_tap/test asan
FAIL tools/barr_check.py
pytest: 264 passed, 1 skipped in 12.93s
```
**STOPPED: tests fail**
