# ST-02: Tap to hit feedback

**Requirements.** NFR3 (under 300 ms in the build), FR13, FR14

**Set-up.** A one-tile session on tile 5 (`pc/web.py --layout 1x1 --map 5 --policy continue`), game mode, easy level. The other 8 tiles stayed online, so their echo TAPs reached the Pico as in a full wall.

**Procedure.** The delay is split into parts, each measured or bounded, because a front-on video cannot show the moment of contact:

1. Contact to the accelerometer peak: at most 10 ms (the detector's peak window).
2. Tap time to arrival at the Pico: the Pico logs each `GAME_TAP` with arrival minus tap time on the synced clock.
3. The collect wait: 150 ms from the first arrival (`GAME_COLLECT_MS`), then FEEDBACK.
4. FEEDBACK to the visible squash: a 240 fps video. After a hit the next mole is planned 200 ms later, so squash onset to next-mole onset (47 frames, 196 ms, on all 4 clean hits) puts the squash about 4 ms after FEEDBACK.

**Pass rule.** Every tap under 300 ms from contact to the squash.

**Result.** 39 taps (38 hits and the start). Tap time to Pico arrival: median 17.4 ms, max 32.5 ms. Tap to squash: **median about 171 ms, worst about 196 ms.** On the 24 Nov 9-tile round (ST-03) the worst first arrival was 103 ms, giving about 267 ms, still under 300 ms. **Verdict: PASS.**

**Limits.** The display part rests on 4 clean hits. A one-tile session sends fewer MOLE_ON and FEEDBACK messages than a 3x3 round.

## Files

| File | What it is |
|---|---|
| `pico_nr03_round.log` | Pico serial log of the round, with each tap's arrival delay |
| `web_nr03.log` | website log of the session |
