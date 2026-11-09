/**
 * @file    test_ack_outbox.h
 * @brief   Host test of the acked outbox (C16).
 *
 * Owner: Benjamin (Buddy 2). Written for the whack-a-mole re-plan, build
 * step 7.
 */
#ifndef TEST_ACK_OUTBOX_H
#define TEST_ACK_OUTBOX_H

#include <stdbool.h>

/**
 * @brief  C16: a message is resent at its interval until its ACK, a wrong
 *         ACK frees nothing, a message is dropped at its deadline, one type
 *         can be cancelled, and a full outbox refuses.
 * @return true when every check passed.
 */
bool test_ack_outbox_limits (void);

#endif /* TEST_ACK_OUTBOX_H */

/*** end of file ***/
