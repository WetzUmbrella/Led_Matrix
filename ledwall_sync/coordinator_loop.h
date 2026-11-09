/**
 * @file    coordinator_loop.h
 * @brief   Pico W coordinator (T-B2-02 to T-B2-05): one poll-mode main loop
 *          that sends the BEACON on an absolute 100 ms schedule, keeps the
 *          9-node registry and heartbeat scan, runs the W6 statechart 2
 *          session and readiness logic, and sends the timed commands.
 *
 * Owner: Benjamin (Buddy 2), tasks T-B2-02, T-B2-03, T-B2-04 and T-B2-05.
 * Implemented by Ahsanul (B4) for Benjamin; T-B2-03 to T-B2-05 written on
 * 29 Oct 2026 in Ahsanul's working copy for Benjamin to review and defend.
 *
 * Implemented:
 * - T-B2-02: cyw43_arch_poll() every pass; BEACON {session_id, seq, t_p}
 *   on next_beacon_us += 100,000 us with t_p read immediately before
 *   udp_sendto; the lateness histogram; a missed slot after a stall is
 *   skipped and counted, never sent late in a burst.
 * - T-B2-03: receive callbacks that only timestamp, copy into a ring and
 *   return (cl_rx_cb()); one buffered message dispatched per loop pass, only
 *   when 1 ms is left before the next beacon (OD-08); registry of 9 Node IDs
 *   (cl_registry[9]); heartbeat scan every 100 ms on its own absolute
 *   schedule with the 2.5 s timeout; router join bounded by the 10 s
 *   CONNECTING timeout, retried forever on the 1, 2, 4, 8, 10 s backoff of
 *   join_backoff.h while in FAULT, and rejoined the same way after a link
 *   loss (cl_try_join(), cl_on_link_lost()).
 * - T-B2-04: the 12 states of W6 statechart 2; slots only from
 *   SESSION_CONFIG; one readiness bitmap per session written only in poll
 *   context; ASSET_READY counted only for the session and the manifest
 *   CRC32 of that Node ID; stale session_id or seq dropped and counted;
 *   STATUS every 250 ms on an absolute schedule; Start only when every
 *   required tile has sent ASSET_READY and SYNC_READY; continue and pause
 *   policies; the 5 s recovery timeout; rejoin per OD-14 option (a).
 * - T-B2-05: START_AT and RESUME_AT 600 ms ahead, PAUSE and STOP, each sent
 *   3 times at 0, 100 and 200 ms with one seq; STOP from PAUSED.
 * - T-B2-06 instrumentation: callback and dispatch time histograms.
 * Not yet implemented: CL_TEST_SUPPRESS_MS is a build option for T-B4-05
 *   (see coordinator_loop.c); the nine-sender load itself is
 *   pc/tools/sim_nodes.py (T-B2-06).
 *
 * OD-01 (owner Benjamin), PROVISIONAL: poll mode. Citation, Raspberry Pi
 * Pico SDK 2.3.1 (the CI version), src/rp2_common/pico_cyw43_arch/include/
 * pico/cyw43_arch.h line 56: poll mode "requires the user to call
 * cyw43_arch_poll periodically from their main loop"; lines 98 to 99: in
 * threadsafe background mode "lwIP callbacks happen in a (low priority) IRQ
 * context"; src/rp2_common/pico_async_context/include/pico/
 * async_context_poll.h line 31: the poll context "processes asynchronous
 * work by polling rather than using interrupts or threads". No SDK sentence
 * states where callbacks run in poll mode, so cl_rx_cb() records the
 * largest __get_current_exception() value it sees (CL_STATS exc_max); the
 * T-B2-02 bench run passes OD-01 when it reads 0 (decision_briefs.md OD-01).
 * PROVISIONAL (owner Benjamin): ports, seq streams and the C to P commands
 * of protocol/layout_table.md sections 10 and 11; the 32-entry receive ring
 * and the 4 command send slots (plan choice, sized in coordinator_loop.c).
 *
 * DEVIATION D-2 (module-wide): Pico SDK and lwIP names and types.
 */
