/**
 * @file    tile_net.c
 * @brief   Tile Network Task (T-B3-03): BEACON receive and the Time Sync
 *          Queue.
 *
 * Minimal implementation by Buddy 4 for T-B4-03. Owner: Cheston, task
 * T-B3-03. Implemented and not yet implemented: see tile_net.h.
 * PROVISIONAL choices: see tile_net.h.
 *
 * DEVIATION D-2 (module-wide): ESP-IDF, FreeRTOS and lwIP names, types and
 * macros (QueueHandle_t, pdTRUE, portMAX_DELAY, struct udp_pcb, struct pbuf,
 * ip_addr_t, u16_t, err_t, the int socket descriptor and ssize_t).
 */
#include "tile_net.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "freertos/task.h"
#include "esp_timer.h"
#include "lwip/pbuf.h"
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "lwip/udp.h"

#include "proto.h"
#include "tile_tasks.h"
#include "timesync.h"

/** UDP port of the beacon broadcast (unchanged from the prototype). */
#define TN_SYNC_PORT 4210u

/** Spread-log queue length (plan choice: 1.6 s of beacons at 10 Hz, so a
 *  slow console never stalls the receive path). */
#define TN_LOG_QUEUE_LEN 16u

/** Counter line every 100 beacons (10 s, plan choice). */
#define TN_STATS_EVERY 100u

/** Receive buffer: one byte more than a beacon, so a longer datagram is
 *  seen as the wrong length. */
#define TN_RX_BYTES (PROTO_BEACON_BYTES + 1u)

/** One spread-log record. */
typedef struct
{
    uint16_t seq;
    int64_t  t_p;
    int64_t  t_l;
} tn_log_record_t;

/** Scratch item large enough for either queue, for the drop-oldest read. */
typedef union
{
    timesync_pair_t pair;
    tn_log_record_t record;
} tn_any_item_t;

static QueueHandle_t    g_p_q_timesync = NULL;
static QueueHandle_t    g_p_q_log      = NULL;
static tile_net_stats_t g_stats;
static uint32_t         g_log_dropped = 0u;
static uint16_t         g_next_seq    = 0u;
static bool             g_b_seq_known = false;

static void tn_on_beacon (uint8_t const * p_buf, uint32_t len, int64_t t_l);
static void tn_post_drop_oldest (QueueHandle_t p_queue, void const * p_item,
                                 uint32_t * p_drops);
static void tn_print_log (void);

#if (TN_RX_HOOK == TN_RX_HOOK_LWIP)
static void tn_lwip_setup (void * p_ctx);
static void tn_lwip_recv (void * p_arg, struct udp_pcb * p_pcb,
                          struct pbuf * p_pbuf, ip_addr_t const * p_addr,
                          u16_t port);
#else
static void tn_recvfrom_loop (void);
#endif

/**
 * @brief  Create the queues and the Network Task.
 * @return true when everything was created.
 */
bool tile_net_start (void)
{
    bool b_ok = false;

    g_p_q_timesync =
        xQueueCreate(TILE_NET_TIMESYNC_QUEUE_LEN, sizeof(timesync_pair_t));
    g_p_q_log = xQueueCreate(TN_LOG_QUEUE_LEN, sizeof(tn_log_record_t));

    if ((NULL != g_p_q_timesync) && (NULL != g_p_q_log))
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
 * @brief  Copy of the beacon receive counters. A copy may mix counts from two
 *         beacons; it is for logs only.
 * @param  p_stats  Receives the counters.
 */
void tile_net_get_stats (tile_net_stats_t * p_stats)
{
    *p_stats = g_stats;
}

/**
 * @brief  Network Task.
 * @param  p_arg  Unused.
 */
void tile_net_task (void * p_arg)
{
    (void)p_arg; /* The task takes no parameter. */

    printf("TN_RX_HOOK %d (1 = lwIP callback, 2 = recvfrom)\n", TN_RX_HOOK);

#if (TN_RX_HOOK == TN_RX_HOOK_LWIP)
    for (;;)
    {
        tn_print_log();
    }
#else
    tn_recvfrom_loop();
#endif
}

/**
 * @brief  Handle one received datagram on the beacon port: decode, count seq
 *         gaps, post (t_p, t_l) to the Time Sync Queue and queue a log line.
 *         Runs in the tcpip task (option (a)) or the Network Task (option
 *         (b)); in both builds it is the only producer of both queues.
 * @param  p_buf  Received bytes.
 * @param  len    Received length, bytes.
 * @param  t_l    Local receive time taken at the hook, microseconds.
 */
static void tn_on_beacon (uint8_t const * p_buf, uint32_t len, int64_t t_l)
{
    proto_beacon_t msg;

    if (proto_decode_beacon(p_buf, len, &msg))
    {
        if (g_b_seq_known)
        {
            /* Wrap-aware gap: seq is a 16-bit counter. */
            uint16_t const gap = (uint16_t)(msg.header.seq - g_next_seq);

            g_stats.lost += gap;
        }

        g_b_seq_known = true;
        g_next_seq    = (uint16_t)(msg.header.seq + 1u); /* 16-bit wrap. */

        /* time_us_64() on the Pico is below 2^63 us (292,000 years), so the
         * value fits int64_t unchanged. */
        timesync_pair_t const pair   = {.t_p = (int64_t)msg.t_p, .t_l = t_l};
        tn_log_record_t const record = {
            .seq = msg.header.seq, .t_p = pair.t_p, .t_l = t_l};

        tn_post_drop_oldest(g_p_q_timesync, &pair, &g_stats.dropped);
        g_stats.received++;
        tn_post_drop_oldest(g_p_q_log, &record, &g_log_dropped);
    }
}

/**
 * @brief  Post an item; when the queue is full, first remove the oldest item
 *         and count it (VA-3.7: xQueueReceive(q, &old, 0) then xQueueSend).
 *         Correct only with a single producer, which tn_on_beacon() is.
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
        }
    }

    /* With a single producer a slot is free here; a failure is counted. */
    if (pdTRUE != xQueueSend(p_queue, p_item, 0))
    {
        (*p_drops)++;
    }
}

