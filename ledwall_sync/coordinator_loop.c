/**
 * @file    coordinator_loop.c
 * @brief   Pico W coordinator main loop (T-B2-02).
 *
 * Minimal implementation by Buddy 4 for T-B4-03. Owner: Benjamin, task
 * T-B2-02. Implemented and not yet implemented: see coordinator_loop.h.
 * PROVISIONAL choices: see coordinator_loop.h.
 *
 * DEVIATION D-2 (module-wide): Pico SDK and lwIP names and types.
 */
#include "coordinator_loop.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/pbuf.h"

#include "proto.h"

/** UDP port of the beacon broadcast (unchanged from the prototype). */
#define CL_SYNC_PORT 4210u

/** session_id carried until T-B2-04 sessions exist (PROVISIONAL). */
#define CL_SESSION_NONE 0u

/** Width of one lateness bin, microseconds. */
#define CL_LATE_BIN_US 1000u

/** Histogram line printed every 100 beacons (10 s, plan choice). */
#define CL_HIST_EVERY 100u

static uint32_t g_lateness_hist[CL_LATE_BINS];

static uint64_t cl_send_beacon (struct udp_pcb *  p_tx,
                                ip_addr_t const * p_bcast, uint16_t seq);
static void     cl_record_lateness (uint64_t late_us);
static void     cl_print_histogram (void);

/**
 * @brief  Run the coordinator loop forever.
 * @param  p_tx     UDP pcb used to send.
 * @param  p_bcast  Subnet broadcast address.
 */
void coordinator_loop_run (struct udp_pcb * p_tx, ip_addr_t const * p_bcast)
{
    uint64_t next_beacon_us = time_us_64() + CL_BEACON_PERIOD_US;
    uint16_t seq            = 0u;
    uint32_t sent           = 0u;

    for (;;)
    {
        /* Poll mode: lwIP and cyw43 work, including every receive callback,
         * runs here in the main loop and nowhere else. */
        cyw43_arch_poll();

        if (time_us_64() >= next_beacon_us)
        {
            uint64_t const t_p     = cl_send_beacon(p_tx, p_bcast, seq);
            uint64_t const late_us = t_p - next_beacon_us;

            cl_record_lateness(late_us);
            printf("BEACON seq=%" PRIu16 " t_p=%" PRIu64 " late_us=%" PRIu64
                   "\n",
                   seq, t_p, late_us);

            /* Absolute schedule: the period never stretches with load. */
            next_beacon_us += CL_BEACON_PERIOD_US;
            seq++;
            sent++;

            if (0u == (sent % CL_HIST_EVERY))
            {
                cl_print_histogram();
            }
        }
    }
}

/**
 * @brief  Count in one lateness bin.
 * @param  bin  0 to CL_LATE_BINS - 1.
 * @return The count, or 0 for a bin out of range.
 */
uint32_t coordinator_loop_lateness_bin (uint32_t bin)
{
    uint32_t count = 0u;

    if (bin < CL_LATE_BINS)
    {
        count = g_lateness_hist[bin];
    }

    return count;
}

/**
 * @brief  Encode and broadcast one BEACON. Everything except t_p is encoded
 *         first; t_p = time_us_64() is then read and written into the
 *         encoded bytes, and udp_sendto follows with no other work between
 *         (T-B2-02).
 * @param  p_tx     UDP pcb used to send.
 * @param  p_bcast  Subnet broadcast address.
 * @param  seq      Beacon sequence number.
 * @return The t_p that was sent, microseconds.
 */
static uint64_t cl_send_beacon (struct udp_pcb *  p_tx,
                                ip_addr_t const * p_bcast, uint16_t seq)
{
    uint64_t t_p = 0u;

    proto_beacon_t const msg = {
        .header = {.type = 0u, .session_id = CL_SESSION_NONE, .seq = seq},
        .t_p    = 0u,
    };

    struct pbuf * const p_pbuf =
        pbuf_alloc(PBUF_TRANSPORT, PROTO_BEACON_BYTES, PBUF_RAM);

    if (NULL != p_pbuf)
    {
        /* PBUF_RAM gives one contiguous payload of the requested size. */
        uint8_t * const p_payload = (uint8_t *)p_pbuf->payload;

        /* The payload is exactly PROTO_BEACON_BYTES, so the length is known. */
        (void)proto_encode_beacon(&msg, p_payload, PROTO_BEACON_BYTES);
        t_p = time_us_64();
        proto_beacon_set_t_p(p_payload, t_p);
        /* A failed send shows as a seq gap on the tiles (T-B3-03 count). */
        (void)udp_sendto(p_tx, p_pbuf, p_bcast, CL_SYNC_PORT);
        (void)pbuf_free(p_pbuf); /* Returns the number of pbufs freed. */
    }
    else
    {
        t_p = time_us_64();
        printf("BEACON seq=%" PRIu16 " pbuf_alloc failed\n", seq);
    }

    return t_p;
}

/**
 * @brief  Add one lateness to the histogram.
 * @param  late_us  Send time minus the scheduled time, microseconds.
 */
static void cl_record_lateness (uint64_t late_us)
{
    uint64_t bin = late_us / CL_LATE_BIN_US;

    if (bin >= CL_LATE_BINS)
    {
        bin = CL_LATE_BINS - 1u;
    }

    g_lateness_hist[bin]++;
}

/**
 * @brief  Print the lateness histogram as one CSV line: bins 0 to 9 ms, then
 *         10 ms or more.
 */
static void cl_print_histogram (void)
{
    printf("LATE_HIST");

    for (uint32_t bin = 0u; bin < CL_LATE_BINS; bin++)
    {
        printf(",%" PRIu32, g_lateness_hist[bin]);
    }

    printf("\n");
}

/*** end of file ***/
