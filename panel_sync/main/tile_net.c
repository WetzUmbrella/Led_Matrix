/**
 * @file    tile_net.c
 * @brief   Tile Network Task (T-B3-03). See tile_net.h for what is
 *          implemented and the PROVISIONAL choices.
 *
 * Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston.
 *
 * Two contexts touch this module:
 * - the receive context (the lwIP callback in the tcpip task, option (a), or
 *   the Network Task, option (b)) runs tn_on_datagram(): it is the only
 *   producer of the three queues and the only writer of the receive
 *   counters, which it copies into each log record;
 * - the Network Task owns everything else (the Pico address, the session,
 *   the send socket, the heartbeat schedule) and learns about received
 *   beacons only from the log records, so no variable is shared between
 *   the two contexts except through a FreeRTOS queue or event group.
 * The WiFi event handler runs in the ESP-IDF event task and talks to the
 * Network Task only through the event group. The Display Engine is the only
 * producer of the Telemetry Queue (tile_net_post_status()); its drop counter
 * is written only in that context and travels inside each item (T-B3-06).
 *
 * DEVIATION D-2 (module-wide): ESP-IDF, FreeRTOS and lwIP names, types and
 * macros (QueueHandle_t, EventGroupHandle_t, pdTRUE, portMAX_DELAY,
 * esp_err_t, esp_event_base_t, struct udp_pcb, struct pbuf, ip_addr_t,
 * u16_t, err_t, the int socket descriptor and ssize_t).
 */
#include "tile_net.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/pbuf.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "lwip/udp.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "asset_store.h"
#include "playback_control.h"
#include "coord_lock.h"
#include "fault_word.h"
#include "proto.h"
#include "telemetry_book.h"
#include "tile_tasks.h"
#include "timesync.h"

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#else
/** Placeholder credentials (wifi_secrets.h is git-ignored). */
#define ROUTER_SSID "CHANGE_ME"
#define ROUTER_PASS "CHANGE_ME"
#endif

/** UDP port the tiles listen on: beacons and commands (layout_table.md
 *  section 11). */
#define TN_SYNC_PORT 4210u

/** UDP port of the Pico for tile messages. */
#define TN_PICO_PORT 4211u

/** UDP port of the tile for PC messages: TELEMETRY_ACK (layout_table.md
 *  section 11). */
#define TN_PC_TO_TILE_PORT 4214u

/** UDP port of the PC for TELEMETRY, sent as a subnet broadcast
 *  (layout_table.md section 11). */
#define TN_TELEMETRY_PORT 4215u

/** Log queue length (plan choice: 1.6 s of beacons at 10 Hz, so a slow
 *  console never stalls the receive path). */
#define TN_LOG_QUEUE_LEN 16u

/** Counter line every 100 beacons (10 s, plan choice). */
#define TN_STATS_EVERY 100u

/** Receive buffer: one byte more than the longest message on port 4210
 *  (RESUME_AT, 13 B), so a longer datagram is seen as the wrong length. */
#define TN_RX_BYTES (PROTO_RESUME_AT_BYTES + 1u)

/** Receive buffer for port 4214: one byte more than the longest message
 *  there (ASSET_CHUNK, 5 + 1,024 B), so a longer datagram is seen as the
 *  wrong length. */
#define TN_PC_RX_BYTES (PROTO_ASSET_CHUNK_MAX_BYTES + 1u)

/** Heartbeat period 0.5 s (proposed, VA-2.7). */
#define TN_HEARTBEAT_US INT64_C(500000)

/** Heartbeat stagger: (Node ID - 1) x 500 / 9 ms (derived, VA-2.10). */
#define TN_STAGGER_US (TN_HEARTBEAT_US / (int64_t)PROTO_NODE_MAX)

/** CONNECTING timeout 10 s (proposed, W6D-2d). */
#define TN_CONNECT_TIMEOUT_US INT64_C(10000000)

/** Network Task wake-up when idle, 20 ms (plan choice: heartbeats leave
 *  within 20 ms of their slot, far inside the 2.5 s timeout). */
#define TN_IDLE_WAIT_MS 20u

#ifdef TN_TEST_INJECT_FAULTS
/** VC-4 test build only (T-B3-06): an event fault is injected every 5 s
 *  (plan choice: 5 telemetry periods, so each injection is a separate
 *  report unless its ACK is lost twice). Off in the shipped firmware. */
#define TN_INJECT_PERIOD_US INT64_C(5000000)
#endif

/** REGISTER is sent this many times, TN_REGISTER_GAP_US apart, so one lost
 *  datagram does not leave the tile invisible (R1-1; PROVISIONAL, owner
 *  Cheston). Same pattern as the three START_AT copies. */
#define TN_REGISTER_COPIES 3u

/** Gap between REGISTER copies, 100 ms (R1-1; PROVISIONAL, owner Cheston). */
#define TN_REGISTER_GAP_US INT64_C(100000)

/** A beacon seq this far behind the previous one (wrap-aware) means the
 *  Pico restarted: the tile registers again (R1-1). */
#define TN_BEACON_SEQ_BACK 16

/** Datagrams read from port 4214 in one Network Task pass, so a flood
 *  cannot starve the heartbeats (R1-9; PROVISIONAL, owner Cheston). */
#define TN_PC_RX_PER_PASS 8u

/** Event group bits set by the WiFi event handler. */
#define TN_EV_HAVE_IP BIT0
#define TN_EV_NEW_IP  BIT1

/** Kinds of log record. */
#define TN_REC_BEACON 1u
#define TN_REC_NACK   2u

/** One record from the receive context to the Network Task. */
typedef struct
{
    uint8_t  kind;       /**< TN_REC_BEACON or TN_REC_NACK. */
    uint8_t  session_id; /**< Beacon: its session; NACK: the command's. */
    uint16_t seq;        /**< Beacon seq, or the rejected command's seq. */
    uint32_t src_addr;   /**< Sender address (the Pico). */
    int64_t  t_p;        /**< Beacon t_p. */
    int64_t  t_l;        /**< Beacon local receive time. */
    uint32_t received;   /**< Counters at the time of the record. */
    uint32_t dropped;
    uint32_t lost;
    uint32_t log_dropped;
    uint32_t foreign; /**< Beacons from another coordinator (lock). */
} tn_record_t;

/** One Telemetry Queue item: the status and the producer's drop count. */
typedef struct
{
    tile_net_status_t status;
    uint32_t          dropped; /**< Telemetry Queue drops so far. */
} tn_status_item_t;

/** Scratch item large enough for any of the queues, for the
 *  drop-oldest read. */
typedef union
{
    timesync_pair_t    pair;
    tn_record_t        record;
    tile_net_command_t command;
    tn_status_item_t   status;
} tn_any_item_t;

/* Created once in tile_net_start(), then only read. */
static QueueHandle_t      g_p_q_timesync  = NULL;
static QueueHandle_t      g_p_q_command   = NULL;
static QueueHandle_t      g_p_q_log       = NULL;
static QueueHandle_t      g_p_q_telemetry = NULL;
static EventGroupHandle_t g_p_events      = NULL;
static uint8_t            g_node_id       = 0u;

/* Receive context only (single producer). */
static uint32_t g_received    = 0u;
static uint32_t g_dropped     = 0u;
static uint32_t g_lost        = 0u;
static uint32_t g_log_dropped = 0u;
static uint16_t g_next_seq    = 0u;
static bool     g_b_seq_known = false;
/* Locked coordinator: all zero is "none" (same as coord_lock_init). */
static coord_lock_t g_coord = {.addr = 0u, .last_us = 0, .foreign = 0u};

