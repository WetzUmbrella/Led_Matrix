/**
 * @file    ack_outbox.h
 * @brief   Outbox of acked unicast messages (layout_table.md section 10
 *          item 13 and the whack-a-mole ACK rule): a message is kept until
 *          the ACK with its node_id and seq arrives, resent at a fixed
 *          interval until then, and dropped at its deadline. Pure C with no
 *          hardware call, shared by the Pico coordinator and the tiles.
 *
 * Owner: Benjamin (Buddy 2). Written for the whack-a-mole re-plan, build
 * step 7. The retry interval and deadlines are plan choices made by the
 * caller; this module only keeps the table.
 *
 * - A send is identified by (node_id, seq), because all acked messages from
 *   one sender share one seq counter (layout_table.md section 10 item 13).
 * - Entries are sent in due order, one per call of ack_outbox_due(), so a
 *   caller that sends one datagram per loop pass stays bounded (OD-08).
 */
#ifndef ACK_OUTBOX_H
#define ACK_OUTBOX_H

#include <stdbool.h>
#include <stdint.h>

/** Entries kept at once: 9 tiles times 2 messages, plus 6 spare (plan
 *  choice; a round start sends START to 9 tiles, and a score sends END and
 *  SCORE_MASK to 9). */
#define ACK_OUTBOX_SLOTS 24u

/** Largest message kept, bytes (the largest acked message is MOLE_ON, 14). */
#define ACK_OUTBOX_MSG_MAX 16u

/** One message waiting for its ACK. */
typedef struct
{
    bool        b_busy;
    uint8_t     node_id;   /**< Addressee: 1 to 9 for a tile, 0 for the PC. */
    uint8_t     type;      /**< Message type code, for ack_outbox_cancel(). */
    uint16_t    seq;       /**< Header seq; the ACK's acked_seq. */
    uint32_t    addr;      /**< Destination address. */
    uint16_t    port;      /**< Destination port. */
    uint8_t     len;       /**< Bytes in buf. */
    uint8_t     buf[ACK_OUTBOX_MSG_MAX];
    uint64_t    next_us;     /**< Next send is due at or after this time. */
    uint64_t    deadline_us; /**< Dropped at or after this time. */
    uint32_t    sends;       /**< Sends so far, the first included. */
} ack_outbox_entry_t;

/** The outbox. */
typedef struct
{
    ack_outbox_entry_t slots[ACK_OUTBOX_SLOTS];
    uint32_t           full_drops; /**< Messages refused: no free slot. */
    uint32_t           expired;    /**< Messages dropped at their deadline. */
} ack_outbox_t;

/**
 * @brief  Empty the outbox and clear its counters.
 * @param  p_box  Outbox; must not be NULL.
 */
void ack_outbox_init (ack_outbox_t * p_box);

/**
 * @brief  Queue a message; its first send is due at once.
 * @param  p_box     Outbox; must not be NULL.
 * @param  node_id   Addressee (1 to 9, or 0 for the PC).
 * @param  type      Message type code.
 * @param  seq       Header seq of the message.
 * @param  addr      Destination address.
 * @param  port      Destination port.
 * @param  p_buf     Encoded message.
 * @param  len       Bytes in p_buf, 1 to ACK_OUTBOX_MSG_MAX.
 * @param  now_us    Current time, microseconds.
 * @param  deadline_us  Drop the message at or after this time.
 * @return false when the message is refused (no free slot or bad length);
 *         full_drops counts a full outbox.
 */
bool ack_outbox_add (ack_outbox_t * p_box, uint8_t node_id, uint8_t type,
                     uint16_t seq, uint32_t addr, uint16_t port,
                     uint8_t const * p_buf, uint32_t len, uint64_t now_us,
                     uint64_t deadline_us);

/**
 * @brief  An ACK arrived: free the message it names.
 * @param  p_box    Outbox; must not be NULL.
 * @param  node_id  Sender of the ACK (the addressee of the message).
 * @param  seq      acked_seq of the ACK.
 * @return true when a waiting message was freed.
 */
bool ack_outbox_ack (ack_outbox_t * p_box, uint8_t node_id, uint16_t seq);

/**
 * @brief  Drop every waiting message of one type (a round that ended makes
 *         its MOLE_ON messages pointless).
 * @param  p_box  Outbox; must not be NULL.
 * @param  type   Message type code.
 */
void ack_outbox_cancel (ack_outbox_t * p_box, uint8_t type);

/**
 * @brief  Earliest time a send is due.
 * @param  p_box  Outbox; must not be NULL.
 * @return Microseconds, or UINT64_MAX when nothing waits.
 */
uint64_t ack_outbox_next_due (ack_outbox_t const * p_box);

/**
 * @brief  Pick the entry to send now. Entries past their deadline are
 *         dropped first (counted in expired). Among the entries due at
 *         now_us, the one due earliest is returned.
 * @param  p_box     Outbox; must not be NULL.
 * @param  now_us    Current time, microseconds.
 * @param  p_index   Receives the slot index when true.
 * @return true when an entry is due.
 */
bool ack_outbox_due (ack_outbox_t * p_box, uint64_t now_us, uint32_t * p_index);

/**
 * @brief  Record that the entry at index was sent: its next send is due
 *         retry_us later.
 * @param  p_box     Outbox; must not be NULL.
 * @param  index     Slot index from ack_outbox_due().
 * @param  now_us    Time of the send, microseconds.
 * @param  retry_us  Interval before the next resend, microseconds.
 */
void ack_outbox_sent (ack_outbox_t * p_box, uint32_t index, uint64_t now_us,
                      uint64_t retry_us);

#endif /* ACK_OUTBOX_H */

/*** end of file ***/
