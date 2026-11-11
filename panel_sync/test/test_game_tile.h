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
 * @brief  The mole is a solid fill from show to hide in the Pico's clock; a
 *         FEEDBACK flashes green (a hit ends the mole) or red (the mole stays)
 *         on the tile's own clock.
 * @return true when every check passed.
 */
bool test_game_tile_mole_and_flash (void);

/**
 * @brief  Only messages for this tile are ACKed; a repeat (same seq) is ACKed
 *         but not applied; an older seq is neither applied nor ACKed twice.
 * @return true when every check passed.
 */
bool test_game_tile_acks (void);

/**
 * @brief  START clears the score, WALL_PICTURE is drawn in the tile's rotation,
 *         END keeps the score on the tile.
 * @return true when every check passed.
 */
bool test_game_tile_score (void);

/**
 * @brief  TAP and ACK encodings decode back, out-of-range Node IDs are
 *         refused, and the state publishes and reads back whole.
 * @return true when every check passed.
 */
bool test_game_tile_codecs (void);

#endif /* TEST_GAME_TILE_H */

/*** end of file ***/