/* Display Engine context only (single producer of the Telemetry Queue). */
static uint32_t g_tel_q_dropped = 0u;

/* Network Task only. */
static int      g_tx_sock          = -1; /* DEVIATION D-2: lwIP int socket. */
static bool     g_b_pico_known     = false;
static bool     g_b_need_register  = false;
static bool     g_b_registered     = false;
static bool     g_b_wifi_fault     = false;
static bool     g_b_heap_logged    = false;
static uint32_t g_pico_addr        = 0u;
static uint8_t  g_session          = 0u;
static uint16_t g_tile_seq         = 0u;
static int64_t  g_next_beat_us     = 0;
static int64_t  g_join_start_us    = 0;
static uint32_t g_register_left    = 0u; /* REGISTER copies still to send. */
static int64_t  g_next_register_us = 0;
static bool     g_b_bcn_seen       = false; /* A beacon record was handled. */
static uint8_t  g_last_bcn_session = 0u;
static uint16_t g_last_bcn_seq     = 0u;

/* Network Task only (T-B3-06). */
static int              g_ack_sock    = -1; /* DEVIATION D-2: lwIP socket. */
static uint32_t         g_bcast_addr  = 0u; /* Network byte order. */
static int64_t          g_next_tel_us = 0;
static telemetry_book_t g_tel_book;
static tn_status_item_t g_tel_status   = {{0u, 0u}, 0u};
static uint32_t         g_tel_sent     = 0u;
static uint32_t         g_tel_acked    = 0u;
static uint32_t         g_ack_unknown  = 0u;
static uint32_t         g_pc_addr      = 0u; /* Last uploader on 4214. */
static uint16_t         g_pc_port      = 0u; /* Its port, host order. */
static bool             g_b_had_ip     = false;
static uint32_t         g_comms        = 0u; /* tile_net_comms_t. */
static uint32_t         g_upload_nacks = 0u;
static uint32_t         g_upload_wrong_session = 0u;
#ifdef TN_TEST_INJECT_FAULTS
static int64_t  g_next_inject_us = 0;
static uint32_t g_injected       = 0u;
#endif

static bool      tn_wifi_start (void);
static esp_err_t tn_nvs_start (void);
static bool      tn_read_node_id (void);
static void      tn_wifi_event (void * p_arg, esp_event_base_t base,
                                int32_t event_id, void * p_data);
static void tn_on_datagram (uint8_t const * p_buf, uint32_t len, int64_t t_l,
                            uint32_t src_addr);
static void tn_on_beacon (uint8_t const * p_buf, uint32_t len, int64_t t_l,
                          uint32_t src_addr);
static void tn_on_command (uint8_t const * p_buf, uint32_t len,
                           proto_header_t const * p_header, uint32_t src_addr);
static bool tn_decode_command (uint8_t const * p_buf, uint32_t len,
                               proto_header_t const * p_header,
                               tile_net_command_t *   p_command);
static void tn_post_drop_oldest (QueueHandle_t p_queue, void const * p_item,
                                 uint32_t * p_drops);
static void tn_service (TickType_t wait);
static bool tn_pico_restarted (tn_record_t const * p_record);
static void tn_handle_record (tn_record_t const * p_record);
static void tn_housekeeping (void);
static void tn_send_heartbeat (int64_t now);
static void tn_send_register (void);
static void tn_send_nack (uint8_t reason, uint8_t session, uint16_t ref_seq,
                          bool b_to_pc);
static void tn_send_pc (uint8_t const * p_buf, uint32_t len);
static void tn_on_pc_datagram (uint8_t const * p_buf, uint32_t len,
                               uint32_t src_addr, uint16_t src_port);
static bool tn_on_upload (uint8_t const * p_buf, uint32_t len,
                          proto_header_t const * p_header);
static void tn_send_replies (void);
static void tn_comms_sm (void);
static void tn_send (uint8_t const * p_buf, uint32_t len);
static int  tn_ack_socket (void); /* DEVIATION D-2: lwIP int socket. */
static void tn_on_new_ip (void);
static void tn_telemetry_service (int64_t now);
static void tn_telemetry_send (void);
static void tn_poll_pc (void);
static void tn_on_telemetry_ack (uint16_t acked_seq);

#if (TN_RX_HOOK == TN_RX_HOOK_LWIP)
static void tn_lwip_setup (void * p_ctx);
static void tn_lwip_recv (void * p_arg, struct udp_pcb * p_pcb,
                          struct pbuf * p_pbuf, ip_addr_t const * p_addr,
                          u16_t port);
#else
static int tn_rx_socket (void); /* DEVIATION D-2: lwIP int socket. */
#endif

/**
 * @brief  Start NVS, WiFi, the queues and the Network Task.
 * @return true when everything was created.
 */
bool tile_net_start (void)
{
    bool b_ok = false;

    g_p_q_timesync =
        xQueueCreate(TILE_NET_TIMESYNC_QUEUE_LEN, sizeof(timesync_pair_t));
    g_p_q_command =
        xQueueCreate(TILE_NET_COMMAND_QUEUE_LEN, sizeof(tile_net_command_t));
    g_p_q_log = xQueueCreate(TN_LOG_QUEUE_LEN, sizeof(tn_record_t));
    g_p_q_telemetry =
        xQueueCreate(TILE_NET_TELEMETRY_QUEUE_LEN, sizeof(tn_status_item_t));
    g_p_events = xEventGroupCreate();

    if ((NULL != g_p_q_timesync) && (NULL != g_p_q_command) &&
        (NULL != g_p_q_log) && (NULL != g_p_q_telemetry) &&
        (NULL != g_p_events))
    {
        b_ok = tn_wifi_start();
    }

    if (b_ok)
    {
        b_ok = (pdPASS == xTaskCreatePinnedToCore(
                              tile_net_task, TILE_NET_NAME, TILE_NET_STACK,
                              NULL, TILE_NET_PRIO, NULL, TILE_NET_CORE));
    }

#if (TN_RX_HOOK == TN_RX_HOOK_LWIP)
    if (b_ok)
    {
        /* The raw API must run in the tcpip task, so set up there. */
        b_ok = (ERR_OK == tcpip_callback(tn_lwip_setup, NULL));
    }
#endif

    return b_ok;
}

/**
 * @brief  The Time Sync Queue.
 * @return Its handle, or NULL before tile_net_start().
 */
QueueHandle_t tile_net_timesync_queue (void)
{
    return g_p_q_timesync;
}

/**
 * @brief  The Command Queue.
 * @return Its handle, or NULL before tile_net_start().
 */
QueueHandle_t tile_net_command_queue (void)
{
    return g_p_q_command;
}

/**
 * @brief  This tile's Node ID.
 * @return 1 to 9, or 0 when unset.
 */
uint8_t tile_net_node_id (void)
{
    return g_node_id;
}

/**
 * @brief  Whether the tile has an IP address.
 * @return true while TN_EV_HAVE_IP is set.
 */
bool tile_net_has_ip (void)
{
    bool b_has_ip = false;

    if (NULL != g_p_events)
    {
        b_has_ip = (0u != (xEventGroupGetBits(g_p_events) & TN_EV_HAVE_IP));
    }

    return b_has_ip;
}

/**
 * @brief  Post a status update from the Display Engine (single producer):
 *         drop-oldest with a drop counter (VA-3.7), never blocks.
 * @param  orientation    proto_orientation_t, 0 to 3.
 * @param  display_state  proto_display_state_t, 0 to 6.
 * @return true when the item was queued.
 */
