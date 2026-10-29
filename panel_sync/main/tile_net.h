/**
 * @file    tile_net.h
 * @brief   Tile Network Task (T-B3-03): the only path to the network.
 *
 * Minimal implementation by Buddy 4 for T-B4-03. Owner: Cheston, task
 * T-B3-03. Implemented: tile_net_start(), tile_net_task() (Network Task),
 * BEACON receive on UDP 4210 decoded with proto_decode_beacon(), t_l from
 * esp_timer_get_time() at the OD-09 hook, the (t_p, t_l) pair posted to the
 * Time Sync Queue q_timesync by drop-oldest with a drop counter, seq-gap
 * (router-lost), received and dropped counters, and a per-beacon spread log
 * line "BCN,<seq>,<t_p>,<t_l>" for the OD-09 measurement.
 * Not yet implemented: WiFi start-up and wifi_event (still in panel_sync.c),
 * tn_send_heartbeat() and the Node ID stagger, the Command Queue q_command
 * and its NACK (added with T-B4-04), session_id and stale-seq rejection
 * (T-B2-04), and every other message type.
 *
 * PROVISIONAL (OD-09, owners Cheston and Ahsanul): TN_RX_HOOK selects the
 * receive hook at compile time. TN_RX_HOOK_LWIP (default, the plan's first
 * option (a)) timestamps in the lwIP udp_recv callback in the tcpip task
 * (priority 18, core 0); TN_RX_HOOK_RECVFROM (option (b)) timestamps when
 * recvfrom returns in the Network Task (priority 12). Build each and compare
 * the 10 min spread logs (smaller p99 wins).
 * PROVISIONAL (OD-11, owner Cheston): Time Sync Queue length 4
 * (Week10_Kickoff/decision_briefs.md: at most ceil(307 / 100) = 4 beacons
 * released by one DTIM 3 interval).
 *
 * DEVIATION D-2 (module-wide): ESP-IDF, FreeRTOS and lwIP names and types.
 */
#ifndef TILE_NET_H
#define TILE_NET_H

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/** Receive hook option (a): lwIP udp_recv callback in the tcpip task. */
#define TN_RX_HOOK_LWIP 1

/** Receive hook option (b): recvfrom return in the Network Task. */
#define TN_RX_HOOK_RECVFROM 2

#ifndef TN_RX_HOOK
/** OD-09 receive hook, PROVISIONAL default (a). Build option (b) with
 *  idf.py -DTN_RX_HOOK=2 build (panel_sync/main/CMakeLists.txt passes it). */
#define TN_RX_HOOK TN_RX_HOOK_LWIP
#endif

/** Time Sync Queue length (PROVISIONAL, OD-11). */
#define TILE_NET_TIMESYNC_QUEUE_LEN 4u

/** Beacon receive counters. For the T-B3-03 check, received + lost = beacons
 *  sent by the Pico, and (received - dropped) pairs reached the Time Sync
 *  Task, so (received - dropped) + dropped + lost = sent. */
typedef struct
{
    uint32_t received; /**< Decoded beacons, each posted to the queue. */
    uint32_t dropped;  /**< Oldest pairs dropped because the queue was full. */
    uint32_t lost;     /**< Beacons missing from the seq sequence. */
} tile_net_stats_t;

/**
 * @brief  Create the queues and the Network Task (priority 12 on core 0,
 *         tile_tasks.h). Call after esp_netif_init(), before the Time Sync
 *         Task is created.
 * @return true when the queues and the task were created.
 */
bool tile_net_start (void);

/**
 * @brief  The Time Sync Queue: items are timesync_pair_t, drop-oldest.
 * @return Its handle, or NULL before tile_net_start().
 */
QueueHandle_t tile_net_timesync_queue (void);

/**
 * @brief  Copy of the beacon receive counters.
 * @param  p_stats  Receives the counters.
 */
void tile_net_get_stats (tile_net_stats_t * p_stats);

/**
 * @brief  Network Task: prints the spread log and, under option (b), receives
 *         the beacons.
 * @param  p_arg  Unused.
 */
void tile_net_task (void * p_arg);

#endif /* TILE_NET_H */

/*** end of file ***/
