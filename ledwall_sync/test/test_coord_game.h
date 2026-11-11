/**
 * @file    test_coord_game.h
 * @brief   Host tests of the coordinator's whack-a-mole game (C20 to C22).
 *
 * Owner: Benjamin (Buddy 2). Written for the whack-a-mole re-plan, build
 * step 7.
 */
#ifndef TEST_COORD_GAME_H
#define TEST_COORD_GAME_H

#include <stdbool.h>

/**
 * @brief  C20: GAME_CONTROL START is refused in video mode; the mode switch
 *         is ACKed to the PC; START then starts the round (START to each
 *         tile, ACKed by the tiles); a repeated GAME_CONTROL with the same
 *         seq is ACKed again and starts nothing.
 * @return true when every check passed.
 */
bool test_coord_game_mode_and_start (void);

/**
 * @brief  C21: a full round on a 2 x 2 wall: MOLE_ON resent until ACKed and
 *         then not again, a tap on the mole is ACKed, relayed to the PC and
 *         answered with a green FEEDBACK, STATUS carries the live score, and
 *         the round end sends END and the score across the wall (8 lit
 *         pixels for a 1), then the Pico is READY again.
 * @return true when every check passed.
 */
bool test_coord_game_round (void);

/**
 * @brief  C22: a TAP outside a round is ACKed and relayed once (a repeat is
 *         ACKed but not relayed again); SESSION_CONFIG is refused during a
 *         round.
 * @return true when every check passed.
 */
bool test_coord_game_taps (void);

/**
 * @brief  C23: after a tile's REGISTER, its TAPs from a low seq are new, not
 *         stale (the tile restarted its TAP counter).
 * @return true when every check passed.
 */
bool test_coord_game_tap_restart (void);

/**
 * @brief  C24: ARM, the start sprite on the centre tile, its tap, the
 *         countdown on every tile, then the round at the armed level.
 * @return true when every check passed.
 */
bool test_coord_game_arm (void);

/**
 * @brief  C25: START_NOW, END in the countdown, and the 60 s arm timeout.
 * @return true when every check passed.
 */
bool test_coord_game_abort (void);

/**
 * @brief  C26: the tap test lights the tapped tile; refused while armed.
 * @return true when every check passed.
 */
bool test_coord_game_tap_test (void);

/**
 * @brief  C27: STATUS level, time left, wrong, missed, shown, reaction.
 * @return true when every check passed.
 */
bool test_coord_game_status (void);

#endif /* TEST_COORD_GAME_H */

/*** end of file ***/