bool tile_net_post_status (uint8_t orientation, uint8_t display_state)
{
    bool b_ok = false;

    if ((NULL != g_p_q_telemetry) && (orientation < PROTO_ORIENT_COUNT) &&
        (display_state < PROTO_DISPLAY_COUNT))
    {
        uint32_t const   before = g_tel_q_dropped;
        tn_status_item_t item   = {
              .status  = {.orientation   = orientation,
                          .display_state = display_state},
              .dropped = before,
        };

        if (0u == uxQueueSpacesAvailable(g_p_q_telemetry))
        {
            /* The oldest item is about to go: count it in this item. */
            item.dropped = before + 1u;
        }

        tn_post_drop_oldest(g_p_q_telemetry, &item, &g_tel_q_dropped);
        b_ok = (g_tel_q_dropped == item.dropped);
    }

    return b_ok;
}

/**
 * @brief  Network Task.
 * @param  p_arg  Unused.
 */
void tile_net_task (void * p_arg)
{
    (void)p_arg; /* The task takes no parameter. */

    int const b_bcast = 1; /* DEVIATION D-2: setsockopt takes an int. */

    g_tx_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    /* TELEMETRY is a broadcast; a failure only stops telemetry. */
    (void)setsockopt(g_tx_sock, SOL_SOCKET, SO_BROADCAST, &b_bcast,
                     sizeof(b_bcast));
    g_ack_sock      = tn_ack_socket();
    g_join_start_us = esp_timer_get_time();
    telemetry_book_init(&g_tel_book);
    printf("TN_RX_HOOK %d (1 = lwIP callback, 2 = recvfrom), node %" PRIu8 "\n",
           TN_RX_HOOK, g_node_id);

#if (TN_RX_HOOK == TN_RX_HOOK_LWIP)
    for (;;)
    {
        tn_service(pdMS_TO_TICKS(TN_IDLE_WAIT_MS));
        tn_housekeeping();
    }
#else
    /* DEVIATION D-2: lwIP sockets use an int descriptor. */
    int const sock = tn_rx_socket();

    for (;;)
    {
        uint8_t            buf[TN_RX_BYTES];
        struct sockaddr_in from;
        socklen_t          from_len = sizeof(from);

        /* DEVIATION D-2: recvfrom returns ssize_t. sockaddr_in is the IPv4
         * form of sockaddr, as the sockets API expects. */
        ssize_t const got = recvfrom(sock, buf, sizeof(buf), 0,
                                     (struct sockaddr *)&from, &from_len);
        int64_t const t_l = esp_timer_get_time();

        if (got > 0)
        {
            /* got is 1 to TN_RX_BYTES, so it fits uint32_t. */
            tn_on_datagram(buf, (uint32_t)got, t_l, from.sin_addr.s_addr);
        }

        tn_service(0);
        tn_housekeeping();
    }
#endif
}

/**
 * @brief  NVS, Node ID, netif, WiFi station with storage in RAM and power
 *         save off, and the event handlers (moved from panel_sync.c).
 * @return true when WiFi started.
 */
static bool tn_wifi_start (void)
{
    static char hostname[16];

    esp_err_t err = tn_nvs_start();

    bool const b_node = tn_read_node_id();

    /* The name fits: "ESP-" and at most 3 digits. */
    (void)snprintf(hostname, sizeof(hostname), "ESP-%" PRIu8, g_node_id);

    if (ESP_OK == err)
    {
        err = esp_netif_init();
    }

    if (ESP_OK == err)
    {
        err = esp_event_loop_create_default();
    }

    esp_netif_t * const p_sta = esp_netif_create_default_wifi_sta();

    if ((ESP_OK == err) && (NULL != p_sta))
    {
        /* The name only labels the tile in the router's client list. */
        (void)esp_netif_set_hostname(p_sta, hostname);

        wifi_init_config_t const cfg = WIFI_INIT_CONFIG_DEFAULT();

        err = esp_wifi_init(&cfg);
    }

    if (ESP_OK == err)
    {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                         tn_wifi_event, NULL);
    }

    if (ESP_OK == err)
    {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                         tn_wifi_event, NULL);
    }

    wifi_config_t wifi_cfg;

    (void)memset(&wifi_cfg, 0, sizeof(wifi_cfg)); /* Returns &wifi_cfg. */
    /* ssid and password are uint8_t arrays holding text, so they are
     * written as char; the copy keeps the last byte 0. */
    (void)strncpy((char *)wifi_cfg.sta.ssid, ROUTER_SSID,
                  sizeof(wifi_cfg.sta.ssid) - 1u);
    /* Same for the password. */
    (void)strncpy((char *)wifi_cfg.sta.password, ROUTER_PASS,
                  sizeof(wifi_cfg.sta.password) - 1u);
    wifi_cfg.sta.threshold.authmode =
        ('\0' != ROUTER_PASS[0]) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    wifi_cfg.sta.pmf_cfg.capable = true; /* Joins WPA2/WPA3 mixed routers. */

    if (ESP_OK == err)
    {
        /* WiFi never writes NVS during play (VA-3.3, SRM-3M). */
        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    }

    if (ESP_OK == err)
    {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    }

    if (ESP_OK == err)
    {
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    }

    if (ESP_OK == err)
    {
        err = esp_wifi_start();
    }

    if (ESP_OK == err)
    {
        /* No radio sleep: beacons arrive without 100 ms+ jitter (SRM-8Mc). */
        err = esp_wifi_set_ps(WIFI_PS_NONE);
    }

    printf("tile_net: %s, node %" PRIu8 "%s, WiFi start %s\n", hostname,
           g_node_id,
           b_node ? ""
                  : " (no board number: run "
                    "set_board_number.sh)",
           (ESP_OK == err) ? "ok" : esp_err_to_name(err));

    return (ESP_OK == err);
}

/**
 * @brief  Start NVS; reformat it only when the partition is full or from an
 *         older IDF version. Runs at boot only, before WiFi starts, never
 *         while ARMED or PLAYING (VA-3.2).
 * @return ESP_OK, or the nvs_flash_init() error.
 */
static esp_err_t tn_nvs_start (void)
{
    esp_err_t err = nvs_flash_init();

    if ((ESP_ERR_NVS_NO_FREE_PAGES == err) ||
        (ESP_ERR_NVS_NEW_VERSION_FOUND == err))
    {
        (void)nvs_flash_erase(); /* Failure shows in the init below. */
        err = nvs_flash_init();
    }

    return err;
}

/**
 * @brief  Read the board number written once by set_board_number.sh: the
 *         permanent Node ID (FR9).
 * @return true when it is 1 to 9.
 */
static bool tn_read_node_id (void)
{
    nvs_handle_t handle;
    uint8_t      number = 0u;

    if (ESP_OK == nvs_open("board", NVS_READONLY, &handle))
    {
        /* A missing key leaves number at 0. */
        (void)nvs_get_u8(handle, "number", &number);
        nvs_close(handle);
    }

    g_node_id = number;

    return (number >= 1u) && (number <= PROTO_NODE_MAX);
}

/**
 * @brief  WiFi and IP events (ESP-IDF event task): reconnect after a loss,
 *         tell the Network Task through the event group.
 * @param  p_arg   Unused.
 * @param  base    Event base.
 * @param  event_id  Event id.
 * @param  p_data  Unused.
 */
