/**
 * @file    test_coord.h
 * @brief   Host tests of coordinator_loop (T-B2-02 to T-B2-05).
 *
 * Owner: Benjamin (Buddy 2). Implemented by Ahsanul (B4) for Benjamin.
 */
#ifndef TEST_COORD_H
#define TEST_COORD_H

#include <stdbool.h>

bool test_coord_beacon_schedule (void);
bool test_coord_beacon_stall (void);
bool test_coord_nine_nodes (void);
bool test_coord_heartbeat_loss (void);
bool test_coord_dispatch_guard (void);
bool test_coord_stale_rejected (void);
bool test_coord_start_gate (void);
bool test_coord_crc_mismatch (void);
bool test_coord_session_validation (void);
bool test_coord_session_while_busy (void);
bool test_coord_status (void);
bool test_coord_start_copies (void);
bool test_coord_pause_resume_stop (void);
bool test_coord_pause_policy (void);
bool test_coord_continue_rejoin (void);

#endif /* TEST_COORD_H */

/*** end of file ***/
