/**
 * @file    test_game_tile.h
 * @brief   Host tests of the tile's whack-a-mole handling (game_tile.c).
 *
 * Owner: Cheston (Buddy 3) and Jia Jun (Buddy 5). Written for the
 * whack-a-mole re-plan, build step 7.
 */
#ifndef TEST_GAME_TILE_H
#define TEST_GAME_TILE_H

#include <stdbool.h>

/**
 * @brief  G1: the mole rises, stays and sinks; hit squash and burst; the
 *         red X on a miss.
 * @return true when every check passed.
 */
bool test_game_tile_mole_and_flash (void);

/**
 * @brief  G2: only messages for this tile are ACKed; a repeat (same seq) is
 *         ACKed but not applied.
 * @return true when every check passed.
 */
bool test_game_tile_acks (void);

/**
 * @brief  G3: ARM, the start sprite, START, END, the tap test, and the
 *         GAME_CONTROL order.
 * @return true when every check passed.
 */
bool test_game_tile_round (void);

/**
 * @brief  G4: the picture list: timed entries, loop, clears, rotation.
 * @return true when every check passed.
 */
bool test_game_tile_pictures (void);

/**
 * @brief  G5: the game-mode GPIO4 pulse widths.
 * @return true when every check passed.
 */
bool test_game_tile_pulses (void);

/**
 * @brief  G6: TAP and ACK encodings decode back, out-of-range Node IDs are
 *         refused, and the state publishes and reads back whole.
 * @return true when every check passed.
 */
bool test_game_tile_codecs (void);

#endif /* TEST_GAME_TILE_H */

/*** end of file ***/
