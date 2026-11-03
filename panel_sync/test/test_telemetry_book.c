/**
 * @file    test_telemetry_book.c
 * @brief   Host tests of telemetry_book and the VC-4 send/ACK rule (T-B3-06),
 *          run by test_main.c.
 *
 * Implemented by Ahsanul (B4) for Cheston, task T-B3-06. Build and run:
 * make -C panel_sync/test. T5 drives telemetry_book and fault_word the way
 * tn_telemetry_send() and tn_on_telemetry_ack() in tile_net.c do; the radio
 * and the PC are simulated.
 */
#include "test_telemetry_book.h"

#include <stddef.h>
#include <stdint.h>

#include "test_main.h"
#include "../main/fault_word.h"
#include "../main/telemetry_book.h"

/** Sends in T5 (plan choice: 20 receiver cycles of 3). */
#define T5_SENDS 60u

/** The receiver drops every T5_DROP_EVERY-th packet (VC-4). */
#define T5_DROP_EVERY 3u

/** Event bits injected in T5, one per send in turn. */
static uint32_t const g_t5_events[] = {
    FAULT_BIT_CRC_FAIL,
    FAULT_BIT_QUEUE_OVERFLOW,
    FAULT_BIT_MISSED_DEADLINE,
    FAULT_BIT_SYNC_STALE,
};

/** Number of T5 event bits (4): a four-entry array count fits uint32_t. */
#define T5_EVENT_COUNT                                                         \
    ((uint32_t)(sizeof(g_t5_events) / sizeof(g_t5_events[0]))) /* Fits. */

static void word_reset (void);

/**
 * @brief  Test T1: ACK of a recorded seq.
 * @return true when every check passed.
 */