static void tn_wifi_event (void * p_arg, esp_event_base_t base,
                           int32_t event_id, void * p_data)
{
    (void)p_arg;  /* No context. */
    (void)p_data; /* The address is read from the socket layer. */

    if ((WIFI_EVENT == base) && (WIFI_EVENT_STA_START == event_id))
    {
        (void)esp_wifi_connect(); /* A failure ends in a disconnect event. */
    }
    else if ((WIFI_EVENT == base) && (WIFI_EVENT_STA_DISCONNECTED == event_id))
    {
        /* The returned bits are not needed. */
        (void)xEventGroupClearBits(g_p_events, TN_EV_HAVE_IP);
        (void)esp_wifi_connect(); /* Same. */
    }
    else if ((IP_EVENT == base) && (IP_EVENT_STA_GOT_IP == event_id))
    {
        /* The returned bits are not needed. */
        (void)xEventGroupSetBits(g_p_events, TN_EV_HAVE_IP | TN_EV_NEW_IP);
    }
    else
    {
        /* Other events change nothing here. */
    }
}

/**
 * @brief  One datagram on port 4210 (receive context): a BEACON, or a
 *         command for Playback Control.
 * @param  p_buf     Bytes.
 * @param  len       Length.
 * @param  t_l       Local receive time from the hook.
 * @param  src_addr  Sender address.
 */
static void tn_on_datagram (uint8_t const * p_buf, uint32_t len, int64_t t_l,
                            uint32_t src_addr)
{
    proto_header_t header;

    if (proto_decode_header(p_buf, len, &header))
    {
        /* A decoded type is below 18, a valid proto_type_t. */
        proto_type_t const type = (proto_type_t)header.type;

        if (PROTO_TYPE_BEACON == type)
        {
            tn_on_beacon(p_buf, len, t_l, src_addr);
        }
        else
        {
            tn_on_command(p_buf, len, &header, src_addr);
        }
    }
}

/**
 * @brief  BEACON: count seq gaps, post (t_p, t_l) to the Time Sync Queue
 *         and a record to the Network Task.
 * @param  p_buf     Bytes.
 * @param  len       Length.
 * @param  t_l       Local receive time.
 * @param  src_addr  The Pico's address.
 */
static void tn_on_beacon (uint8_t const * p_buf, uint32_t len, int64_t t_l,
                          uint32_t src_addr)
{
    proto_beacon_t msg;

    if (proto_decode_beacon(p_buf, len, &msg))
    {
        coord_lock_result_t const lock =
            coord_lock_beacon(&g_coord, src_addr, t_l);

        if (COORD_LOCK_SWITCHED == lock)
        {
            g_b_seq_known = false; /* New sender: new seq baseline. */
        }

        /* Another coordinator's beacon is never mixed in. */
        if (COORD_LOCK_FOREIGN != lock)
        {
            if (g_b_seq_known)
            {
                /* Wrap-aware gap: seq is a 16-bit counter. */
                uint16_t const gap = (uint16_t)(msg.header.seq - g_next_seq);

                g_lost += gap;
            }

            g_b_seq_known = true;
            g_next_seq    = (uint16_t)(msg.header.seq + 1u); /* 16-bit wrap. */

            /* time_us_64() on the Pico is below 2^63 us (292,000 years), so the
             * value fits int64_t unchanged. */
            timesync_pair_t const pair = {.t_p = (int64_t)msg.t_p, .t_l = t_l};

            tn_post_drop_oldest(g_p_q_timesync, &pair, &g_dropped);
            g_received++;

            tn_record_t const record = {
                .kind        = TN_REC_BEACON,
                .session_id  = msg.header.session_id,
                .seq         = msg.header.seq,
                .src_addr    = src_addr,
                .t_p         = pair.t_p,
                .t_l         = t_l,
                .received    = g_received,
                .dropped     = g_dropped,
                .lost        = g_lost,
                .log_dropped = g_log_dropped,
                .foreign     = g_coord.foreign,
            };

            tn_post_drop_oldest(g_p_q_log, &record, &g_log_dropped);
        }
    }
}

/**
 * @brief  START_AT, RESUME_AT, PAUSE or STOP: post to the Command Queue, or
 *         reject when it is full and ask the Network Task for a NACK with
 *         reason 2 (VA-3.6). Other types on this port are ignored.
 * @param  p_buf     Bytes.
 * @param  len       Length.
 * @param  p_header  Decoded header.
 * @param  src_addr  Sender address.
 */
static void tn_on_command (uint8_t const * p_buf, uint32_t len,
                           proto_header_t const * p_header, uint32_t src_addr)
{
    tile_net_command_t command;

    /* Commands only from the locked coordinator (same rule as beacons), and
     * none while nothing is locked (R1-5). */
    bool const b_from_coord = coord_lock_command_ok(&g_coord, src_addr);

    if ((b_from_coord) && (tn_decode_command(p_buf, len, p_header, &command)))
    {
        if (0u != uxQueueSpacesAvailable(g_p_q_command))
        {
            /* A slot is free and this context is the only producer. */
            (void)xQueueSend(g_p_q_command, &command, 0);
        }
        else
        {
            tn_record_t const record = {
                .kind        = TN_REC_NACK,
                .session_id  = command.session_id,
                .seq         = command.seq,
                .src_addr    = src_addr,
                .t_p         = 0,
                .t_l         = 0,
                .received    = g_received,
                .dropped     = g_dropped,
                .lost        = g_lost,
                .log_dropped = g_log_dropped,
            };

            fault_word_set(FAULT_BIT_QUEUE_OVERFLOW); /* R1-11. */
            tn_post_drop_oldest(g_p_q_log, &record, &g_log_dropped);
        }
    }
}

/**
 * @brief  Decode one command into the queue item.
 * @param  p_buf      Bytes.
 * @param  len        Length.
 * @param  p_header   Decoded header.
 * @param  p_command  Receives the command.
 * @return true for a valid START_AT, RESUME_AT, PAUSE or STOP.
 */
static bool tn_decode_command (uint8_t const * p_buf, uint32_t len,
                               proto_header_t const * p_header,
                               tile_net_command_t *   p_command)
{
    bool b_ok = false;

    /* A decoded type is below 18, a valid proto_type_t. */
    proto_type_t const type = (proto_type_t)p_header->type;

    (void)memset(p_command, 0, sizeof(*p_command)); /* Returns p_command. */
    p_command->type       = p_header->type;
    p_command->session_id = p_header->session_id;
    p_command->seq        = p_header->seq;

    if (PROTO_TYPE_START_AT == type)
    {
        proto_start_at_t msg;

        b_ok            = proto_decode_start_at(p_buf, len, &msg);
        p_command->t_us = msg.t_start;
    }
    else if (PROTO_TYPE_RESUME_AT == type)
    {
        proto_resume_at_t msg;

        b_ok                   = proto_decode_resume_at(p_buf, len, &msg);
        p_command->t_us        = msg.t_resume;
        p_command->frame_index = msg.frame_index;
    }
    else if (PROTO_TYPE_PAUSE == type)
    {
        proto_command_t msg;

        b_ok = proto_decode_pause(p_buf, len, &msg);
    }
    else if (PROTO_TYPE_STOP == type)
    {
        proto_command_t msg;

        b_ok = proto_decode_stop(p_buf, len, &msg);
    }
    else
    {
        b_ok = false;
    }

    return b_ok;
}

/**
 * @brief  Post an item; when the queue is full, first remove the oldest item
 *         and count it (VA-3.7: xQueueReceive(q, &old, 0) then xQueueSend).
 *         Correct only with a single producer, which the receive context is.
 * @param  p_queue  Queue.
 * @param  p_item   Item of the queue's item size.
 * @param  p_drops  Drop counter.
 */
