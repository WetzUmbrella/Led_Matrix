# ST-04: Rounds back to back

**Requirements.** NFR8 (10 rounds without a restart)

**Set-up.** 3x3 wall, the demo set-up (`pc/web.py --layout 3x3 --policy continue`), game mode.

**Procedure.** Play rounds one after another and watch the Pico's message counter (`sent`) and boot lines.

**Pass rule.** 10 rounds in a row without a Pico restart.

**Result.** 13 rounds (8 easy, 5 hard), 432 moles shown. The Pico's `sent` rose from 16,401 to 28,001 with no reset and no boot line. **Verdict: PASS.**

**Files.** The logs of this run are in [`../ST-14/`](../ST-14/) (the same rehearsal session).