/**
 * @brief  Wait for one spread-log record and print it; every 100 beacons
 *         print the counters.
 */
static void tn_print_log (void)
{
    tn_log_record_t record;

    if (pdTRUE == xQueueReceive(g_p_q_log, &record, portMAX_DELAY))
    {
        printf("BCN,%" PRIu16 ",%" PRId64 ",%" PRId64 "\n", record.seq,
               record.t_p, record.t_l);

        if (0u == (g_stats.received % TN_STATS_EVERY))
        {
            printf("TN_STATS received=%" PRIu32 " dropped=%" PRIu32
                   " lost=%" PRIu32 " log_dropped=%" PRIu32 "\n",
                   g_stats.received, g_stats.dropped, g_stats.lost,
                   g_log_dropped);
        }
    }
}

#if (TN_RX_HOOK == TN_RX_HOOK_LWIP)
/**
 * @brief  Bind the raw lwIP pcb to the beacon port (runs in the tcpip task).
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
 * @param  p_addr  Unused.
 * @param  port    Unused.
 */
static void tn_lwip_recv (void * p_arg, struct udp_pcb * p_pcb,
                          struct pbuf * p_pbuf, ip_addr_t const * p_addr,
                          u16_t port)
{
    int64_t const t_l = esp_timer_get_time();
    uint8_t       buf[TN_RX_BYTES];

    (void)p_arg;  /* The callback takes no context. */
    (void)p_pcb;  /* Only one pcb exists. */
    (void)p_addr; /* The sender is not checked yet (T-B2-04). */
    (void)port;   /* Any source port. */

    if (NULL != p_pbuf)
    {
        /* pbuf_copy_partial returns at most TN_RX_BYTES, a u16_t count. */
        uint32_t const len =
            (uint32_t)pbuf_copy_partial(p_pbuf, buf, TN_RX_BYTES, 0);

        if (p_pbuf->tot_len > TN_RX_BYTES)
        {
            /* Too long for a beacon: decode sees the wrong length. */
            tn_on_beacon(buf, TN_RX_BYTES, t_l);
        }
        else
        {
            tn_on_beacon(buf, len, t_l);
        }

        (void)pbuf_free(p_pbuf); /* Returns the number of pbufs freed. */
    }
}
#else
/**
 * @brief  OD-09 option (b): blocking recvfrom in the Network Task; t_l is
 *         taken as soon as recvfrom returns. Prints each log line after the
 *         beacon is handled.
 */
static void tn_recvfrom_loop (void)
{
    /* DEVIATION D-2: lwIP sockets use an int descriptor. */
    int const sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(TN_SYNC_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };

    /* sockaddr_in is the IPv4 form of sockaddr, as the sockets API expects. */
    if (0 != bind(sock, (struct sockaddr *)&addr, sizeof(addr)))
    {
        printf("tile_net: bind %u failed\n", TN_SYNC_PORT);
    }

    for (;;)
    {
        uint8_t buf[TN_RX_BYTES];

        /* DEVIATION D-2: recvfrom returns ssize_t. */
        ssize_t const got = recvfrom(sock, buf, sizeof(buf), 0, NULL, NULL);
        int64_t const t_l = esp_timer_get_time();

        if (got > 0)
        {
            /* got is 1 to TN_RX_BYTES, so it fits uint32_t. */
            tn_on_beacon(buf, (uint32_t)got, t_l);
        }

        while (0u != uxQueueMessagesWaiting(g_p_q_log))
        {
            tn_print_log();
        }
    }
}
#endif

/*** end of file ***/