static void tn_post_drop_oldest (QueueHandle_t p_queue, void const * p_item,
                                 uint32_t * p_drops)
{
    if (0u == uxQueueSpacesAvailable(p_queue))
    {
        tn_any_item_t old_item;

        if (pdTRUE == xQueueReceive(p_queue, &old_item, 0))
        {
            (*p_drops)++;
            fault_word_set(FAULT_BIT_QUEUE_OVERFLOW); /* R1-11. */
        }
    }

    /* With a single producer a slot is free here; a failure is counted. */
    if (pdTRUE != xQueueSend(p_queue, p_item, 0))
    {
        (*p_drops)++;
        fault_word_set(FAULT_BIT_QUEUE_OVERFLOW); /* R1-11. */
    }
}

/**
 * @brief  Network Task: wait up to wait for one record and handle it.
 * @param  wait  Ticks to wait.
 */
static void tn_service (TickType_t wait)
{
    tn_record_t record;

    if (pdTRUE == xQueueReceive(g_p_q_log, &record, wait))
    {
        tn_handle_record(&record);
    }
}

/**
 * @brief  Did the Pico behind this beacon restart on the same address
 *         (R1-1)? Its session_id fell from non-zero to 0, or its seq is more
 *         than TN_BEACON_SEQ_BACK behind the previous beacon's (wrap-aware).
 * @param  p_record  The beacon record.
 * @return true when a restart is likely.
 */
static bool tn_pico_restarted (tn_record_t const * p_record)
{
    bool b_restart = false;

    if (g_b_bcn_seen)
    {
        /* The difference is taken modulo 2^16 and read as signed: a value in
         * -32,768 to 32,767 fits int16_t after the unsigned subtraction. */
        int16_t const delta =
            (int16_t)(uint16_t)(p_record->seq - g_last_bcn_seq);

        b_restart =
            ((0u != g_last_bcn_session) && (0u == p_record->session_id)) ||
            (delta < -TN_BEACON_SEQ_BACK);
    }

    return b_restart;
}

/**
 * @brief  Network Task: one record. A beacon updates the Pico address and
 *         the session, and prints the spread line; a NACK request is sent.
 * @param  p_record  The record.
 */
static void tn_handle_record (tn_record_t const * p_record)
{
    if (TN_REC_BEACON == p_record->kind)
    {
        if ((!g_b_pico_known) || (g_pico_addr != p_record->src_addr))
        {
            g_b_pico_known    = true;
            g_pico_addr       = p_record->src_addr;
            g_b_need_register = true;
        }
        else if (tn_pico_restarted(p_record))
        {
            g_b_need_register = true; /* R1-1: same IP, Pico rebooted. */
        }
        else
        {
            /* Same Pico, no restart seen. */
        }

        g_b_bcn_seen       = true;
        g_last_bcn_session = p_record->session_id;
        g_last_bcn_seq     = p_record->seq;
        g_session          = p_record->session_id;
        printf("BCN,%" PRIu16 ",%" PRId64 ",%" PRId64 "\n", p_record->seq,
               p_record->t_p, p_record->t_l);

        if (0u == (p_record->received % TN_STATS_EVERY))
        {
            printf("TN_STATS received=%" PRIu32 " dropped=%" PRIu32
                   " lost=%" PRIu32 " log_dropped=%" PRIu32 " tel_sent=%" PRIu32
                   " tel_acked=%" PRIu32 " ack_unknown=%" PRIu32
                   " tel_q_dropped=%" PRIu32 " upload_nacks=%" PRIu32
                   " upload_wrong_session=%" PRIu32 " foreign=%" PRIu32 "\n",
                   p_record->received, p_record->dropped, p_record->lost,
                   p_record->log_dropped, g_tel_sent, g_tel_acked,
                   g_ack_unknown, g_tel_status.dropped, g_upload_nacks,
                   g_upload_wrong_session, p_record->foreign);
        }
    }
    else if (TN_REC_NACK == p_record->kind)
    {
        /* NACK reason 2 fits uint8_t. */
        tn_send_nack((uint8_t)PROTO_NACK_COMMAND_QUEUE_FULL,
                     p_record->session_id, p_record->seq, false);
    }
    else
    {
        /* No other kind is produced. */
    }
}

/**
 * @brief  Network Task, every pass: the CONNECTING timeout, REGISTER after
 *         a (re)connect, and the heartbeat schedule.
 */
static void tn_housekeeping (void)
{
    int64_t const     now  = esp_timer_get_time();
    EventBits_t const bits = xEventGroupGetBits(g_p_events);

    if (0u == (bits & TN_EV_HAVE_IP))
    {
        if ((!g_b_wifi_fault) &&
            ((now - g_join_start_us) > TN_CONNECT_TIMEOUT_US))
        {
            g_b_wifi_fault = true;
            fault_word_set(FAULT_BIT_WIFI);
            printf("tile_net: no IP address 10 s after the join started\n");
        }
    }
    else if (0u != (bits & TN_EV_NEW_IP))
    {
        /* The returned bits are not needed. */
        (void)xEventGroupClearBits(g_p_events, TN_EV_NEW_IP);
        g_b_need_register = true;
        g_join_start_us   = now;

        tn_on_new_ip();

        if (!g_b_heap_logged)
        {
            /* T-B3-01: once, after the first IP, when every Week 10 task
             * exists (app_main created them before WiFi could connect). */
            asset_store_probe_heap();
            g_b_heap_logged = true;
        }

        if (g_b_wifi_fault)
        {
            g_b_wifi_fault = false;
            fault_word_clear_condition(FAULT_BIT_WIFI); /* Owner: tile_net. */
        }
    }
    else
    {
        g_join_start_us = now; /* Connected: the timeout restarts on loss. */
    }

    tn_telemetry_service(now);

    if ((g_b_pico_known) && (g_b_need_register))
    {
        tn_send_register();
        g_b_need_register  = false;
        g_b_registered     = true;
        g_register_left    = TN_REGISTER_COPIES - 1u; /* R1-1: more to come. */
        g_next_register_us = now + TN_REGISTER_GAP_US;
        /* Node IDs are 1 to 9 here (tn_send_register checks), so the
         * offset is 0 to 8 x 500 / 9 ms. */
        g_next_beat_us = now + ((int64_t)(g_node_id - 1u) * TN_STAGGER_US);
    }
    else if ((0u != g_register_left) && (now >= g_next_register_us))
    {
        tn_send_register();
        g_register_left--;
        g_next_register_us += TN_REGISTER_GAP_US;
    }
    else if ((g_b_registered) && (now >= g_next_beat_us))
    {
        tn_send_heartbeat(now);
    }
    else
    {
        /* Nothing due. */
    }
}

/**
 * @brief  HEARTBEAT, and SYNC_READY while the OD-10 rule holds; then the
 *         next slot 0.5 s later on an absolute schedule (a late wake-up
 *         skips missed slots instead of sending a burst).
 * @param  now  Current time.
 */
static void tn_send_heartbeat (int64_t now)
{
    uint8_t buf[PROTO_HEARTBEAT_BYTES];

    proto_heartbeat_t const beat = {
        .header  = {.type       = PROTO_TYPE_HEARTBEAT,
                    .session_id = g_session,
                    .seq        = g_tile_seq},
        .node_id = g_node_id,
    };

    g_tile_seq++;
    tn_send(buf, proto_encode_heartbeat(&beat, buf, sizeof(buf)));

    if (timesync_sync_ready())
    {
        proto_sync_ready_t const ready = {
            .header  = {.type       = PROTO_TYPE_SYNC_READY,
                        .session_id = g_session,
                        .seq        = g_tile_seq},
            .node_id = g_node_id,
        };

        g_tile_seq++;
        tn_send(buf, proto_encode_sync_ready(&ready, buf, sizeof(buf)));
    }

    g_next_beat_us += TN_HEARTBEAT_US;

    if (g_next_beat_us <= now)
    {
        g_next_beat_us = now + TN_HEARTBEAT_US;
    }
}

