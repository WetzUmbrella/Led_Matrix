/**
 * @file    tile_net.h
 * @brief   Tile Network Task (T-B3-03): the only path to the network. WiFi
 *          start-up, BEACON and command receive, REGISTER, HEARTBEAT,
 *          SYNC_READY and the Time Sync and Command Queues.
 *
 * Owner: Cheston (Buddy 3), task T-B3-03. Implemented by Ahsanul (B4) for
 * Cheston; the parts below marked 29 Oct 2026 were written in Ben's (B2)
 * session for Cheston to review and defend.
 * Implemented:
 * - tile_net_start(): NVS, the Node ID (board number) from NVS, esp_netif,
 *   WiFi in station mode with esp_wifi_set_storage(WIFI_STORAGE_RAM) before
 *   esp_wifi_start() (VA-3.3) and power save off (SRM-8Mc), wifi_event
 *   (moved from panel_sync.c, plan section 0.1), the queues and the task.
 * - BEACON receive on UDP 4210 decoded with proto_decode_beacon(), t_l from
 *   esp_timer_get_time() at the OD-09 hook, the (t_p, t_l) pair posted to
 *   q_timesync by drop-oldest with a drop counter, seq-gap (router-lost)
 *   counting and the "BCN,<seq>,<t_p>,<t_l>" spread log.
 * - START_AT, RESUME_AT, PAUSE and STOP on UDP 4210 decoded into
 *   tile_net_command_t and posted to q_command (length 3); a full queue
 *   rejects the message and sends NACK reason 2 to the Pico (VA-3.6).
 * - The Pico address from the beacons' source address; the session_id of
 *   the last beacon copied into every message this tile sends.
 * - REGISTER {Node ID, cached session_id, CRC32} when the Pico is first
 *   heard and after every reconnect (FR9, W6B-2 restored).
 * - HEARTBEAT every 0.5 s (proposed), the first one (Node ID - 1) x 500 / 9
 *   ms after the REGISTER (derived: 0.5 s spread over 9 Node IDs, VA-2.10).
 * - SYNC_READY with every heartbeat while timesync_sync_ready() is true,
 *   because no message acknowledges it (layout_table.md section 10, plan
 *   choice: a lost SYNC_READY is repeated 0.5 s later).
 * - CONNECTING timeout: no IP address 10 s (proposed, W6D-2d) after the
 *   join starts sets the WIFI condition bit (fault_word); the bit clears on
 *   IP_EVENT_STA_GOT_IP.
 * - T-B3-01: asset_store_probe_heap() once, after the first IP address.
 * Not yet implemented: the comms statechart and the upload path (T-B3-05),
 * ASSET_READY and the cached asset values in REGISTER (sent as 0 until
 * asset_store exists, T-B3-04), TELEMETRY and its ACK (T-B3-06).
 *
 * PROVISIONAL (OD-09, owners Cheston and Ahsanul): TN_RX_HOOK selects the
 * receive hook at compile time. TN_RX_HOOK_LWIP (default, option (a))
 * timestamps in the lwIP udp_recv callback in the tcpip task (priority 18,
 * core 0); TN_RX_HOOK_RECVFROM (option (b)) timestamps when recvfrom returns
 * in the Network Task (priority 12). Compare the 10 min spread logs.
 * PROVISIONAL (OD-11, owner Cheston): Time Sync Queue length 4, Command
 * Queue length 3 (Week10_Kickoff/decision_briefs.md).
 * PROVISIONAL (owner Cheston): after the CONNECTING timeout the tile keeps
 * retrying the join (so a router power cycle recovers) with the WIFI bit
 * set; T-B3-05's statechart decides whether FAULT stops the retries.
 * PROVISIONAL (owner Benjamin): ports 4210 and 4211, layout_table.md
 * section 11.
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

/** Command Queue length (PROVISIONAL, OD-11). */
#define TILE_NET_COMMAND_QUEUE_LEN 3u

/** One command from the Pico, as Playback Control reads it from the
 *  Command Queue (T-B4-04). */
typedef struct
{
    uint8_t  type;        /**< proto_type_t: START_AT, RESUME_AT, PAUSE,
                               STOP. */
    uint8_t  session_id;  /**< From the header. */
    uint16_t seq;         /**< From the header. */
    uint16_t frame_index; /**< RESUME_AT only. */
    uint64_t t_us;        /**< t_start or t_resume, Pico time. */
} tile_net_command_t;

/**
 * @brief  Start NVS, WiFi, the queues and the Network Task (priority 12 on
 *         core 0, tile_tasks.h). Call once from app_main, before the Time
 *         Sync Task is created.
 * @return true when everything was created.
 */
bool tile_net_start (void);

/**
 * @brief  The Time Sync Queue: items are timesync_pair_t, drop-oldest.
 * @return Its handle, or NULL before tile_net_start().
 */
QueueHandle_t tile_net_timesync_queue (void);

/**
 * @brief  The Command Queue: items are tile_net_command_t, reject-and-NACK.
 * @return Its handle, or NULL before tile_net_start().
 */
QueueHandle_t tile_net_command_queue (void);

/**
 * @brief  This tile's Node ID (board number), read once at start.
 * @return 1 to 9, or 0 when no board number is set.
 */
uint8_t tile_net_node_id (void);

/**
 * @brief  Whether the tile has an IP address (Playback Control input).
 * @return true after IP_EVENT_STA_GOT_IP until the next disconnect.
 */
bool tile_net_has_ip (void);

/**
 * @brief  Network Task: sends REGISTER, HEARTBEAT and SYNC_READY, prints
 *         the spread log and, under option (b), receives the datagrams.
 * @param  p_arg  Unused.
 */
void tile_net_task (void * p_arg);

#endif /* TILE_NET_H */

/*** end of file ***/
