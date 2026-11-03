/**
 * @file    test_playback.h
 * @brief   Host tests of playback_control (T-B4-04).
 *
 * Owner: Ahsanul (Buddy 4). Written 29 Oct 2026 in Ben's (B2) session.
 */
#ifndef TEST_PLAYBACK_H
#define TEST_PLAYBACK_H

#include <stdbool.h>

bool test_playback_start_once (void);
bool test_playback_wrong_session (void);
bool test_playback_frame_index (void);
bool test_playback_stop_from_paused (void);
bool test_playback_resume_frame (void);
bool test_playback_holdover (void);

#endif /* TEST_PLAYBACK_H */

/*** end of file ***/