/**
 * @brief  REGISTER {Node ID, cached session_id, cached CRC32}: the asset in
 *         RAM, or 0 and 0 when there is none (T-B3-05).
 */
static void tn_send_register (void)
{
    uint8_t buf[PROTO_REGISTER_BYTES];

    proto_register_t msg = {
        .header            = {.type       = PROTO_TYPE_REGISTER,
                              .session_id = g_session,
                              .seq        = g_tile_seq},
        .node_id           = g_node_id,
        .cached_session_id = 0u,
        .cached_crc32      = 0u,
    };

    /* The asset in RAM, if any, so the Pico can reuse it (T-B3-05). */
    asset_store_cached(&msg.cached_session_id, &msg.cached_crc32);
    g_tile_seq++;

    /* A tile without a valid Node ID cannot register (the encoder refuses
     * Node ID 0 and the send is skipped). */
    tn_send(buf, proto_encode_register(&msg, buf, sizeof(buf)));
    printf("tile_net: REGISTER node %" PRIu8 "\n", g_node_id);
}

/**
 * @brief  NACK: reason 2 (Command Queue full) to the Pico, or an upload
 *         reason (0, 1 or 3) to the PC.
 * @param  reason   proto_nack_reason_t.
 * @param  session  Session of the rejected message.
 * @param  ref_seq  seq of the rejected message.
 * @param  b_to_pc  true: to the PC that sent it; false: to the Pico.
 */
static void tn_send_nack (uint8_t reason, uint8_t session, uint16_t ref_seq,
                          bool b_to_pc)
{
    uint8_t buf[PROTO_NACK_BYTES];

    proto_nack_t const msg = {
        .header  = {.type       = PROTO_TYPE_NACK,
                    .session_id = session,
                    .seq        = g_tile_seq},
        .node_id = g_node_id,
        .reason  = reason,
        .ref_seq = ref_seq,
    };

    uint32_t const len = proto_encode_nack(&msg, buf, sizeof(buf));

    g_tile_seq++;

    if (b_to_pc)
    {
        tn_send_pc(buf, len);
    }
    else
    {
        tn_send(buf, len);
    }
}

/**
 * @brief  Send to the PC at the address and port its last message on port
 *         4214 came from (layout_table.md section 11). Nothing is sent
 *         before the PC has sent anything.
 * @param  p_buf  Bytes.
 * @param  len    Length (0 sends nothing).
 */
static void tn_send_pc (uint8_t const * p_buf, uint32_t len)
{
    if ((0u != len) && (0u != g_pc_addr) && (g_tx_sock >= 0))
    {
        struct sockaddr_in const to = {
            .sin_family      = AF_INET,
            .sin_port        = htons(g_pc_port),
            .sin_addr.s_addr = g_pc_addr,
        };

        /* sockaddr_in is the IPv4 form of sockaddr. A lost reply is repeated
         * when the PC sends the chunk again. */
        (void)sendto(g_tx_sock, p_buf, len, 0, (struct sockaddr const *)&to,
                     sizeof(to));
    }
}

/**
 * @brief  Send to the Pico's port 4211 (Network Task). A length of 0 (an
 *         encoder refused the message) sends nothing.
 * @param  p_buf  Bytes.
 * @param  len    Length.
 */
static void tn_send (uint8_t const * p_buf, uint32_t len)
{
    if ((0u != len) && (g_b_pico_known) && (g_tx_sock >= 0))
    {
        struct sockaddr_in const to = {
            .sin_family      = AF_INET,
            .sin_port        = htons(TN_PICO_PORT),
            .sin_addr.s_addr = g_pico_addr,
        };

        /* sockaddr_in is the IPv4 form of sockaddr. A failed send is
         * repeated by the next heartbeat. */
        (void)sendto(g_tx_sock, p_buf, len, 0, (struct sockaddr const *)&to,
                     sizeof(to));
    }
}

/**
 * @brief  The port 4214 socket, non-blocking, read by the Network Task.
 * @return The socket, or -1.
 */
