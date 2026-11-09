/**
 * @file    test_ack_outbox.c
 * @brief   Host test of ack_outbox (C16): the resend interval, the ACK
 *          match, the deadline, cancel by type and the full outbox.
 *
 * Owner: Benjamin (Buddy 2). Written for the whack-a-mole re-plan, build
 * step 7.
 */
#include "test_ack_outbox.h"

#include <stdint.h>

#include "ack_outbox.h"
#include "test_main.h"

/** Addressee, type and destination used by the checks. */
#define TST_NODE  3u
#define TST_TYPE  19u
#define TST_ADDR  UINT32_C(0xC0A80103)
#define TST_PORT  4210u
#define TST_RETRY UINT64_C(50000)

/**
 * @brief  C16: a message is resent at its interval until its ACK, a wrong
 *         ACK frees nothing, a message is dropped at its deadline, one type
 *         can be cancelled, and a full outbox refuses.
 * @return true when every check passed.
 */
bool test_ack_outbox_limits (void)
{
    static ack_outbox_t box;
    uint8_t const       msg[4] = {1u, 2u, 3u, 4u};
    uint32_t            index  = 0u;
    bool                b_ok   = true;

    ack_outbox_init(&box);

    b_ok = (test_main_check(ack_outbox_add(&box, TST_NODE, TST_TYPE, 100u,
                                           TST_ADDR, TST_PORT, msg, 4u,
                                           1000u, 1000u + 300000u),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(!ack_outbox_due(&box, 999u, &index), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(ack_outbox_due(&box, 1000u, &index), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(0u == index, __LINE__)) && b_ok;
    ack_outbox_sent(&box, index, 1000u, TST_RETRY);
    b_ok = (test_main_check(1000u + TST_RETRY == ack_outbox_next_due(&box),
                            __LINE__)) && b_ok;

    /* Resent every 50 ms: not before, then at, the interval. */
    b_ok = (test_main_check(!ack_outbox_due(&box, 1000u + TST_RETRY - 1u,
                                            &index), __LINE__)) && b_ok;
    b_ok = (test_main_check(ack_outbox_due(&box, 1000u + TST_RETRY, &index),
                            __LINE__)) && b_ok;
    ack_outbox_sent(&box, index, 1000u + TST_RETRY, TST_RETRY);
    b_ok = (test_main_check(2u == box.slots[index].sends, __LINE__)) && b_ok;

    /* An ACK from another node, or for another seq, frees nothing. */
    b_ok = (test_main_check(!ack_outbox_ack(&box, 2u, 100u), __LINE__)) && b_ok;
    b_ok = (test_main_check(!ack_outbox_ack(&box, TST_NODE, 101u), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(box.slots[index].b_busy, __LINE__)) && b_ok;

    /* The matching ACK ends it: nothing is due afterwards. */
    b_ok = (test_main_check(ack_outbox_ack(&box, TST_NODE, 100u), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(UINT64_MAX == ack_outbox_next_due(&box),
                            __LINE__)) && b_ok;

    /* A deadline drops the message even if it is never ACKed. */
    b_ok = (test_main_check(ack_outbox_add(&box, TST_NODE, TST_TYPE, 101u,
                                           TST_ADDR, TST_PORT, msg, 4u,
                                           2000u, 5000u),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(!ack_outbox_due(&box, 5000u, &index), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(1u == box.expired, __LINE__)) && b_ok;

    /* Cancelling a type drops only that type. */
    b_ok = (test_main_check(ack_outbox_add(&box, TST_NODE, TST_TYPE, 102u,
                                           TST_ADDR, TST_PORT, msg, 4u,
                                           6000u, 9000u),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(ack_outbox_add(&box, TST_NODE, 20u, 103u,
                                           TST_ADDR, TST_PORT, msg, 4u,
                                           6000u, 9000u),
                            __LINE__)) && b_ok;
    ack_outbox_cancel(&box, TST_TYPE);
    b_ok = (test_main_check(!ack_outbox_ack(&box, TST_NODE, 102u), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(ack_outbox_ack(&box, TST_NODE, 103u), __LINE__)) &&
           b_ok;

    /* A full outbox and a bad length are refused, and counted. */
    ack_outbox_init(&box);
    for (uint32_t fill = 0u; fill < ACK_OUTBOX_SLOTS; fill++)
    {
        b_ok = (test_main_check(ack_outbox_add(&box, TST_NODE, TST_TYPE,
                                               (uint16_t)fill, /* < 24 */
                                               TST_ADDR,
                                               TST_PORT, msg, 4u, 0u,
                                               UINT64_MAX),
                                __LINE__)) && b_ok;
    }
    b_ok = (test_main_check(!ack_outbox_add(&box, TST_NODE, TST_TYPE, 999u,
                                            TST_ADDR, TST_PORT, msg, 4u, 0u,
                                            UINT64_MAX),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(1u == box.full_drops, __LINE__)) && b_ok;
    b_ok = (test_main_check(!ack_outbox_add(&box, TST_NODE, TST_TYPE, 1000u,
                                            TST_ADDR, TST_PORT, msg, 0u, 0u,
                                            UINT64_MAX),
                            __LINE__)) && b_ok;

    return b_ok;
}

/*** end of file ***/
