/**
 * @file    ack_outbox.c
 * @brief   Acked unicast outbox (see ack_outbox.h).
 *
 * Owner: Benjamin (Buddy 2). Written for the whack-a-mole re-plan, build
 * step 7.
 */
#include "ack_outbox.h"

#include <string.h>

static bool ack_outbox_busy_at (ack_outbox_t const * p_box, uint32_t index);

/**
 * @brief  Empty the outbox and clear its counters.
 * @param  p_box  Outbox; must not be NULL.
 */
void ack_outbox_init (ack_outbox_t * p_box)
{
    (void)memset(p_box, 0, sizeof(*p_box)); /* Returns the outbox. */
}

/**
 * @brief  Queue a message; its first send is due at once.
 * @param  p_box        Outbox; must not be NULL.
 * @param  node_id      Addressee (1 to 9, or 0 for the PC).
 * @param  type         Message type code.
 * @param  seq          Header seq of the message.
 * @param  addr         Destination address.
 * @param  port         Destination port.
 * @param  p_buf        Encoded message.
 * @param  len          Bytes in p_buf, 1 to ACK_OUTBOX_MSG_MAX.
 * @param  now_us       Current time, microseconds.
 * @param  deadline_us  Drop the message at or after this time.
 * @return false when the message is refused.
 */
bool ack_outbox_add (ack_outbox_t * p_box, uint8_t node_id, uint8_t type,
                     uint16_t seq, uint32_t addr, uint16_t port,
                     uint8_t const * p_buf, uint32_t len, uint64_t now_us,
                     uint64_t deadline_us)
{
    bool b_added = false;

    if ((len >= 1u) && (len <= ACK_OUTBOX_MSG_MAX))
    {
        for (uint32_t index = 0u; (!b_added) && (index < ACK_OUTBOX_SLOTS);
             index++)
        {
            ack_outbox_entry_t * const p_entry = &p_box->slots[index];

            if (!p_entry->b_busy)
            {
                p_entry->b_busy      = true;
                p_entry->node_id     = node_id;
                p_entry->type        = type;
                p_entry->seq         = seq;
                p_entry->addr        = addr;
                p_entry->port        = port;
                p_entry->len         = (uint8_t)len; /* At most 24. */
                p_entry->next_us     = now_us;
                p_entry->deadline_us = deadline_us;
                p_entry->sends       = 0u;
                (void)memcpy(p_entry->buf, p_buf, len); /* Returns buf. */
                b_added = true;
            }
        }

        if (!b_added)
        {
            p_box->full_drops++;
        }
    }

    return b_added;
}

/**
 * @brief  An ACK arrived: free the message it names.
 * @param  p_box    Outbox; must not be NULL.
 * @param  node_id  Sender of the ACK (the addressee of the message).
 * @param  seq      acked_seq of the ACK.
 * @return true when a waiting message was freed.
 */
bool ack_outbox_ack (ack_outbox_t * p_box, uint8_t node_id, uint16_t seq)
{
    bool b_freed = false;

    for (uint32_t index = 0u; index < ACK_OUTBOX_SLOTS; index++)
    {
        ack_outbox_entry_t * const p_entry = &p_box->slots[index];

        if ((p_entry->b_busy) && (p_entry->node_id == node_id) &&
            (p_entry->seq == seq))
        {
            p_entry->b_busy = false;
            b_freed         = true;
        }
    }

    return b_freed;
}

/**
 * @brief  Drop every waiting message of one type.
 * @param  p_box  Outbox; must not be NULL.
 * @param  type   Message type code.
 */
void ack_outbox_cancel (ack_outbox_t * p_box, uint8_t type)
{
    for (uint32_t index = 0u; index < ACK_OUTBOX_SLOTS; index++)
    {
        if ((p_box->slots[index].b_busy) && (p_box->slots[index].type == type))
        {
            p_box->slots[index].b_busy = false;
        }
    }
}

/**
 * @brief  Earliest time a send is due.
 * @param  p_box  Outbox; must not be NULL.
 * @return Microseconds, or UINT64_MAX when nothing waits.
 */
uint64_t ack_outbox_next_due (ack_outbox_t const * p_box)
{
    uint64_t due = UINT64_MAX;

    for (uint32_t index = 0u; index < ACK_OUTBOX_SLOTS; index++)
    {
        if ((ack_outbox_busy_at(p_box, index)) &&
            (p_box->slots[index].next_us < due))
        {
            due = p_box->slots[index].next_us;
        }
    }

    return due;
}

/**
 * @brief  Pick the entry to send now (see ack_outbox.h).
 * @param  p_box    Outbox; must not be NULL.
 * @param  now_us   Current time, microseconds.
 * @param  p_index  Receives the slot index when true.
 * @return true when an entry is due.
 */
bool ack_outbox_due (ack_outbox_t * p_box, uint64_t now_us, uint32_t * p_index)
{
    bool     b_found = false;
    uint64_t best_us = UINT64_MAX;

    for (uint32_t index = 0u; index < ACK_OUTBOX_SLOTS; index++)
    {
        ack_outbox_entry_t * const p_entry = &p_box->slots[index];

        if ((p_entry->b_busy) && (p_entry->deadline_us <= now_us))
        {
            p_entry->b_busy = false;
            p_box->expired++;
        }
    }

    for (uint32_t index = 0u; index < ACK_OUTBOX_SLOTS; index++)
    {
        ack_outbox_entry_t const * const p_entry = &p_box->slots[index];

        if ((p_entry->b_busy) && (p_entry->next_us <= now_us) &&
            (p_entry->next_us < best_us))
        {
            best_us  = p_entry->next_us;
            *p_index = index;
            b_found  = true;
        }
    }

    return b_found;
}

/**
 * @brief  Record that the entry at index was sent.
 * @param  p_box     Outbox; must not be NULL.
 * @param  index     Slot index from ack_outbox_due().
 * @param  now_us    Time of the send, microseconds.
 * @param  retry_us  Interval before the next resend, microseconds.
 */
void ack_outbox_sent (ack_outbox_t * p_box, uint32_t index, uint64_t now_us,
                      uint64_t retry_us)
{
    if (index < ACK_OUTBOX_SLOTS)
    {
        ack_outbox_entry_t * const p_entry = &p_box->slots[index];

        p_entry->sends++;
        p_entry->next_us = now_us + retry_us;
    }
}

/**
 * @brief  Whether a slot holds a waiting message.
 * @param  p_box  Outbox; must not be NULL.
 * @param  index  Slot index, below ACK_OUTBOX_SLOTS.
 * @return The busy flag.
 */
static bool ack_outbox_busy_at (ack_outbox_t const * p_box, uint32_t index)
{
    return p_box->slots[index].b_busy;
}

/*** end of file ***/
