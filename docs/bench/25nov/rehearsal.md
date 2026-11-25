# Demo rehearsal, 25 Nov 2026 (3x3, continue policy)

Website `pc/web.py --pico 192.168.1.154 --layout 3x3 --gif frog_ripple_64x64.gif --policy continue` (log `web_rehearsal.log`), state log `rehearsal_state.log`, Pico serial `pico_rehearsal.log`, taps `pc/logs/taps_20261125_145007.csv`.

## Part 1: the demo run (14:53 to 14:58)

Video play, pause and resume twice, stop, game mode, an easy round. User: no issues seen. Tile 8 dropped for 49 s during the video (14:54:57 to 14:55:46); under the continue policy the video kept playing. Tile 4 dropped for 6 s between rounds (15:00:18). Both are taken as power.

## Part 2: E9, a tile drops out mid-round

Tile 9 unplugged about 14:57:55 during an easy round. The Pico marked it lost at 14:57:58.4 and took it out of the mole choice; the round ran on to the end (14:58:13.5, 30 of 30 moles). Tile 9 was back at 14:58:10.1 but showed no hole until the results, which it did show.

**Known limit (user's decision 25 Nov: leave it):** a tile that restarts mid-round never gets the round state again (the round command goes out once, at the start; open since 12 Nov), so `game_tile_active()` stays false and it draws nothing. `cl_game_sync_active()` puts it back in the mole choice when it returns, so a mole sent to it is invisible and counts as missed. Today none was sent in the 3 s left. Fix later: resend the round state to a returning tile, or keep it out of the choice until the round ends.

## Part 3: NR-06, rounds without a Pico restart

13 rounds finished (14:58 to 15:09; 8 easy, 5 hard), 432 moles shown. Pico `sent` rose from 16,401 to 28,001 with no reset and no boot line: **no restart. NR-06 met** (10 rounds in a row).

## Wrong taps (to check)

121 MISS feedbacks against 412 hits (2 to 21 wrong per round). 92 of the 121 came within 400 ms of a hit, 29 more than 400 ms from any hit. On 24 Nov, with one careful tapper, the same firmware gave 34 of 34 and 0 wrong. Unknown yet whether these were echoes or real second taps by the players.
