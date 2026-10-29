/**
 * @file    fault_word.h
 * @brief   Shared 32-bit tile fault word (T-B3-06): set by atomic fetch-or,
 *          event bits cleared only on the TELEMETRY_ACK, condition bits
 *          cleared only by their owning task.
 *
 * Implemented by Ahsanul (B4) for Cheston, task T-B3-06.
 * Implemented: the word, fault_word_set(), fault_word_clear_condition(),
 * fault_word_snapshot(), fault_word_clear_events() and the bit map with
 * FAULT_EVENT_MASK; host tests in panel_sync/test/test_fault_word.c.
 * Not yet implemented: tn_telemetry_send() and tn_on_telemetry_ack() in
 * tile_net.c (snapshot, TELEMETRY {seq, fault word, orientation, display
 * state}, remember (seq, sent bits), call fault_word_clear_events() on the
 * matching ACK), the Telemetry and Fault queues q_telemetry and q_fault, and
 * the telemetry period (OD-11). Those need the TELEMETRY rows of T-B2-01.
 *
 * PROVISIONAL (OD-12, owner Cheston): bit numbers, and the event or
 * condition class of CRC_FAIL, QUEUE_OVERFLOW, BEACON_LOSS and SYNC_STALE.
 * The class of MISSED_DEADLINE (event) and of WIFI, IMU, ASSET_FORMAT and
 * DISPLAY (conditions) is fixed by the plan rows quoted beside each bit.
 *
 * DEVIATION D-1 (module-wide): C11 stdatomic.h for the fetch-or, fetch-and
 * and acquire load across the two ESP32-S3 cores (LDC-2, VB-3a).
 */
#ifndef FAULT_WORD_H
#define FAULT_WORD_H

#include <stdint.h>

/** Condition: router join or link lost (T-B3-05 "the WIFI condition bit").
 *  Owner: tile_net. */
#define FAULT_BIT_WIFI (UINT32_C(1) << 0u)

/** Event: asset CRC32 check failed (PROVISIONAL, OD-12: each failed verify is
 *  one occurrence and a retry follows, so nothing stays true to latch). */
#define FAULT_BIT_CRC_FAIL (UINT32_C(1) << 1u)

/** Event: a queue dropped or rejected an item (PROVISIONAL, OD-12: each drop
 *  is one occurrence; the per-queue drop counters keep the count). */
#define FAULT_BIT_QUEUE_OVERFLOW (UINT32_C(1) << 2u)

/** Condition: no beacon for longer than the 1 s holdover (PROVISIONAL, OD-12:
 *  it stays true until beacons return). Owner: playback_control. */
#define FAULT_BIT_BEACON_LOSS (UINT32_C(1) << 3u)

/** Condition: IMU transaction failed (T-B5-05 "the IMU condition bit is set
 *  and is cleared when injection stops"). Owner: orientation. */
#define FAULT_BIT_IMU (UINT32_C(1) << 4u)

/** Event: RMT still busy at a tick (T-B5-03 "raise the MISSED_DEADLINE event
 *  bit through fault_word_set()"). */
#define FAULT_BIT_MISSED_DEADLINE (UINT32_C(1) << 5u)

/** Condition: unknown format_version or wrong payload length (VC-5 "raises an
 *  ASSET_FORMAT condition fault"). Owner: asset_store. */
#define FAULT_BIT_ASSET_FORMAT (UINT32_C(1) << 6u)

/** Condition: RMT timeout or buffer failure (T-B5-03 "sets the DISPLAY
 *  condition bit"). Owner: display_engine. */
#define FAULT_BIT_DISPLAY (UINT32_C(1) << 7u)

/** Event: a timesync read ended in SYNC_STALE after 3 failed retries
 *  (PROVISIONAL, OD-12: VC-1 makes each stale read one occurrence; the reader
 *  already keeps the count in stale_count). */
#define FAULT_BIT_SYNC_STALE (UINT32_C(1) << 8u)

/** Event bits: cleared by fault_word_clear_events() on the TELEMETRY_ACK (VC-4,
 *  plan name EVENT_MASK). */
#define FAULT_EVENT_MASK                                                       \
    (FAULT_BIT_CRC_FAIL | FAULT_BIT_QUEUE_OVERFLOW |                           \
     FAULT_BIT_MISSED_DEADLINE | FAULT_BIT_SYNC_STALE)

/** Condition bits: cleared only by their owner with
 *  fault_word_clear_condition(). */
#define FAULT_CONDITION_MASK                                                   \
    (FAULT_BIT_WIFI | FAULT_BIT_BEACON_LOSS | FAULT_BIT_IMU |                  \
     FAULT_BIT_ASSET_FORMAT | FAULT_BIT_DISPLAY)

/**
 * @brief  Raise fault bits (atomic fetch-or, LDC-2, VB-3a). Safe from any
 *         task on either core. Every task raises faults only through this
 *         function (RAR-1).
 * @param  bits  FAULT_BIT_* values to set.
 */
void fault_word_set (uint32_t bits);

/**
 * @brief  Clear condition bits (atomic fetch-and). Only the task that owns a
 *         condition calls this, when the condition has ended (VB-3b, VC-4).
 *         Event bits in bits are ignored.
 * @param  bits  FAULT_BIT_* condition values to clear.
 */
void fault_word_clear_condition (uint32_t bits);

/**
 * @brief  Read the whole word for a TELEMETRY packet.
 * @return The fault word.
 */
uint32_t fault_word_snapshot (void);

/**
 * @brief  On the TELEMETRY_ACK for a packet that carried sent_bits, clear
 *         (sent_bits AND FAULT_EVENT_MASK) with an atomic fetch-and (VC-4).
 *         Condition bits and event bits that were not sent stay set.
 * @param  sent_bits  The fault word sent in the acknowledged packet.
 */
void fault_word_clear_events (uint32_t sent_bits);

#endif /* FAULT_WORD_H */

/*** end of file ***/