#ifndef COORDINATOR_LOOP_H
#define COORDINATOR_LOOP_H

#include <stdbool.h>
#include <stdint.h>

#include "proto.h"

/** Beacon period: 100 ms, 10 Hz (T-B2-02, VA-2.2). */
#define CL_BEACON_PERIOD_US 100000u

/** Lateness histogram: 1 ms bins 0 to 9 ms, and one bin for 10 ms or more
 *  (plan choice: the pass line is 5 ms, NFR16 proposed). */
#define CL_LATE_BINS 11u

/** Registry size: Node IDs 1 to 9 (T-B2-03, VC-11b). */
#define CL_MAX_NODES 9u

/** UDP ports (PROVISIONAL, layout_table.md section 11). */
#define CL_PORT_TILE_RX   4210u /**< Tiles listen: beacons and commands. */
#define CL_PORT_FROM_TILE 4211u /**< Pico listens: tile messages. */
#define CL_PORT_FROM_PC   4212u /**< Pico listens: SESSION_CONFIG, commands. */

/**
 * @brief  Join the router and run the coordinator forever. Never returns.
 *         A failed join stays in FAULT and is retried with backoff; a
 *         joined coordinator leaves FAULT and starts from WAITING FOR
 *         SESSION.
 */
void coordinator_loop_run (void);

/**
 * @brief  Count in one lateness bin.
 * @param  bin  0 to CL_LATE_BINS - 1.
 * @return Number of beacons sent with that lateness, or 0 for a bin out of
 *         range.
 */
uint32_t coordinator_loop_lateness_bin (uint32_t bin);

#ifdef CL_HOST_TEST
/*
 * Host test surface (ledwall_sync/test). The clock and the send function
 * come from ledwall_sync/test/coordinator_port.c; every other function is
 * the firmware code.
 */

/**
 * @brief  Reset to the state just after the router join, at the test
 *         clock's time.
 * @param  bcast_addr  Subnet broadcast address.
 */
void coordinator_loop_test_reset (uint32_t bcast_addr);

/**
 * @brief  A datagram arrives, as from cl_rx_cb().
 * @param  listen_port  CL_PORT_FROM_TILE or CL_PORT_FROM_PC.
 * @param  src_addr     Sender address.
 * @param  src_port     Sender port.
 * @param  p_buf        Bytes.
 * @param  len          Length, bytes.
 */
void coordinator_loop_test_rx (uint16_t listen_port, uint32_t src_addr,
                               uint16_t src_port, uint8_t const * p_buf,
                               uint32_t len);

/**
 * @brief  One loop pass at the test clock's time, without cyw43_arch_poll().
 */
void coordinator_loop_test_pass (void);

/**
 * @brief  Coordinator state.
 * @return The state.
 */
proto_coord_state_t coordinator_loop_test_state (void);

/**
 * @brief  Online bitmap: bit n - 1 for Node ID n.
 * @return The bitmap.
 */
uint16_t coordinator_loop_test_online (void);

/**
 * @brief  Readiness (ASSET_READY) bitmap.
 * @return The bitmap.
 */
uint16_t coordinator_loop_test_readiness (void);

/**
 * @brief  Messages dropped for a stale session_id or seq.
 * @return The count.
 */
uint32_t coordinator_loop_test_stale (void);

/**
 * @brief  Beacon slots skipped after a stall.
 * @return The count.
 */
uint32_t coordinator_loop_test_skipped (void);

/**
 * @brief  Time the last loss of a node was detected, microseconds.
 * @param  node_id  1 to 9.
 * @return Detection time, or 0 if none.
 */
uint64_t coordinator_loop_test_loss_time (uint8_t node_id);

/**
 * @brief  Force the coordinator state. COORDINATING GAME has no entry path
 *         until build step 7, so a test reaches it only through this.
 * @param  state  The state to set.
 */
void coordinator_loop_test_set_state (proto_coord_state_t state);
#endif

#endif /* COORDINATOR_LOOP_H */

/*** end of file ***/