bool test_telemetry_book_ack_match (void)
{
    bool             b_ok = true;
    telemetry_book_t book;
    uint32_t         bits = 0u;

    telemetry_book_init(&book);
    telemetry_book_record(&book, 10u, UINT32_C(0x11));
    telemetry_book_record(&book, 11u, UINT32_C(0x22));

    b_ok = (test_main_check(telemetry_book_ack(&book, 11u, &bits), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(UINT32_C(0x22) == bits, __LINE__)) && b_ok;
    b_ok = (test_main_check(telemetry_book_ack(&book, 10u, &bits), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(UINT32_C(0x11) == bits, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test T2: duplicate and unknown ACKs.
 * @return true when every check passed.
 */
bool test_telemetry_book_repeat (void)
{
    bool             b_ok = true;
    telemetry_book_t book;
    uint32_t         bits = UINT32_C(0xABCD);

    telemetry_book_init(&book);
    b_ok = (test_main_check(!telemetry_book_ack(&book, 0u, &bits), __LINE__)) &&
           b_ok;

    telemetry_book_record(&book, 5u, UINT32_C(0x3));
    b_ok = (test_main_check(telemetry_book_ack(&book, 5u, &bits), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(!telemetry_book_ack(&book, 5u, &bits), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(!telemetry_book_ack(&book, 6u, &bits), __LINE__)) &&
           b_ok;
    /* A false result leaves the output untouched. */
    b_ok = (test_main_check(UINT32_C(0x3) == bits, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test T3: ring expiry.
 * @return true when every check passed.
 */
bool test_telemetry_book_expiry (void)
{
    bool             b_ok = true;
    telemetry_book_t book;
    uint32_t         bits = 0u;

    telemetry_book_init(&book);

    for (uint32_t seq = 0u; seq <= TELEMETRY_BOOK_CAPACITY; seq++)
    {
        /* seq is at most TELEMETRY_BOOK_CAPACITY (4), so it fits. */
        telemetry_book_record(&book, (uint16_t)seq, seq + 1u);
    }

    b_ok = (test_main_check(!telemetry_book_ack(&book, 0u, &bits), __LINE__)) &&
           b_ok;

    for (uint32_t seq = 1u; seq <= TELEMETRY_BOOK_CAPACITY; seq++)
    {
        uint16_t const ack_of = (uint16_t)seq; /* seq is at most 4. */

        b_ok = (test_main_check(telemetry_book_ack(&book, ack_of, &bits),
                                __LINE__)) &&
               b_ok;
        b_ok = (test_main_check((seq + 1u) == bits, __LINE__)) && b_ok;
    }

    return b_ok;
}

/**
 * @brief  Test T4: seq wrap.
 * @return true when every check passed.
 */
bool test_telemetry_book_wrap (void)
{
    bool             b_ok = true;
    telemetry_book_t book;
    uint32_t         bits = 0u;

    telemetry_book_init(&book);
    telemetry_book_record(&book, UINT16_MAX, UINT32_C(0x1));
    telemetry_book_record(&book, 0u, UINT32_C(0x2));

    b_ok = (test_main_check(telemetry_book_ack(&book, 0u, &bits), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(UINT32_C(0x2) == bits, __LINE__)) && b_ok;
    b_ok = (test_main_check(telemetry_book_ack(&book, UINT16_MAX, &bits),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(UINT32_C(0x1) == bits, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test T5: VC-4 drop-every-third simulation.
 * @return true when every check passed.
 */
bool test_telemetry_book_vc4 (void)
{
    bool             b_ok = true;
    telemetry_book_t book;
    uint32_t         pending     = 0u; /* Injected, not yet received. */
    uint32_t         transitions = 0u;
    uint32_t         injected    = 0u;
    bool             b_ack_due   = false;
    uint16_t         ack_seq     = 0u;
    uint32_t const   condition   = FAULT_BIT_BEACON_LOSS;

    word_reset();
    telemetry_book_init(&book);
    fault_word_set(condition); /* Held for the whole run. */

    for (uint32_t send = 0u; send < (T5_SENDS + T5_DROP_EVERY); send++)
    {
        /* Inject a new event while the run lasts, only when its bit is clear,
         * so every injection is a separate report (no VC-4 merge). */
        uint32_t const event = g_t5_events[send % T5_EVENT_COUNT];

        if ((send < T5_SENDS) && (0u == (fault_word_snapshot() & event)))
        {
            fault_word_set(event);
            pending |= event;
            injected++;
        }

        /* tn_telemetry_send(): snapshot, record, send. */
        uint16_t const seq       = (uint16_t)send; /* send < 65,536. */
        uint32_t const sent_bits = fault_word_snapshot();

        telemetry_book_record(&book, seq, sent_bits);

        if (0u == (sent_bits & condition))
        {
            transitions++;
        }

        /* The ACK of the previous received packet arrives after this send. */
        if (b_ack_due)
        {
            uint32_t acked_bits = 0u;

            if (telemetry_book_ack(&book, ack_seq, &acked_bits))
            {
                fault_word_clear_events(acked_bits);
            }

            b_ack_due = false;
        }

        /* Receiver: every third packet is dropped and never ACKed. */
        if (0u != ((send + 1u) % T5_DROP_EVERY))
        {
            pending &= (~sent_bits);
            ack_seq   = seq;
            b_ack_due = true;
        }
    }

    uint32_t const min_injected = (T5_SENDS / T5_EVENT_COUNT);

    b_ok = (test_main_check(injected >= min_injected, __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == pending, __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == transitions, __LINE__)) && b_ok;
    /* After the last ACKs only the held condition bit is left: every event
     * was cleared, and only through an ACK. */
    b_ok =
        (test_main_check(condition == fault_word_snapshot(), __LINE__)) && b_ok;

    word_reset();

    return b_ok;
}

/**
 * @brief  Clear every bit so each test starts from a zero word.
 */
static void word_reset (void)
{
    fault_word_clear_events(FAULT_EVENT_MASK);
    fault_word_clear_condition(FAULT_CONDITION_MASK);
}

/*** end of file ***/