static int tn_ack_socket (void) /* DEVIATION D-2: lwIP int socket. */
{
    /* DEVIATION D-2: lwIP sockets use an int descriptor. */
    int const sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    struct sockaddr_in const addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(TN_PC_TO_TILE_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    /* sockaddr_in is the IPv4 form of sockaddr, as the sockets API expects. */
    if ((sock < 0) ||
        (0 != bind(sock, (struct sockaddr const *)&addr, sizeof(addr))))
    {
        printf("tile_net: bind %u failed\n", TN_PC_TO_TILE_PORT);
    }
    else
    {
        /* DEVIATION D-2: fcntl takes and returns int flags. */
        int const flags = fcntl(sock, F_GETFL, 0);

        /* A failure leaves the socket blocking; MSG_DONTWAIT still applies. */
        (void)fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    }

    return sock;
}

/**
 * @brief  After each new IP address: the subnet broadcast address for
 *         TELEMETRY, and the first telemetry one period later.
 */
static void tn_on_new_ip (void)
{
    esp_netif_ip_info_t info;
    esp_netif_t * const p_sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if ((NULL != p_sta) && (ESP_OK == esp_netif_get_ip_info(p_sta, &info)))
    {
        /* Both values are in network byte order, so is the result. */
        g_bcast_addr = (info.ip.addr | (~info.netmask.addr));
    }

    g_next_tel_us = esp_timer_get_time() + TILE_NET_TELEMETRY_PERIOD_US;
}

/**
 * @brief  Network Task, every pass: take the newest Display Engine status,
 *         read any TELEMETRY_ACK, and send TELEMETRY when the period is due
 *         (absolute schedule; a late pass skips missed slots, no burst).
 * @param  now  Current time.
 */
static void tn_telemetry_service (int64_t now)
{
    tn_status_item_t item;

    while (pdTRUE == xQueueReceive(g_p_q_telemetry, &item, 0))
    {
        g_tel_status = item;
    }

    tn_poll_pc();
    tn_send_replies();
    tn_comms_sm();

#ifdef TN_TEST_INJECT_FAULTS
    /* Inject only while the bit is clear, so the tile log counts separate
     * faults and the PC's report count must match it (VC-4). */
    if ((0u != g_bcast_addr) && (now >= g_next_inject_us) &&
        (0u == (fault_word_snapshot() & FAULT_BIT_CRC_FAIL)))
    {
        fault_word_set(FAULT_BIT_CRC_FAIL);
        g_injected++;
        g_next_inject_us = now + TN_INJECT_PERIOD_US;
        printf("TN_INJECT n=%" PRIu32 " bit=1\n", g_injected);
    }
#endif

    if ((0u != g_bcast_addr) && (now >= g_next_tel_us))
    {
        tn_telemetry_send();
        g_next_tel_us += TILE_NET_TELEMETRY_PERIOD_US;

        if (g_next_tel_us <= now)
        {
            g_next_tel_us = now + TILE_NET_TELEMETRY_PERIOD_US;
        }
    }
}

/**
 * @brief  TELEMETRY {seq, fault word, orientation, display state} as a subnet
 *         broadcast to port 4215; (seq, sent bits) go into the pending-ACK
 *         ring (VC-4). Bits not yet acknowledged are still set in the word,
 *         so they ride in this packet without extra work.
 */
static void tn_telemetry_send (void)
{
    uint8_t  buf[PROTO_TELEMETRY_BYTES];
    uint16_t seq = g_tile_seq;

    proto_telemetry_t const msg = {
        .header        = {.type       = PROTO_TYPE_TELEMETRY,
                          .session_id = g_session,
                          .seq        = seq},
        .node_id       = g_node_id,
        .fault_word    = fault_word_snapshot(),
        .orientation   = g_tel_status.status.orientation,
        .display_state = g_tel_status.status.display_state,
    };

    uint32_t const len = proto_encode_telemetry(&msg, buf, sizeof(buf));

    g_tile_seq++;

    /* Node ID 0 makes the encoder refuse the message: nothing is sent. */
    if ((0u != len) && (g_tx_sock >= 0))
    {
        struct sockaddr_in const to = {
            .sin_family      = AF_INET,
            .sin_port        = htons(TN_TELEMETRY_PORT),
            .sin_addr.s_addr = g_bcast_addr,
        };

        /* sockaddr_in is the IPv4 form of sockaddr. */
        if (sendto(g_tx_sock, buf, len, 0, (struct sockaddr const *)&to,
                   sizeof(to)) > 0)
        {
            telemetry_book_record(&g_tel_book, seq, msg.fault_word);
            g_tel_sent++;
        }
    }
}

/**
 * @brief  Read the datagrams waiting on port 4214, at most
 *         TN_PC_RX_PER_PASS per pass (Network Task).
 */
static void tn_poll_pc (void)
{
    static uint8_t buf[TN_PC_RX_BYTES]; /* 1 KB: off the task stack. */

    bool     b_more = (g_ack_sock >= 0);
    uint32_t reads  = 0u;

    while ((b_more) && (reads < TN_PC_RX_PER_PASS)) /* R1-9: bounded. */
    {
        reads++;
        struct sockaddr_in from;
        socklen_t          from_len = sizeof(from);

        /* DEVIATION D-2: recvfrom returns ssize_t. sockaddr_in is the IPv4
         * form of sockaddr, as the sockets API expects. */
        ssize_t const got = recvfrom(g_ack_sock, buf, sizeof(buf), MSG_DONTWAIT,
                                     (struct sockaddr *)&from, &from_len);

        b_more = (got > 0);

        if (b_more)
        {
            /* got is 1 to TN_PC_RX_BYTES, so it fits uint32_t. */
            tn_on_pc_datagram(buf, (uint32_t)got, from.sin_addr.s_addr,
                              ntohs(from.sin_port));
        }
    }
}

/**
 * @brief  One datagram from the PC: TELEMETRY_ACK, MANIFEST or ASSET_CHUNK.
 *         Other types on this port are ignored. Only MANIFEST and
 *         ASSET_CHUNK set the reply address, so a TELEMETRY_ACK from
 *         another PC socket (pc/telemetry.py) never redirects upload
 *         replies.
 * @param  p_buf     Bytes.
 * @param  len       Length.
 * @param  src_addr  Sender address, network byte order.
 * @param  src_port  Sender port, host order.
 */
static void tn_on_pc_datagram (uint8_t const * p_buf, uint32_t len,
                               uint32_t src_addr, uint16_t src_port)
{
    proto_header_t header;

    if (proto_decode_header(p_buf, len, &header))
    {
        /* A decoded type is below 18, a valid proto_type_t. */
        proto_type_t const type = (proto_type_t)header.type;

        if (PROTO_TYPE_TELEMETRY_ACK == type)
        {
            proto_telemetry_ack_t ack;

            if (proto_decode_telemetry_ack(p_buf, len, &ack))
            {
                tn_on_telemetry_ack(ack.acked_seq);
            }
        }
        else if ((PROTO_TYPE_MANIFEST == type) ||
                 (PROTO_TYPE_ASSET_CHUNK == type))
        {
            /* The reply address is taken only from an upload that decoded and
             * passed the session check (R1-10). */
            if (tn_on_upload(p_buf, len, &header))
            {
                g_pc_addr = src_addr;
                g_pc_port = src_port;
            }
        }
        else
        {
            /* Not a PC-to-tile message. */
        }
    }
}

/**
 * @brief  MANIFEST or ASSET_CHUNK into the Upload Queue (asset_store);
 *         queue full: reject and NACK reason 3 (VA-3.6).
 * @param  p_buf     Bytes.
 * @param  len       Length.
 * @param  p_header  Decoded header.
 * @return true when the item decoded and passed the session check (posted,
 *         or NACKed because the queue was full); false otherwise.
 */
static bool tn_on_upload (uint8_t const * p_buf, uint32_t len,
                          proto_header_t const * p_header)
{
    static asset_store_upload_t item; /* 1 KB: off the task stack. */

    bool b_valid = false;
    bool b_taken = false;

    (void)memset(&item, 0, sizeof(item)); /* Returns &item. */
    item.session_id = p_header->session_id;
    item.seq        = p_header->seq;

    if (PROTO_TYPE_MANIFEST == p_header->type)
    {
        proto_manifest_t msg;

        b_valid   = proto_decode_manifest(p_buf, len, &msg);
        item.kind = (uint8_t)ASSET_STORE_UP_MANIFEST; /* Enum value fits. */
        item.format_version = msg.format_version;
        item.frame_count    = msg.frame_count;
        item.crc32          = msg.crc32;
    }
    else
    {
        proto_asset_chunk_t msg;

        b_valid = proto_decode_asset_chunk(p_buf, len, &msg);

        if (b_valid)
        {
            item.kind = (uint8_t)ASSET_STORE_UP_CHUNK; /* Enum value fits. */
            item.chunk_index = msg.chunk_index;
            item.payload_len = msg.payload_len;
            /* Returns item.payload. payload_len is 1 to 1,024 (decoder). */
            (void)memcpy(item.payload, msg.p_payload, msg.payload_len);
        }
    }

    if ((b_valid) && (0u != g_session) && (item.session_id != g_session))
    {
        /* T-X-08 case 3: an upload for another session than the one the
         * beacons carry is rejected. The NACK reason list has no session
         * code (layout_table.md section 10 item 8), so it is dropped and
         * counted; the PC sees no CHUNK_ACK and the Pico no ASSET_READY.
         * Session 0 (no session configured on the Pico yet) still accepts
         * uploads, so the bench upload tests run without the dashboard;
         * R1-10's nonzero-session rule waits for an owner decision. */
        g_upload_wrong_session++;
    }
    else if ((b_valid) && (!asset_store_post_upload(&item)))
    {
        b_taken = true;
        g_upload_nacks++;
        fault_word_set(FAULT_BIT_QUEUE_OVERFLOW); /* R1-11. */
        /* NACK reason 3 fits uint8_t. */
        tn_send_nack((uint8_t)PROTO_NACK_UPLOAD_QUEUE_FULL, item.session_id,
                     item.seq, true);
    }
    else
    {
        /* Posted, or not a valid MANIFEST or ASSET_CHUNK. */
        b_taken = b_valid;
    }

    return b_taken;
}

/**
 * @brief  Send every reply asset_store has queued: CHUNK_ACK, BUSY and NACK
 *         to the PC, ASSET_READY to the Pico.
 */
static void tn_send_replies (void)
{
    asset_store_reply_t reply;

    while (asset_store_take_reply(&reply))
    {
        uint8_t buf[PROTO_ASSET_READY_BYTES];

        if (ASSET_STORE_REPLY_NACK == reply.kind)
        {
            tn_send_nack(reply.reason, reply.session_id, reply.ref_seq, true);
        }
        else
        {
            proto_header_t const header = {
                .type = 0u, .session_id = reply.session_id, .seq = g_tile_seq};

            g_tile_seq++;

            if (ASSET_STORE_REPLY_CHUNK_ACK == reply.kind)
            {
                proto_chunk_ack_t msg = {.header      = header,
                                         .node_id     = g_node_id,
                                         .chunk_index = reply.chunk_index};

                msg.header.type =
                    (uint8_t)PROTO_TYPE_CHUNK_ACK; /* Enum value fits. */
                tn_send_pc(buf, proto_encode_chunk_ack(&msg, buf, sizeof(buf)));
            }
            else if (ASSET_STORE_REPLY_BUSY == reply.kind)
            {
                proto_busy_t msg = {.header  = header,
                                    .node_id = g_node_id,
                                    .ref_seq = reply.ref_seq};

                msg.header.type =
                    (uint8_t)PROTO_TYPE_BUSY; /* Enum value fits. */
                tn_send_pc(buf, proto_encode_busy(&msg, buf, sizeof(buf)));
            }
            else
            {
                proto_asset_ready_t msg = {.header  = header,
                                           .node_id = g_node_id,
                                           .crc32   = reply.crc32};

                msg.header.type =
                    (uint8_t)PROTO_TYPE_ASSET_READY; /* Enum value fits. */
                tn_send(buf, proto_encode_asset_ready(&msg, buf, sizeof(buf)));
                printf("tile_net: ASSET_READY crc=0x%08" PRIx32 "\n",
                       reply.crc32);
            }
        }
    }
}

/**
 * @brief  Comms statechart (W6 statechart 3, T-B3-05), derived each pass
 *         from what each owner publishes: the IP event bits and the
 *         CONNECTING timeout (this task), REGISTER (this task), the upload
 *         phase (asset_store) and the playback state (Playback Control).
 *         A change is logged as "TN_COMMS <state>".
 */
static void tn_comms_sm (void)
{
    static char const * const names[TILE_NET_COMMS_COUNT] = {
        "BOOTING",          "CONNECTING TO WIFI",
        "REGISTERING TILE", "WAITING FOR ASSET",
        "RECEIVING ASSET",  "VERIFYING ASSET",
        "ASSET READY",      "SUPPORTING PLAYBACK",
        "RECONNECTING",     "FAULT"};

    playback_control_state_t const play  = playback_control_state();
    asset_store_phase_t const      phase = asset_store_phase();
    bool const                     b_ip  = tile_net_has_ip();
    tile_net_comms_t               next  = TILE_NET_COMMS_WAITING;

    if (b_ip)
    {
        g_b_had_ip = true;
    }

    if (g_b_wifi_fault)
    {
        next = TILE_NET_COMMS_FAULT;
    }
    else if (!b_ip)
    {
        next = g_b_had_ip ? TILE_NET_COMMS_RECONNECTING
                          : TILE_NET_COMMS_CONNECTING;
    }
    else if (!g_b_registered)
    {
        next = TILE_NET_COMMS_REGISTERING;
    }
    else if ((PLAYBACK_ARMED == play) || (PLAYBACK_PLAYING == play) ||
             (PLAYBACK_PAUSED == play) || (PLAYBACK_CLOCK_HOLDOVER == play))
    {
        next = TILE_NET_COMMS_SUPPORTING_PLAYBACK;
    }
    else if (ASSET_STORE_RECEIVING == phase)
    {
        next = TILE_NET_COMMS_RECEIVING;
    }
    else if (ASSET_STORE_VERIFYING == phase)
    {
        next = TILE_NET_COMMS_VERIFYING;
    }
    else if (ASSET_STORE_READY == phase)
    {
        next = TILE_NET_COMMS_ASSET_READY;
    }
    else
    {
        /* Registered, no upload yet: WAITING FOR ASSET. */
    }

    if ((uint32_t)next != g_comms) /* Enum value fits. */
    {
        g_comms = (uint32_t)next; /* Enum value fits. */
        printf("TN_COMMS %s\n", names[next]);
    }
}

/**
 * @brief  TELEMETRY_ACK: clear (sent bits AND FAULT_EVENT_MASK) of that
 *         packet with an atomic fetch-and (VC-4). Condition bits are never
 *         cleared here. An unknown, expired or repeated seq changes nothing.
 * @param  acked_seq  seq of the acknowledged TELEMETRY.
 */
static void tn_on_telemetry_ack (uint16_t acked_seq)
{
    uint32_t sent_bits = 0u;

    if (telemetry_book_ack(&g_tel_book, acked_seq, &sent_bits))
    {
        fault_word_clear_events(sent_bits);
        g_tel_acked++;
    }
    else
    {
        g_ack_unknown++;
    }
}

#if (TN_RX_HOOK == TN_RX_HOOK_LWIP)
/**
 * @brief  Bind the raw lwIP pcb to port 4210 (runs in the tcpip task).
 * @param  p_ctx  Unused.
 */
static void tn_lwip_setup (void * p_ctx)
{
    (void)p_ctx; /* tcpip_callback() passes NULL. */

    struct udp_pcb * const p_pcb = udp_new();

    if (NULL != p_pcb)
    {
        if (ERR_OK == udp_bind(p_pcb, IP_ANY_TYPE, TN_SYNC_PORT))
        {
            udp_recv(p_pcb, tn_lwip_recv, NULL);
        }
        else
        {
            printf("tile_net: udp_bind %u failed\n", TN_SYNC_PORT);
        }
    }
}

/**
 * @brief  OD-09 option (a): lwIP receive callback in the tcpip task. t_l is
 *         the first thing taken.
 * @param  p_arg   Unused.
 * @param  p_pcb   Unused.
 * @param  p_pbuf  Received datagram; freed here.
 * @param  p_addr  Sender.
 * @param  port    Unused.
 */
static void tn_lwip_recv (void * p_arg, struct udp_pcb * p_pcb,
                          struct pbuf * p_pbuf, ip_addr_t const * p_addr,
                          u16_t port)
{
    int64_t const t_l = esp_timer_get_time();
    uint8_t       buf[TN_RX_BYTES];

    (void)p_arg; /* The callback takes no context. */
    (void)p_pcb; /* Only one pcb exists. */
    (void)port;  /* Any source port. */

    if (NULL != p_pbuf)
    {
        /* pbuf_copy_partial returns at most TN_RX_BYTES, a u16_t count. */
        uint32_t const len =
            (uint32_t)pbuf_copy_partial(p_pbuf, buf, TN_RX_BYTES, 0);
        uint32_t const src = ip4_addr_get_u32(ip_2_ip4(p_addr));

        /* A datagram longer than TN_RX_BYTES is passed as TN_RX_BYTES, which
         * no row has, so the decoder sees the wrong length. */
        tn_on_datagram(buf, (p_pbuf->tot_len > TN_RX_BYTES) ? TN_RX_BYTES : len,
                       t_l, src);
        (void)pbuf_free(p_pbuf); /* Returns the number of pbufs freed. */
    }
}
#else
/**
 * @brief  OD-09 option (b): the receive socket on port 4210 with a 20 ms
 *         timeout, so the Network Task also runs its housekeeping.
 * @return The socket.
 */
static int tn_rx_socket (void) /* DEVIATION D-2: lwIP int socket. */
{
    /* DEVIATION D-2: lwIP sockets use an int descriptor. */
    int const sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    struct sockaddr_in const addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(TN_SYNC_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    struct timeval const wait = {.tv_sec = 0, .tv_usec = 20000};

    /* sockaddr_in is the IPv4 form of sockaddr, as the sockets API expects. */
    if (0 != bind(sock, (struct sockaddr const *)&addr, sizeof(addr)))
    {
        printf("tile_net: bind %u failed\n", TN_SYNC_PORT);
    }

    /* A failure only makes the housekeeping wait for the next datagram. */
    (void)setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &wait, sizeof(wait));

    return sock;
}
#endif

/*** end of file ***/
