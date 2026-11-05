# Step 1

Started 2026-11-05T17:24:41Z.
Quota before step: mode BRIDGE_OK, used 131, limit 1000, remaining 869.

## Report

**Build step 1's code part is done and committed as `8a875e7`, but `barr_check` and `clang-format` couldn't run in this session.** The remap in `display_frame.c` didn't need changing. The new tests did show that the wording in `display_frame.h` was wrong, so I corrected it. `quota.txt` said `MODE=BRIDGE_OK` (131 of 1,000 used), so there was no switch to Claude only.

## Finding to check on a tile
- **Wrong wording:** `display_frame.h` said code 90 means a tile turned clockwise. If the IMU axes are X right and Y up, +X reading up means the tile was turned **anticlockwise**.
- **Why the remap stays:** the classifier and remap agree under those axes, so the picture stays upright. Swapping 90 and 270 to match the old wording would have turned the picture upside down at both angles.
- **Still unproven:** no tile has confirmed those axes. D4 to D6 prove the code matches the geometry, not that the board is wired that way. If the tile test shows the picture wrong at 90 and 270, the axis assumption is what to fix.

## Requirements
| Requirement | Status |
|---|---|
| Rotation-mapping host test | Met: tests D4 to D6 added |
| Tile check at each angle, router, 5 tiles, `pico_tap` calibration | Not done: hardware, excluded as you asked |
| Delegate diff read before tests | Met: in scope, and no Makefile, `.sh` or `.github/` changes |
| Commit authored by you only | Met: `WetzUmbrella`, no trailers, not pushed |

## Test results
- **Host tests:** `make run` and `make asan` both pass, with `Display: Pass = 9 of 9`.
- **Python:** pytest shows 264 passed, 1 skipped.
- **Do the new tests catch a swap?** I temporarily swapped codes 90 and 270: D4, D5 and D6 failed while D2 still passed, so D2 alone could never have caught this. I reverted the swap and `git status` confirmed the file matched HEAD.
- **Not run:** `barr_check.py` and `clang-format` both needed approval this session can't grant. I checked the code by hand against their rules, so CI could still flag formatting. `overnight.sh` runs `barr_check` in its gate, but not `clang-format`, so run this yourself:
  ```
  clang-format --dry-run --Werror panel_sync/test/test_display.c panel_sync/test/test_display.h panel_sync/test/test_main.c panel_sync/main/display_frame.h
  ```

## Decisions (pre-approved, logged here)
1. **Remap left unchanged:** I corrected the wording instead, as explained above.
2. **Three tests:** D4 is a hand-worked corner table, D5 runs from gravity to picture for all 64 LEDs, and D6 follows one pixel through `display_frame_unpack()`.
3. **Test code partly rewritten by me:** the spec I gave the delegate broke repo rules (`x` and `y` too short, uncommented casts). That was my error, not the delegate's, so I rewrote those parts rather than retrying.
4. **Defence doc updated:** `docs/display_engine_DEFENCE.md` had the old test count and direction note. It's outside the three delegated files, so I edited it myself.

## Other things to know
- **Bridge:** the delegate tried `make` and the bash block stopped it, so that patch is holding. It also reports opencode 1.19.5, newer than the 1.18 line it was tested against, though all its routes are present.
- **Spend not verified:** the `curl` check was blocked, so I have no before-and-after figure. The one run was on a pinned `:free` model and reported $0.0000, and the `overnight.sh` check will catch any real spend.
- **Skill files:** reading `LEARNINGS.md` and `runs.csv` was denied, so I applied no past lessons and logged nothing for this run. Later runs will hit the same block, along with `barr_check`, `clang-format` and `git checkout`, unless you allow them.

**Tiers:** Tier 1: 1 call, Tier 3: 0, reviewer: 0, plus my own edits. **OpenRouter:** $0.0000 reported by the bridge, unverified as above. About 869 free requests were left at the start, less whatever this run used.

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
PASS tools/barr_check.py
pytest: 264 passed, 1 skipped in 13.05s
```
OpenRouter spend so far: $0.
Finished 2026-11-05T17:30:48Z with 1 commit(s).
