# ST-14: Whole demo from the website

**Requirements.** FR1 to FR7, FR10, FR11, FR15, FR16, FR18

**Set-up.** `pc/web.py --pico 192.168.1.154 --layout 3x3 --gif frog_ripple_64x64.gif --policy continue`.

**Procedure.** The demo run: video play, pause and resume twice, stop; game mode; an easy round; then ST-13 and ST-04 in the same session.

**Pass rule.** Every step done on the website, with no error and no manual fix.

**Result.** No issue. Tile 8 dropped out for 49 s during the video (a power bank) and the video kept playing under the continue policy. **Verdict: PASS.**

**Note on several players.** In the 13 rounds of ST-04, several people tapped at once: 121 wrong taps against 412 hits, 92 of them within 400 ms of a hit. It is not known whether these were echoes or real second taps. With one careful player (ST-03) the same firmware gave 0 wrong. The game is tuned for one player.

## Files

| File | What it is |
|---|---|
| `web_rehearsal.log` | website log |
| `rehearsal_state.log` | website state through the session |
| `pico_rehearsal.log` | Pico serial log |
