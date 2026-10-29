/**
 * @file    coordinator_loop.h
 * @brief   Pico W coordinator main loop (T-B2-02): poll-mode cyw43/lwIP and
 *          the BEACON broadcast on an absolute 100 ms schedule.
 *
 * Minimal implementation by Buddy 4 for T-B4-03. Owner: Benjamin, task
 * T-B2-02. Implemented: coordinator_loop_run() polling cyw43 every pass,
 * cl_send_beacon() on next_beacon_us += 100,000 us with t_p read just before
 * udp_sendto, the beacon lateness histogram and a per-beacon send log.
 * Not yet implemented: the OD-01 citation in this header, the registry and
 * heartbeat scan (T-B2-03, cl_scan_heartbeats(), cl_connect_router(),
 * router-loss rejoin), session handling (T-B2-04) and commands (T-B2-05).
 *
 * PROVISIONAL (OD-01, owner Benjamin): poll mode, the plan's first option,
 * pending the Pico SDK citation. SDK 2.3.1 cyw43_arch.h line 56 says poll
 * mode "requires the user to call cyw43_arch_poll periodically from their
 * main loop"; Week10_Kickoff/decision_briefs.md OD-01 records that no SDK
 * sentence states where callbacks run in poll mode.
 * PROVISIONAL (T-B2-04, owner Benjamin): session_id 0 in every beacon until
 * sessions exist.
 * PROVISIONAL (plan names): the plan's cl_lateness_hist[] is the static
 * g_lateness_hist[] read through coordinator_loop_lateness_bin() (Barr-C
 * 7.1.j and 4.2.c).
 *
 * DEVIATION D-2 (module-wide): Pico SDK and lwIP names and types
 * (struct udp_pcb, ip_addr_t, struct pbuf, err_t, PBUF_RAM).
 */
#ifndef COORDINATOR_LOOP_H
#define COORDINATOR_LOOP_H

#include <stdint.h>

#include "lwip/ip_addr.h"
#include "lwip/udp.h"

/** Beacon period: 100 ms, 10 Hz (T-B2-02, VA-2.2). */
#define CL_BEACON_PERIOD_US 100000u

/** Lateness histogram: 1 ms bins 0 to 9 ms, and one bin for 10 ms or more
 *  (plan choice: the pass line is 5 ms, NFR16 proposed). */
#define CL_LATE_BINS 11u

/**
 * @brief  Run the coordinator loop forever: poll cyw43 and lwIP every pass
 *         and send a BEACON whenever the absolute schedule is due.
 * @param  p_tx     UDP pcb used to send.
 * @param  p_bcast  Subnet broadcast address.
 */
void coordinator_loop_run (struct udp_pcb * p_tx, ip_addr_t const * p_bcast);

/**
 * @brief  Count in one lateness bin.
 * @param  bin  0 to CL_LATE_BINS - 1.
 * @return Number of beacons sent with that lateness, or 0 for a bin out of
 *         range.
 */
uint32_t coordinator_loop_lateness_bin (uint32_t bin);

#endif /* COORDINATOR_LOOP_H */

/*** end of file ***/
