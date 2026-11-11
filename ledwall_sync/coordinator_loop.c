/**
 * @file    coordinator_loop.c
 * @brief   Pico W coordinator (T-B2-02 to T-B2-05). See coordinator_loop.h
 *          for what is implemented, the OD-01 citation and the PROVISIONAL
 *          choices.
 *
 * Owner: Benjamin (Buddy 2). Implemented by Ahsanul (B4) for Benjamin.
 *
 * Loop structure (OD-08 ruling: one bounded routine per pass). Each pass
 * calls cyw43_arch_poll(), whose receive callbacks only timestamp, copy and
 * return, and then runs exactly one routine, the first that is due:
 *   1. BEACON, when next_beacon_us is reached;
 *   2. heartbeat scan, every 100 ms on its own absolute schedule;
 *   3. one copy of a timed command (START_AT, RESUME_AT, PAUSE, STOP);
 *   4. STATUS to the PC, every 250 ms on an absolute schedule;
 *   5. one buffered message, only if 1 ms is left before the next beacon;
 *   6. one log line, only if 5 ms is left before the next beacon.
 * Every routine is bounded by a fixed amount of work; T-B2-06 measures the
 * callback and dispatch times (cb_max_us, dispatch_max_us in CL_STATS).
 *
 * Concurrency: in poll mode every callback runs inside cyw43_arch_poll() in
 * this loop (OD-01, checked on the bench through exc_max), so the ring, the
 * registry and the bitmaps have one writer and need no lock. If OD-01 moved
 * to threadsafe background mode, cl_enqueue() would need a critical section
 * (plan T-B2-02, option (b)).
 *
 * Beacon after a stall: the schedule stays on the 100 ms grid. A slot whose
 * time has fully passed is skipped and counted (CL_STATS skipped), so a
 * stall never produces a burst of late beacons with stale spacing; the seq
 * counts only beacons actually sent, so the tiles do not count a skipped
 * slot as router loss (plan choice).
 *
 * Build options:
 *   CL_HOST_TEST            host build for ledwall_sync/test (no Pico SDK).
 *   CL_TEST_SUPPRESS_MS=<n> T-B4-05 test build: 20 s into each playback,
 *                           beacons stop for n ms once (bench procedure in
 *                           docs/week13/holdover_sizing.md).
 *
 * DEVIATION D-2 (module-wide): Pico SDK and lwIP names and types
 * (struct udp_pcb, struct pbuf, ip_addr_t, u16_t, err_t, PBUF_RAM).
 */
#include "coordinator_loop.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "ack_outbox.h"
#include "game_rules.h"
#include "join_backoff.h"
#include "score_font.h"

#ifdef CL_HOST_TEST
#include "coordinator_port.h"
#else
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"

/* Every acked game message must fit an outbox slot (build step 3: the
 * 23-byte WALL_PICTURE outgrew the old 16-byte slots). */
_Static_assert((PROTO_MOLE_ON_BYTES <= ACK_OUTBOX_MSG_MAX) &&
                   (PROTO_FEEDBACK_BYTES <= ACK_OUTBOX_MSG_MAX) &&
                   (PROTO_GAME_CONTROL_BYTES <= ACK_OUTBOX_MSG_MAX) &&
                   (PROTO_WALL_PICTURE_BYTES <= ACK_OUTBOX_MSG_MAX),
               "every acked game message must fit an outbox slot");
#endif
#endif

#ifndef ROUTER_SSID
/** Placeholder credentials (wifi_secrets.h is git-ignored; CI builds
 *  without it). */
#define ROUTER_SSID "CHANGE_ME"
#define ROUTER_PASS "CHANGE_ME"
#endif

/** Heartbeat scan period, 100 ms (VA-2.3, W6D-5a). */
#define CL_SCAN_PERIOD_US 100000u

/** STATUS period, 250 ms (proposed, VA-2.4, W6D-5a). */
#define CL_STATUS_PERIOD_US 250000u

/** Heartbeat timeout 2.5 s (proposed, VA-2.7; OD-18 may select 3 s). */
#define CL_HEARTBEAT_TIMEOUT_US 2500000u

/** One router join attempt is bounded by 10 s (proposed, W6D-2d). Attempts
 *  repeat forever, spaced by join_backoff_ms(). */
#define CL_CONNECT_TIMEOUT_MS 10000u

/** Recovery timeout 5 s (proposed, W6D-2c). */
#define CL_RECOVERY_TIMEOUT_US 5000000u

/** START_AT and RESUME_AT lead, 600 ms (proposed, VA-4.4, W6D-2a). */
#define CL_LEAD_US 600000u

/** Copies of each command and their spacing: 0, 100, 200 ms (VA-4.4,
 *  SRM-8Md; spacing for PAUSE and STOP is a plan choice, T-B2-05). */
#define CL_COPIES      3u
#define CL_COPY_GAP_US 100000u

/** Dispatch only when this much time is left before the next beacon or a
 *  command copy (OD-08: each routine at most 1 ms). */
#define CL_DISPATCH_GUARD_US 1000u

/** Log only with 5 ms to spare (plan choice: a log line must never delay a
 *  beacon or a command copy, even with a slow USB host). */
#define CL_LOG_GUARD_US 5000u

/** CL_STATS and LATE_HIST every 10 s (plan choice). */
#define CL_LOG_PERIOD_US 10000000u

/** Receive ring: 32 datagrams (plan choice: a burst of 9 REGISTER plus 9
 *  HEARTBEAT plus 9 ASSET_READY and 5 PC messages before one dispatch). */
#define CL_RING_LEN 32u

/** Largest datagram the Pico accepts: SESSION_CONFIG is 46 B. */
#define CL_RX_MAX_BYTES 48u

/** Concurrent timed commands (plan choice: START_AT, then PAUSE, then
 *  RESUME_AT, plus one rejoin, can overlap within 200 ms). */
#define CL_SEND_SLOTS 4u

/** Acked messages to a tile are resent every 50 ms until their ACK (plan
 *  choice: well inside the 300 ms MOLE_ON lead). */
#define CL_ACK_RETRY_US 50000u

/** Lifetimes of acked messages after their first send (plan choices). A
 *  MOLE_ON lives until its show time. */
#define CL_FEEDBACK_LIFE_US 300000u
#define CL_CONTROL_LIFE_US  1000000u

/** Logged events waiting for a log slot (plan choice). */
#define CL_EVENT_LEN 16u

/** Callback and dispatch time histograms: 100 us bins, 0 to 1 ms, plus one
 *  bin above 1 ms (T-B2-06 pass line: every callback at most 1 ms). */
#define CL_TIME_BINS   11u
#define CL_TIME_BIN_US 100u

/** Microseconds per second and frames per second, for frame arithmetic. */
#define CL_US_PER_S UINT64_C(1000000)
#define CL_FPS      UINT64_C(60)

/** All 9 node bits. */
#define CL_ALL_NODES UINT16_C(0x01FF)

/** One received datagram, copied by the callback. */
typedef struct
{
    uint64_t t_rx_us;
    uint32_t src_addr;
    uint16_t src_port;
    uint16_t listen_port;
    uint32_t len;
    uint8_t  bytes[CL_RX_MAX_BYTES];
} cl_rx_entry_t;

/** One registry row, indexed by Node ID - 1 (T-B2-03). */
typedef struct
{
    bool     b_known;      /**< A REGISTER was accepted. */
    bool     b_online;     /**< Heard within the heartbeat timeout. */
    bool     b_lost;       /**< Went offline while a session played. */
    bool     b_seq_known;  /**< last_seq holds a value. */
    uint16_t last_seq;     /**< Newest accepted seq from this tile. */
    uint32_t addr;         /**< Source address of the last message. */
    uint64_t last_heard;   /**< Receive time of the last message. */
    uint64_t loss_time_us; /**< When the scan last marked it lost. */
} cl_node_t;

/** The active session (T-B2-04). */
typedef struct
{
    bool           b_active;
    uint8_t        session_id;
    uint8_t        rows;        /**< Wall rows, 1 to 9. */
    uint8_t        cols;        /**< Wall columns, 1 to 9. */
    proto_policy_t policy;
    uint16_t       required;    /**< Node IDs in the map, bit n - 1. */
    uint16_t       frame_count; /**< 1 to 1023. */
    uint8_t        node_map[PROTO_MAP_SLOTS];
    uint32_t       crc32[PROTO_MAP_SLOTS];
} cl_session_t;

/** One timed command being sent 3 times. */
typedef struct
{
    bool         b_busy;
    bool         b_first_sent;
    proto_type_t type;     /**< START_AT, RESUME_AT, PAUSE or STOP. */
    uint8_t      copies;   /**< Copies sent so far. */
    uint16_t     seq;      /**< One seq for all copies. */
    uint16_t     frame;    /**< RESUME_AT frame_index. */
    uint32_t     addr;     /**< Broadcast, or one tile for a rejoin. */
    uint64_t     first_us; /**< Time of copy 1. */
    uint64_t     due_us;   /**< Time of the next copy. */
    uint64_t     t_value;  /**< t_start or t_resume, set at copy 1. */
} cl_send_slot_t;

/** Logged event codes. */
typedef enum
{
    CL_EV_JOIN = 0,
    CL_EV_LOSS,
    CL_EV_BACK,
    CL_EV_STATE,
    CL_EV_SESSION_REJECTED,
    CL_EV_START_REFUSED,
    CL_EV_CRC_MISMATCH,
    CL_EV_TILE_NACK,
    CL_EV_LINK_DOWN,
    CL_EV_REJOIN_SENT,
    CL_EV_SUPPRESS,
    CL_EV_GAME_REFUSED,
    CL_EV_GAME_MOLE,     /* DS-13: node, value up_ms, time = show time. */
    CL_EV_GAME_TAP,      /* DS-13: node, value arrival minus tap time, us
                          * (a 32-bit two's complement: above 2^31 means the
                          * tap is stamped after it arrived). */
    CL_EV_GAME_FEEDBACK, /* DS-13: node, value 1 hit or 0 miss. */
    CL_EV_COUNT
} cl_event_code_t;

/** One logged event. */
typedef struct
{
    cl_event_code_t code;
    uint8_t         node_id;
    uint32_t        value;
    uint64_t        t_us;
} cl_event_t;

/** Counters printed in CL_STATS. */
typedef struct
{
    uint32_t sent;            /**< Beacons sent. */
    uint32_t send_fail;       /**< Beacons whose udp_sendto failed. */
    uint32_t skipped;         /**< Beacon slots skipped after a stall. */
    uint16_t first_seq;       /**< seq of the first beacon in the window. */
    uint16_t last_seq;        /**< seq of the last beacon sent. */
    uint32_t rx_dropped;      /**< Datagrams lost to a full ring. */
    uint32_t rx_bad;          /**< Datagrams that did not decode. */
    uint32_t stale;           /**< Dropped: wrong session_id or old seq. */
    uint32_t cmd_fail;        /**< Command copies whose send failed. */
    uint32_t events_lost;     /**< Events lost to a full event ring. */
    uint32_t exc_max;         /**< Largest exception number in a callback. */
    uint32_t cb_max_us;       /**< Longest callback. */
    uint32_t dispatch_max_us; /**< Longest dispatch routine. */
} cl_stats_t;

/* Ring and registry (single writer: this loop, OD-01 poll mode). */
static cl_rx_entry_t  g_ring[CL_RING_LEN];
static uint32_t       g_ring_head  = 0u;
static uint32_t       g_ring_count = 0u;
static cl_node_t      g_registry[CL_MAX_NODES];
static cl_session_t   g_session;
static cl_send_slot_t g_slots[CL_SEND_SLOTS];
static cl_event_t     g_events[CL_EVENT_LEN];
static uint32_t       g_event_head  = 0u;
static uint32_t       g_event_count = 0u;

/* State (W6 statechart 2) and the playback timeline. */
static proto_coord_state_t g_state     = PROTO_COORD_BOOTING;
static uint16_t            g_readiness = 0u; /* ASSET_READY, bit n - 1. */
static uint16_t            g_sync      = 0u; /* SYNC_READY, bit n - 1. */
static bool                g_b_resume_pending  = false;
static bool                g_b_return_to_pause = false;
static uint64_t            g_origin_us         = 0u; /* Pico time of frame 0. */
static uint16_t            g_saved_frame       = 0u;
static uint64_t            g_recover_until_us  = 0u;
static uint64_t            g_playing_since_us  = 0u;

/* Schedules (absolute, microseconds). */
static uint64_t g_next_beacon_us = 0u;
static uint64_t g_next_scan_us   = 0u;
static uint64_t g_next_status_us = 0u;
static uint64_t g_next_log_us    = 0u;

/* Sequence streams (layout_table.md section 10 item 13). */
static uint16_t g_beacon_seq = 0u;
static uint16_t g_cmd_seq    = 0u;
static uint16_t g_status_seq = 0u;
static uint16_t g_ack_seq    = 0u; /* ACK header seq: not acked itself. */

/* Game (build step 7). The engine and the acked outbox are pure C. */
static game_t       g_game;
static ack_outbox_t g_outbox;
static bool         g_b_game_mode = false; /* false video, true game. */
static uint16_t     g_tap_seq[CL_MAX_NODES];
static bool         g_b_tap_seen[CL_MAX_NODES];

/* PC address, from the last valid datagram on CL_PORT_FROM_PC. */
static bool     g_b_pc_known     = false;
static bool     g_b_pc_seq_known = false;
static uint32_t g_pc_addr        = 0u;
static uint16_t g_pc_port        = 0u;
static uint16_t g_pc_seq         = 0u;

static uint32_t   g_bcast_addr = 0u;
static cl_stats_t g_stats;
static uint32_t   g_lateness_hist[CL_LATE_BINS];
static uint32_t   g_cb_hist[CL_TIME_BINS];
static uint32_t   g_dispatch_hist[CL_TIME_BINS];
static bool       g_b_suppress_done = false;

static void cl_reset_state (uint32_t bcast_addr, uint64_t now);
#ifndef CL_HOST_TEST
static void cl_on_link_lost (uint64_t now);
#endif
static void cl_pass (void);
static void cl_beacon_routine (uint64_t now);
static void cl_send_ack (uint32_t addr, uint16_t port, uint8_t node_id,
                         uint16_t acked_seq, uint8_t session_id);
static void cl_on_tap (cl_rx_entry_t const * p_entry,
                       proto_header_t const * p_header);
static void cl_on_tile_ack (cl_rx_entry_t const * p_entry);
static void cl_on_game_control (cl_rx_entry_t const * p_entry,
                                proto_header_t const * p_header);
static void cl_game_command (uint8_t command);
static void cl_game_start (uint64_t now);
static void cl_game_end (uint64_t now);
static void cl_game_routine (uint64_t now);
static void cl_outbox_routine (uint64_t now);
static void cl_queue_tile (uint8_t node_id, proto_type_t type, uint16_t seq,
                           uint8_t const * p_buf, uint32_t len, uint64_t now,
                           uint64_t deadline_us);
static void cl_queue_control (uint8_t node_id, uint8_t command, uint64_t now,
                              uint64_t life_us);
static void cl_queue_mole (game_event_t const * p_event, uint64_t now);
static void cl_queue_feedback (uint8_t node_id, uint8_t result, uint64_t now);
static void cl_queue_score (uint8_t node_id, int32_t score, uint64_t now);
static void cl_game_sync_active (void);
static bool cl_stopped (void);
static int8_t cl_status_score (void);
static uint16_t cl_next_cmd_seq (void);
static uint16_t cl_active_slots (void);
static void cl_scan_heartbeats (uint64_t now);
static void cl_scan_one (uint32_t index, uint64_t now);
static void cl_send_due_copy (uint64_t now);
static void cl_send_status (uint64_t now);
static void cl_dispatch_one (uint64_t now);
static void cl_dispatch_entry (cl_rx_entry_t const * p_entry);
static void cl_from_tile (cl_rx_entry_t const *  p_entry,
                          proto_header_t const * p_header);
static void cl_on_register (cl_rx_entry_t const * p_entry);
static void cl_on_tile_report (cl_rx_entry_t const *  p_entry,
                               proto_header_t const * p_header);
static void cl_on_asset_ready (cl_rx_entry_t const * p_entry);
static void cl_from_pc (cl_rx_entry_t const *  p_entry,
                        proto_header_t const * p_header);
static void cl_session_apply (cl_rx_entry_t const * p_entry);
static bool cl_session_valid (proto_session_config_t const * p_cfg);
static void cl_pc_command (proto_type_t type);
static void cl_cmd_start_at (void);
static void cl_cmd_pause (void);
static void cl_cmd_resume_at (void);
static void cl_cmd_stop (void);
static void cl_issue_resume (void);
static void cl_triple_send (proto_type_t type, uint16_t frame, uint32_t addr);
static void cl_send_copy (cl_send_slot_t * p_slot, uint64_t now);
static void cl_issue_timeline (cl_send_slot_t * p_slot);
static void cl_node_seen (uint8_t node_id, uint32_t addr, uint64_t t_rx);
static void cl_node_back (uint8_t node_id);
static void cl_policy_on_loss (uint8_t node_id);
static void cl_evaluate (void);
static void cl_set_state (proto_coord_state_t state);
static void cl_maybe_rejoin (uint8_t node_id);
static bool cl_seq_newer (uint16_t seq, uint16_t last);
static bool cl_tile_seq_ok (uint8_t node_id, uint16_t seq);
static bool cl_slot_of (uint8_t node_id, uint32_t * p_slot);
static uint16_t     cl_online_bits (void);
static uint16_t     cl_node_bit (uint8_t node_id);
static uint32_t     cl_state_code (void);
static proto_type_t cl_type_of (proto_header_t const * p_header);
static uint64_t     cl_tick_deadline (uint64_t origin, uint64_t tick);
static uint64_t     cl_ticks_since (uint64_t origin, uint64_t t_us);
static uint64_t     cl_earliest_due (void);
static void cl_log_event (cl_event_code_t code, uint8_t node_id, uint32_t value,
                          uint64_t now);
static void cl_log_routine (uint64_t now);
static void cl_print_stats (void);
static void cl_hist_add (uint32_t * p_hist, uint32_t bins, uint64_t value_us,
                         uint64_t bin_us);
static void cl_enqueue (uint16_t listen_port, uint32_t src_addr,
                        uint16_t src_port, uint8_t const * p_buf, uint32_t len,
                        uint64_t t_rx);
static uint64_t cl_now_us (void);
static bool cl_udp_send (uint32_t addr, uint16_t port, uint8_t const * p_buf,
                         uint32_t len);
static bool cl_send_beacon_bytes (uint8_t * p_buf, uint64_t * p_t_p);

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
 * @brief  Reset every table, counter and schedule to the state after the
 *         router join: WAITING FOR SESSION, beacons due now.
 * @param  bcast_addr  Subnet broadcast address.
 * @param  now         Current time, microseconds.
 */
static void cl_reset_state (uint32_t bcast_addr, uint64_t now)
{
    (void)memset(g_registry, 0, sizeof(g_registry)); /* Returns g_registry. */
    (void)memset(&g_session, 0, sizeof(g_session));  /* Returns its target. */
    (void)memset(g_slots, 0, sizeof(g_slots));       /* Returns g_slots. */
    (void)memset(&g_stats, 0, sizeof(g_stats));      /* Returns &g_stats. */
    (void)memset(g_lateness_hist, 0, sizeof(g_lateness_hist)); /* Same. */
    (void)memset(g_cb_hist, 0, sizeof(g_cb_hist));             /* Same. */
    (void)memset(g_dispatch_hist, 0, sizeof(g_dispatch_hist)); /* Same. */

    g_ring_head         = 0u;
    g_ring_count        = 0u;
    g_event_head        = 0u;
    g_event_count       = 0u;
    g_readiness         = 0u;
    g_sync              = 0u;
    g_b_resume_pending  = false;
    g_b_return_to_pause = false;
    g_b_pc_known        = false;
    g_b_pc_seq_known    = false;
    g_b_suppress_done   = false;
    g_bcast_addr        = bcast_addr;
    /* No round yet; the RNG is seeded from the clock. */
    game_rules_init(&g_game, (uint32_t)now);
    ack_outbox_init(&g_outbox);
    (void)memset(g_b_tap_seen, 0, sizeof(g_b_tap_seen)); /* Returns it. */
    (void)memset(g_tap_seq, 0, sizeof(g_tap_seq));       /* Returns it. */
    g_next_beacon_us    = now;
    g_next_scan_us      = now + CL_SCAN_PERIOD_US;
    g_next_status_us    = now + CL_STATUS_PERIOD_US;
    g_next_log_us       = now + CL_LOG_PERIOD_US;
    g_state             = PROTO_COORD_WAITING_FOR_SESSION;
}

/**
 * @brief  One loop pass: run the first routine that is due (see the file
 *         comment for the order).
 */
static void cl_pass (void)
{
    uint64_t const now     = cl_now_us();
    uint64_t const cmd_due = cl_earliest_due();
    bool     const b_room  =
        ((now + CL_DISPATCH_GUARD_US) <= g_next_beacon_us) &&
        ((now + CL_DISPATCH_GUARD_US) <= cmd_due);

    if (now >= g_next_beacon_us)
    {
        cl_beacon_routine(now);
    }
    else if (now >= g_next_scan_us)
    {
        cl_scan_heartbeats(now);
    }
    else if (now >= cmd_due)
    {
        cl_send_due_copy(now);
    }
    else if ((b_room) && (ack_outbox_next_due(&g_outbox) <= now))
    {
        cl_outbox_routine(now);
    }
    else if (now >= g_next_status_us)
    {
        cl_send_status(now);
    }
    else if ((b_room) && (game_rules_next_due(&g_game) <= now))
    {
        cl_game_routine(now);
    }
    else if ((0u != g_ring_count) && (b_room))
    {
        cl_dispatch_one(now);
    }
    else if (((now + CL_LOG_GUARD_US) <= g_next_beacon_us) &&
             ((now + CL_LOG_GUARD_US) <= cmd_due))
    {
        cl_log_routine(now);
    }
    else
    {
        /* Nothing due: the next pass polls cyw43 again. */
    }
}

/**
 * @brief  Send one BEACON on the absolute schedule. Slots that passed
 *         entirely during a stall are skipped and counted (file comment).
 * @param  now  Current time, microseconds.
 */
static void cl_beacon_routine (uint64_t now)
{
    uint64_t const behind = now - g_next_beacon_us;

    if (behind >= CL_BEACON_PERIOD_US)
    {
        uint64_t const missed = behind / CL_BEACON_PERIOD_US;

        g_next_beacon_us += missed * CL_BEACON_PERIOD_US;
        /* missed is far below 2^32 for any stall the loop survives. */
        g_stats.skipped += (uint32_t)missed;
    }

    bool b_suppressed = false;

#ifdef CL_TEST_SUPPRESS_MS
    /* T-B4-05 test build: one gap of CL_TEST_SUPPRESS_MS, 20 s into play. */
    if ((PROTO_COORD_COORDINATING_PLAYBACK == g_state) &&
        (!g_b_suppress_done) &&
        (now >= (g_playing_since_us + (20u * CL_US_PER_S))))
    {
        b_suppressed      = (now < (g_playing_since_us + (20u * CL_US_PER_S) +
                               /* A build option in ms: widened, kept. */
                               ((uint64_t)CL_TEST_SUPPRESS_MS * 1000u)));
        g_b_suppress_done = !b_suppressed;
    }
#endif

    if (!b_suppressed)
    {
        uint8_t              buf[PROTO_BEACON_BYTES];
        proto_beacon_t const msg = {
            .header = {.type       = PROTO_TYPE_BEACON,
                       .session_id = g_session.session_id,
                       .seq        = g_beacon_seq},
            .t_p    = 0u,
        };
        uint64_t t_p = 0u;

        /* buf is exactly PROTO_BEACON_BYTES: the length is known. */
        (void)proto_encode_beacon(&msg, buf, PROTO_BEACON_BYTES);

        if (cl_send_beacon_bytes(buf, &t_p))
        {
            g_stats.last_seq = g_beacon_seq;
        }
        else
        {
            g_stats.send_fail++;
        }

        if (0u == g_stats.sent)
        {
            g_stats.first_seq = g_beacon_seq;
        }

        g_stats.sent++;
        g_beacon_seq++;
        cl_hist_add(g_lateness_hist, CL_LATE_BINS, t_p - g_next_beacon_us,
                    1000u);
    }

    g_next_beacon_us += CL_BEACON_PERIOD_US;
}

/**
 * @brief  Heartbeat scan, every 100 ms: mark tiles silent for longer than
 *         the timeout offline, apply the failure policy, check the recovery
 *         timeout (T-B2-03, T-B2-04).
 * @param  now  Current time, microseconds.
 */
static void cl_scan_heartbeats (uint64_t now)
{
    for (uint32_t index = 0u; index < CL_MAX_NODES; index++)
    {
        cl_scan_one(index, now);
    }

    if ((PROTO_COORD_RECOVERING == g_state) && (now >= g_recover_until_us))
    {
        cl_set_state(PROTO_COORD_FAULT);
    }

#ifndef CL_HOST_TEST
    if (CYW43_LINK_UP != cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA))
    {
        cl_log_event(CL_EV_LINK_DOWN, 0u, 0u, now);
        cl_on_link_lost(now);
    }
#endif

    g_next_scan_us += CL_SCAN_PERIOD_US;
}

/**
 * @brief  Scan one registry row.
 * @param  index  Node ID - 1.
 * @param  now    Current time, microseconds.
 */
static void cl_scan_one (uint32_t index, uint64_t now)
{
    cl_node_t * const p_node = &g_registry[index];

    if ((p_node->b_online) &&
        ((now - p_node->last_heard) > CL_HEARTBEAT_TIMEOUT_US))
    {
        /* index < 9, so index + 1 is a Node ID that fits uint8_t. */
        /* index < 9. */
            /* index < 9. */
        uint8_t const node_id = (uint8_t)(index + 1u);

        p_node->b_online     = false;
        p_node->loss_time_us = now;
        cl_game_sync_active();
        cl_log_event(CL_EV_LOSS, node_id,
                     /* The silence is below 2^32 us after 2.6 s. */
                     (uint32_t)(now - p_node->last_heard), now);
        cl_policy_on_loss(node_id);
    }
}

/**
 * @brief  Apply the failure policy when a tile is lost (T-B2-04). Pause:
 *         PAUSE to every tile and RECOVERING with the 5 s timeout. Continue:
 *         the tile is marked unavailable and the others keep their regions.
 *         Before playback a missing required tile sends the coordinator
 *         back to WAITING FOR NODES.
 * @param  node_id  The lost tile.
 */
static void cl_policy_on_loss (uint8_t node_id)
{
    uint16_t const bit        = cl_node_bit(node_id);
    bool const     b_required = (0u != (g_session.required & bit));
    bool const     b_playing  = (PROTO_COORD_COORDINATING_PLAYBACK == g_state);

    if (b_required && b_playing)
    {
        g_registry[node_id - 1u].b_lost = true;

        if (PROTO_POLICY_PAUSE == g_session.policy)
        {
            cl_cmd_pause();
            g_b_return_to_pause = true;
            g_recover_until_us  = cl_now_us() + CL_RECOVERY_TIMEOUT_US;
            cl_set_state(PROTO_COORD_RECOVERING);
        }
        else
        {
            /* Continue: nothing else changes (FR18 as rewritten, W6B-3). */
        }
    }
    else if (b_required && (PROTO_COORD_PAUSED == g_state))
    {
        g_registry[node_id - 1u].b_lost = true;
    }
    else if (b_required)
    {
        cl_evaluate();
    }
    else
    {
        /* Not in the session: only the online bitmap changes. */
    }
}

/**
 * @brief  Send the due copy of the earliest timed command.
 * @param  now  Current time, microseconds.
 */
static void cl_send_due_copy (uint64_t now)
{
    cl_send_slot_t * p_due = NULL;

    for (uint32_t index = 0u; index < CL_SEND_SLOTS; index++)
    {
        cl_send_slot_t * const p_slot = &g_slots[index];

        if ((p_slot->b_busy) && (p_slot->due_us <= now) &&
            ((NULL == p_due) || (p_slot->due_us < p_due->due_us)))
        {
            p_due = p_slot;
        }
    }

    if (NULL != p_due)
    {
        cl_send_copy(p_due, now);
    }
}

/**
 * @brief  Send one copy of a timed command. Copy 1 fixes t_start or
 *         t_resume at its own send time plus 600 ms (T-B2-05: "t_start
 *         exactly 600 ms after the first send"); copies 2 and 3 repeat the
 *         same bytes at copy 1 + 100 ms and + 200 ms.
 * @param  p_slot  The command.
 * @param  now     Current time, microseconds.
 */
static void cl_send_copy (cl_send_slot_t * p_slot, uint64_t now)
{
    uint8_t  buf[PROTO_RESUME_AT_BYTES];
    uint32_t len = 0u;

    proto_header_t const header = {.type       = p_slot->type,
                                   .session_id = g_session.session_id,
                                   .seq        = p_slot->seq};

    if (!p_slot->b_first_sent)
    {
        p_slot->first_us     = cl_now_us();
        p_slot->t_value      = p_slot->first_us + CL_LEAD_US;
        p_slot->b_first_sent = true;
        cl_issue_timeline(p_slot);
    }

    if (PROTO_TYPE_START_AT == p_slot->type)
    {
        proto_start_at_t const msg = {header, p_slot->t_value};

        len = proto_encode_start_at(&msg, buf, sizeof(buf));
    }
    else if (PROTO_TYPE_RESUME_AT == p_slot->type)
    {
        proto_resume_at_t const msg = {header, p_slot->t_value, p_slot->frame};

        len = proto_encode_resume_at(&msg, buf, sizeof(buf));
    }
    else if (PROTO_TYPE_PAUSE == p_slot->type)
    {
        proto_command_t const msg = {header};

        len = proto_encode_pause(&msg, buf, sizeof(buf));
    }
    else
    {
        proto_command_t const msg = {header};

        len = proto_encode_stop(&msg, buf, sizeof(buf));
    }

    if (!cl_udp_send(p_slot->addr, CL_PORT_TILE_RX, buf, len))
    {
        g_stats.cmd_fail++;
    }

    p_slot->copies++;
    p_slot->due_us = p_slot->first_us + (p_slot->copies * CL_COPY_GAP_US);
    p_slot->b_busy = (p_slot->copies < CL_COPIES);
    (void)now; /* The send time is read just before the send. */
}

/**
 * @brief  At copy 1 of a timed command, fix the timeline it starts.
 *         START_AT: frame 0 at t_start. Broadcast RESUME_AT: the saved frame
 *         shows at t_resume, so origin = t_resume minus that frame's offset.
 *         Rejoin RESUME_AT (one tile, OD-14 option (a)): t_resume becomes the
 *         first tick deadline at least 600 ms ahead on the current timeline
 *         and frame_index the frame the wall shows then; the origin stays.
 * @param  p_slot  The command, at its first send.
 */
static void cl_issue_timeline (cl_send_slot_t * p_slot)
{
    uint64_t const frames = g_session.frame_count;

    if (PROTO_TYPE_START_AT == p_slot->type)
    {
        g_origin_us        = p_slot->t_value;
        g_playing_since_us = p_slot->t_value;
    }
    else if ((PROTO_TYPE_RESUME_AT == p_slot->type) &&
             (g_bcast_addr == p_slot->addr))
    {
        g_origin_us = p_slot->t_value - cl_tick_deadline(0u, p_slot->frame);
        g_playing_since_us = p_slot->t_value;
    }
    else if (PROTO_TYPE_RESUME_AT == p_slot->type)
    {
        uint64_t tick = cl_ticks_since(g_origin_us, p_slot->t_value);

        if (cl_tick_deadline(g_origin_us, tick) < p_slot->t_value)
        {
            tick++;
        }

        p_slot->t_value = cl_tick_deadline(g_origin_us, tick);
        /* frame_count is 1 to 1023, so the remainder fits uint16_t. */
        p_slot->frame = (uint16_t)(tick % frames);
    }
    else
    {
        /* PAUSE and STOP carry no time. */
    }
}

/**
 * @brief  STATUS to the PC every 250 ms on an absolute schedule (T-B2-04):
 *         readiness (ASSET_READY), online bitmap and state.
 * @param  now  Current time, microseconds.
 */
static void cl_send_status (uint64_t now)
{
    if (g_b_pc_known)
    {
        uint8_t              buf[PROTO_STATUS_BYTES];
        proto_status_t const msg = {
            .header    = {.type       = PROTO_TYPE_STATUS,
                          .session_id = g_session.session_id,
                          .seq        = g_status_seq},
            .readiness = g_readiness,
            .online    = cl_online_bits(),
            .state     = (uint8_t)g_state, /* 13 states fit uint8_t. */
            .score     = cl_status_score(),
        };
        uint32_t const len = proto_encode_status(&msg, buf, sizeof(buf));

        (void)cl_udp_send(g_pc_addr, g_pc_port, buf, len); /* Next one in
                                                                250 ms. */
        g_status_seq++;
    }

    g_next_status_us += CL_STATUS_PERIOD_US;
    (void)now; /* The schedule is absolute. */
}

/**
 * @brief  Take the oldest buffered datagram and handle it, timing the work
 *         for the T-B2-06 dispatch histogram.
 * @param  now  Current time, microseconds.
 */
static void cl_dispatch_one (uint64_t now)
{
    cl_rx_entry_t const * const p_entry = &g_ring[g_ring_head];

    cl_dispatch_entry(p_entry);
    g_ring_head = (g_ring_head + 1u) % CL_RING_LEN;
    g_ring_count--;

    uint64_t const spent = cl_now_us() - now;

    cl_hist_add(g_dispatch_hist, CL_TIME_BINS, spent, CL_TIME_BIN_US);

    if (spent > g_stats.dispatch_max_us)
    {
        /* One routine takes far less than 2^32 us. */
        g_stats.dispatch_max_us = (uint32_t)spent;
    }
}

/**
 * @brief  Route one datagram by the port it arrived on.
 * @param  p_entry  The datagram.
 */
static void cl_dispatch_entry (cl_rx_entry_t const * p_entry)
{
    proto_header_t header;

    if (!proto_decode_header(p_entry->bytes, p_entry->len, &header))
    {
        g_stats.rx_bad++;
    }
    else if (CL_PORT_FROM_TILE == p_entry->listen_port)
    {
        cl_from_tile(p_entry, &header);
    }
    else if (CL_PORT_FROM_PC == p_entry->listen_port)
    {
        cl_from_pc(p_entry, &header);
    }
    else
    {
        g_stats.rx_bad++;
    }
}

/**
 * @brief  A datagram from a tile.
 * @param  p_entry   The datagram.
 * @param  p_header  Its decoded header.
 */
static void cl_from_tile (cl_rx_entry_t const *  p_entry,
                          proto_header_t const * p_header)
{
    if (PROTO_TYPE_REGISTER == cl_type_of(p_header))
    {
        /* REGISTER restarts the sender's seq baseline and needs no
         * session: a tile registers before it knows the session. */
        cl_on_register(p_entry);
    }
    else if (PROTO_TYPE_TAP == cl_type_of(p_header))
    {
        /* TAP is ACKed and relayed whatever the session (tap-to-place). */
        cl_on_tap(p_entry, p_header);
    }
    else if (PROTO_TYPE_ACK == cl_type_of(p_header))
    {
        cl_on_tile_ack(p_entry);
    }
    else if (p_header->session_id != g_session.session_id)
    {
        g_stats.stale++;
    }
    else if (PROTO_TYPE_ASSET_READY == cl_type_of(p_header))
    {
        cl_on_asset_ready(p_entry);
    }
    else
    {
        cl_on_tile_report(p_entry, p_header);
    }
}

/**
 * @brief  REGISTER: the tile joins the registry by its permanent Node ID
 *         (FR9). A cached asset whose session_id and CRC32 match the
 *         active session counts as ASSET_READY (T-B2-04 rejoin); otherwise
 *         the readiness bit is cleared and the PC re-uploads (FR20). The
 *         tile must send SYNC_READY again.
 * @param  p_entry  The datagram.
 */
static void cl_on_register (cl_rx_entry_t const * p_entry)
{
    proto_register_t msg;
    uint32_t         slot = 0u;

    if (!proto_decode_register(p_entry->bytes, p_entry->len, &msg))
    {
        g_stats.rx_bad++;
    }
    else
    {
        cl_node_t * const p_node = &g_registry[msg.node_id - 1u];
        uint16_t const    bit    = cl_node_bit(msg.node_id);

        if (!p_node->b_known)
        {
            cl_log_event(CL_EV_JOIN, msg.node_id, p_entry->src_addr,
                         p_entry->t_rx_us);
        }

        p_node->b_known     = true;
        p_node->b_seq_known = true;
        p_node->last_seq    = msg.header.seq;
        g_sync &= (uint16_t)~bit; /* 9-bit mask. */

        /* A REGISTER restarts the baseline of every counter of this tile
         * (layout_table.md section 10 item 13), the TAP counter too: a tile
         * that restarted counts its TAPs from 0 again, and those would be
         * dropped as stale (bench 11 Nov 2026, host test C23). */
        g_b_tap_seen[msg.node_id - 1u] = false;

        if ((g_session.b_active) &&
            (msg.cached_session_id == g_session.session_id) &&
            (cl_slot_of(msg.node_id, &slot)) &&
            (msg.cached_crc32 == g_session.crc32[slot]))
        {
            g_readiness |= bit;
        }
        else
        {
            g_readiness &= (uint16_t)~bit; /* 9-bit mask. */
        }

        cl_node_seen(msg.node_id, p_entry->src_addr, p_entry->t_rx_us);
        cl_evaluate();
    }
}

/**
 * @brief  ASSET_READY {session_id, CRC32}: counted only when the CRC32
 *         equals the manifest CRC32 of that Node ID (W6D-7a, T-B2-04).
 * @param  p_entry  The datagram (session already checked).
 */
static void cl_on_asset_ready (cl_rx_entry_t const * p_entry)
{
    proto_asset_ready_t msg;
    uint32_t            slot = 0u;

    if (!proto_decode_asset_ready(p_entry->bytes, p_entry->len, &msg))
    {
        g_stats.rx_bad++;
    }
    else if (!cl_tile_seq_ok(msg.node_id, msg.header.seq))
    {
        g_stats.stale++;
    }
    else if ((cl_slot_of(msg.node_id, &slot)) &&
             (msg.crc32 == g_session.crc32[slot]))
    {
        g_readiness |= cl_node_bit(msg.node_id);
        cl_node_seen(msg.node_id, p_entry->src_addr, p_entry->t_rx_us);
        cl_evaluate();
    }
    else
    {
        cl_log_event(CL_EV_CRC_MISMATCH, msg.node_id, msg.crc32,
                     p_entry->t_rx_us);
        cl_node_seen(msg.node_id, p_entry->src_addr, p_entry->t_rx_us);
    }
}

/**
 * @brief  HEARTBEAT, SYNC_READY or NACK from a registered tile.
 * @param  p_entry   The datagram (session already checked).
 * @param  p_header  Its decoded header.
 */
static void cl_on_tile_report (cl_rx_entry_t const *  p_entry,
                               proto_header_t const * p_header)
{
    uint8_t node_id = 0u;
    bool    b_ok    = false;

    if (PROTO_TYPE_HEARTBEAT == cl_type_of(p_header))
    {
        proto_heartbeat_t msg;

        b_ok    = proto_decode_heartbeat(p_entry->bytes, p_entry->len, &msg);
        node_id = msg.node_id;
    }
    else if (PROTO_TYPE_SYNC_READY == cl_type_of(p_header))
    {
        proto_sync_ready_t msg;

        b_ok    = proto_decode_sync_ready(p_entry->bytes, p_entry->len, &msg);
        node_id = msg.node_id;
    }
    else if (PROTO_TYPE_NACK == cl_type_of(p_header))
    {
        proto_nack_t msg;

        b_ok    = proto_decode_nack(p_entry->bytes, p_entry->len, &msg);
        node_id = msg.node_id;

        if (b_ok)
        {
            cl_log_event(CL_EV_TILE_NACK, node_id, msg.ref_seq,
                         p_entry->t_rx_us);
        }
    }
    else
    {
        b_ok = false;
    }

    if (!b_ok)
    {
        g_stats.rx_bad++;
    }
    else if ((!g_registry[node_id - 1u].b_known) ||
             (!cl_tile_seq_ok(node_id, p_header->seq)))
    {
        /* Unregistered (no REGISTER since boot) or not newer. */
        g_stats.stale++;
    }
    else
    {
        if (PROTO_TYPE_SYNC_READY == cl_type_of(p_header))
        {
            g_sync |= cl_node_bit(node_id);
        }

        cl_node_seen(node_id, p_entry->src_addr, p_entry->t_rx_us);
        cl_evaluate();
        cl_maybe_rejoin(node_id);
    }
}

/**
 * @brief  A datagram from the PC: SESSION_CONFIG or a command (START_AT,
 *         PAUSE, RESUME_AT, STOP rows used C to P, layout_table.md section
 *         10 item 11). Its address becomes the STATUS destination.
 * @param  p_entry   The datagram.
 * @param  p_header  Its decoded header.
 */
static void cl_from_pc (cl_rx_entry_t const *  p_entry,
                        proto_header_t const * p_header)
{
    g_b_pc_known = true;
    g_pc_addr    = p_entry->src_addr;
    g_pc_port    = p_entry->src_port;

    if (PROTO_TYPE_SESSION_CONFIG == cl_type_of(p_header))
    {
        cl_session_apply(p_entry);
    }
    else if (PROTO_TYPE_GAME_CONTROL == cl_type_of(p_header))
    {
        /* Mode and round commands need no session, so a website can switch
         * mode before SESSION_CONFIG. */
        cl_on_game_control(p_entry, p_header);
    }
    else if ((!g_session.b_active) ||
             (p_header->session_id != g_session.session_id) ||
             ((g_b_pc_seq_known) && (!cl_seq_newer(p_header->seq, g_pc_seq))))
    {
        g_stats.stale++;
    }
    else
    {
        g_b_pc_seq_known = true;
        g_pc_seq         = p_header->seq;
        cl_pc_command(cl_type_of(p_header));
    }
}

/**
 * @brief  SESSION_CONFIG: VALIDATING SESSION. A new session_id is accepted
 *         only while stopped (W6 statechart 2: "New session or layout while
 *         stopped"); it clears readiness and restarts the PC's seq baseline.
 *         The same session_id again is a repeat and is ignored.
 *         "Stopped" is an explicit allow-list, WAITING FOR SESSION through
 *         READY, so a state added later (as COORDINATING GAME was) refuses
 *         SESSION_CONFIG until it is listed here (decision of 6 Nov 2026).
 * @param  p_entry  The datagram.
 */
static void cl_session_apply (cl_rx_entry_t const * p_entry)
{
    proto_session_config_t cfg;

    bool const b_stopped =
        (PROTO_COORD_WAITING_FOR_SESSION == g_state) ||
        (PROTO_COORD_VALIDATING_SESSION == g_state) ||
        (PROTO_COORD_WAITING_FOR_NODES == g_state) ||
        (PROTO_COORD_WAITING_FOR_TILE_READINESS == g_state) ||
        (PROTO_COORD_SYNCHRONISING == g_state) ||
        (PROTO_COORD_READY == g_state);

    if (!proto_decode_session_config(p_entry->bytes, p_entry->len, &cfg))
    {
        g_stats.rx_bad++;
    }
    else if ((g_session.b_active) &&
             (cfg.header.session_id == g_session.session_id))
    {
        g_stats.stale++; /* A repeat of the active session. */
    }
    else if ((!b_stopped) || (!cl_session_valid(&cfg)))
    {
        cl_log_event(CL_EV_SESSION_REJECTED, 0u, cfg.header.session_id,
                     p_entry->t_rx_us);
    }
    else
    {
        cl_set_state(PROTO_COORD_VALIDATING_SESSION);
        g_session.b_active   = true;
        g_session.session_id = cfg.header.session_id;
        /* The decoder checked policy < 2. */
        g_session.policy      = (proto_policy_t)cfg.policy;
        g_session.frame_count = cfg.frame_count;
        g_session.rows        = cfg.rows;
        g_session.cols        = cfg.cols;
        g_session.required    = 0u;

        for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
        {
            g_session.node_map[slot] = cfg.node_map[slot];
            g_session.crc32[slot]    = cfg.crc32[slot];
            g_session.required |= cl_node_bit(cfg.node_map[slot]);
        }

        g_readiness         = 0u;
        g_sync              = 0u;
        g_b_resume_pending  = false;
        g_b_return_to_pause = false;
        g_b_pc_seq_known    = true;
        g_pc_seq            = cfg.header.seq;
        cl_evaluate();
    }
}

/**
 * @brief  Check a SESSION_CONFIG beyond the codec's ranges: session_id 0 is
 *         reserved for "no session", positions from rows x cols on are
 *         blank, at least one tile is required and no Node ID appears twice.
 *         Blank positions inside the wall are allowed (OD-21).
 * @param  p_cfg  The decoded message; the decoder checked rows x cols <= 9.
 * @return true when valid.
 */
static bool cl_session_valid (proto_session_config_t const * p_cfg)
{
    /* The map is row-major over rows x cols (layout_table.md, re-plan
     * step 3), so positions 0 to rows x cols - 1 are in the wall. */
    uint32_t const used = (uint32_t)p_cfg->rows * p_cfg->cols;
    uint16_t       seen = 0u;
    bool           b_ok = (0u != p_cfg->header.session_id);

    for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
    {
        uint8_t const  node_id = p_cfg->node_map[slot];
        uint16_t const bit     = cl_node_bit(node_id);
        bool const     b_in    = (slot < used);

        if ((0u != node_id) && ((!b_in) || (0u != (seen & bit))))
        {
            b_ok = false;
        }

        seen |= bit;
    }

    return (b_ok) && (0u != seen);
}

/**
 * @brief  Act on a dashboard command (T-B2-05).
 * @param  type  START_AT, PAUSE, RESUME_AT or STOP code.
 */
static void cl_pc_command (proto_type_t type)
{
    if (PROTO_TYPE_START_AT == type)
    {
        cl_cmd_start_at();
    }
    else if (PROTO_TYPE_PAUSE == type)
    {
        cl_cmd_pause();

        if (PROTO_COORD_COORDINATING_PLAYBACK == g_state)
        {
            cl_set_state(PROTO_COORD_PAUSED);
        }
    }
    else if (PROTO_TYPE_RESUME_AT == type)
    {
        cl_cmd_resume_at();
    }
    else if (PROTO_TYPE_STOP == type)
    {
        cl_cmd_stop();
    }
    else
    {
        g_stats.rx_bad++;
    }
}

/**
 * @brief  Start: accepted only in READY, which means every required tile
 *         sent a matching ASSET_READY and SYNC_READY (T-B2-04). START_AT
 *         goes to every tile 3 times; t_start is fixed at copy 1.
 */
static void cl_cmd_start_at (void)
{
    if (PROTO_COORD_READY == g_state)
    {
        cl_triple_send(PROTO_TYPE_START_AT, 0u, g_bcast_addr);
        cl_set_state(PROTO_COORD_COORDINATING_PLAYBACK);
    }
    else
    {
        cl_log_event(CL_EV_START_REFUSED, 0u, cl_state_code(), cl_now_us());
    }
}

/**
 * @brief  Pause: only while playing. Saves the frame shown now, which a
 *         later RESUME_AT carries, and sends PAUSE 3 times.
 */
static void cl_cmd_pause (void)
{
    if (PROTO_COORD_COORDINATING_PLAYBACK == g_state)
    {
        uint64_t const tick = cl_ticks_since(g_origin_us, cl_now_us());

        /* frame_count is 1 to 1023, so the remainder fits uint16_t. */
        g_saved_frame = (uint16_t)(tick % (uint64_t)g_session.frame_count);
        cl_triple_send(PROTO_TYPE_PAUSE, 0u, g_bcast_addr);
    }
}

/**
 * @brief  Resume: only while paused. W6 statechart 2 resynchronises first:
 *         if every required tile is SYNC_READY the RESUME_AT goes now,
 *         otherwise in SYNCHRONISING as soon as they are.
 */
static void cl_cmd_resume_at (void)
{
    if (PROTO_COORD_PAUSED == g_state)
    {
        g_b_return_to_pause = false;
        g_b_resume_pending  = true;
        cl_evaluate();
    }
}

/**
 * @brief  Stop: accepted while playing and while paused (W6D-2e); STOP goes
 *         3 times and the coordinator is READY again.
 */
static void cl_cmd_stop (void)
{
    if ((PROTO_COORD_COORDINATING_PLAYBACK == g_state) ||
        (PROTO_COORD_PAUSED == g_state))
    {
        cl_triple_send(PROTO_TYPE_STOP, 0u, g_bcast_addr);
        g_b_resume_pending  = false;
        g_b_return_to_pause = false;

        for (uint32_t index = 0u; index < CL_MAX_NODES; index++)
        {
            g_registry[index].b_lost = false;
        }

        cl_set_state(PROTO_COORD_READY);
        cl_evaluate();
    }
}

/**
 * @brief  Send RESUME_AT with the saved frame and enter COORDINATING
 *         PLAYBACK.
 */
static void cl_issue_resume (void)
{
    g_b_resume_pending = false;
    cl_triple_send(PROTO_TYPE_RESUME_AT, g_saved_frame, g_bcast_addr);

    for (uint32_t index = 0u; index < CL_MAX_NODES; index++)
    {
        g_registry[index].b_lost = false;
    }

    cl_set_state(PROTO_COORD_COORDINATING_PLAYBACK);
}

/**
 * @brief  Queue a command for 3 sends with one new seq (T-B2-05). If every
 *         slot is busy the oldest is replaced (a newer command supersedes
 *         it; 4 slots cover any sequence a user can click in 200 ms).
 * @param  type   Message type code.
 * @param  frame  RESUME_AT frame_index (0 otherwise).
 * @param  addr   Broadcast, or one tile for a rejoin.
 */
static void cl_triple_send (proto_type_t type, uint16_t frame, uint32_t addr)
{
    uint32_t target = 0u;

    for (uint32_t index = 1u; index < CL_SEND_SLOTS; index++)
    {
        bool const b_free_here   = !g_slots[index].b_busy;
        bool const b_target_busy = g_slots[target].b_busy;

        if ((b_target_busy) && ((b_free_here) || (g_slots[index].first_us <
                                                  g_slots[target].first_us)))
        {
            target = index;
        }
    }

    cl_send_slot_t * const p_slot = &g_slots[target];

    (void)memset(p_slot, 0, sizeof(*p_slot)); /* Returns p_slot. */
    p_slot->b_busy = true;
    p_slot->type   = type;
    p_slot->seq    = g_cmd_seq;
    p_slot->frame  = frame;
    p_slot->addr   = addr;
    p_slot->due_us = cl_now_us();
    g_cmd_seq++;
}

/**
 * @brief  A valid message from a tile: refresh its heartbeat time and
 *         address; a tile that was offline comes back online.
 * @param  node_id  1 to 9.
 * @param  addr     Its source address.
 * @param  t_rx     Receive time from the callback.
 */
static void cl_node_seen (uint8_t node_id, uint32_t addr, uint64_t t_rx)
{
    cl_node_t * const p_node = &g_registry[node_id - 1u];

    p_node->addr       = addr;
    p_node->last_heard = t_rx;

    if (!p_node->b_online)
    {
        p_node->b_online = true;
        cl_node_back(node_id);
    }
}

/**
 * @brief  A tile came back online: log it; in RECOVERING, return to the
 *         statechart path once every required tile is back.
 * @param  node_id  1 to 9.
 */
static void cl_node_back (uint8_t node_id)
{
    cl_log_event(CL_EV_BACK, node_id, 0u, cl_now_us());
    cl_game_sync_active();

    if ((PROTO_COORD_RECOVERING == g_state) &&
        (g_session.required == (cl_online_bits() & g_session.required)))
    {
        /* Valid asset: resynchronise; missing asset: back to readiness
         * (W6 statechart 2). cl_evaluate() picks the state. */
        cl_set_state(PROTO_COORD_WAITING_FOR_NODES);
        cl_evaluate();
    }
}

/**
 * @brief  Continue policy rejoin (OD-14 option (a), T-B2-04): a tile lost
 *         during playback whose asset is valid sends SYNC_READY; it then
 *         gets a RESUME_AT of its own for a tick at least 600 ms ahead.
 * @param  node_id  1 to 9.
 */
static void cl_maybe_rejoin (uint8_t node_id)
{
    cl_node_t * const p_node = &g_registry[node_id - 1u];
    uint16_t const    bit    = cl_node_bit(node_id);

    if ((p_node->b_lost) && (PROTO_COORD_COORDINATING_PLAYBACK == g_state) &&
        (0u != (g_readiness & bit)) && (0u != (g_sync & bit)))
    {
        p_node->b_lost = false;
        cl_triple_send(PROTO_TYPE_RESUME_AT, 0u, p_node->addr);
        cl_log_event(CL_EV_REJOIN_SENT, node_id, 0u, cl_now_us());
    }
}

/**
 * @brief  Move along the automatic part of W6 statechart 2 (solid arrows):
 *         required tiles online, then every ASSET_READY, then every
 *         SYNC_READY, then READY; a pending Resume or a return from
 *         recovery goes on to RESUME_AT or PAUSED. Only the pre-playback
 *         states move here.
 */
static void cl_evaluate (void)
{
    uint16_t const required = g_session.required;
    bool const     b_movable =
        (PROTO_COORD_VALIDATING_SESSION == g_state) ||
        (PROTO_COORD_WAITING_FOR_NODES == g_state) ||
        (PROTO_COORD_WAITING_FOR_TILE_READINESS == g_state) ||
        (PROTO_COORD_SYNCHRONISING == g_state) ||
        (PROTO_COORD_READY == g_state) ||
        ((PROTO_COORD_PAUSED == g_state) && (g_b_resume_pending));

    if ((!g_session.b_active) || (!b_movable))
    {
        /* Nothing automatic happens in this state. */
    }
    else if (required != (cl_online_bits() & required))
    {
        cl_set_state(PROTO_COORD_WAITING_FOR_NODES);
    }
    else if (required != (g_readiness & required))
    {
        cl_set_state(PROTO_COORD_WAITING_FOR_TILE_READINESS);
    }
    else if (required != (g_sync & required))
    {
        cl_set_state(PROTO_COORD_SYNCHRONISING);
    }
    else if (g_b_resume_pending)
    {
        cl_issue_resume();
    }
    else if (g_b_return_to_pause)
    {
        g_b_return_to_pause = false;
        cl_set_state(PROTO_COORD_PAUSED);
    }
    else
    {
        cl_set_state(PROTO_COORD_READY);
    }
}

/**
 * @brief  Change state and log the change.
 * @param  state  New state.
 */
static void cl_set_state (proto_coord_state_t state)
{
    if (state != g_state)
    {
        g_state = state;
        cl_log_event(CL_EV_STATE, 0u, cl_state_code(), cl_now_us());
    }
}

/**
 * @brief  Wrap-aware seq comparison (layout_table.md section 2).
 * @param  seq   Received seq.
 * @param  last  Last accepted seq.
 * @return true when seq is newer: (uint16_t)(seq - last) is 1 to 32,767.
 */
static bool cl_seq_newer (uint16_t seq, uint16_t last)
{
    uint16_t const ahead = (uint16_t)(seq - last); /* 16-bit wrap. */

    return (0u != ahead) && (ahead < UINT16_C(0x8000));
}

/**
 * @brief  Accept a tile message's seq when newer than the last accepted
 *         from that Node ID, and record it.
 * @param  node_id  1 to 9.
 * @param  seq      Received seq.
 * @return true when accepted.
 */
static bool cl_tile_seq_ok (uint8_t node_id, uint16_t seq)
{
    cl_node_t * const p_node = &g_registry[node_id - 1u];
    bool const        b_ok =
        (!p_node->b_seq_known) || (cl_seq_newer(seq, p_node->last_seq));

    if (b_ok)
    {
        p_node->b_seq_known = true;
        p_node->last_seq    = seq;
    }

    return b_ok;
}

/**
 * @brief  Map position of a Node ID in the active session.
 * @param  node_id  1 to 9.
 * @param  p_slot   Receives the position when found.
 * @return true when the Node ID is in the map.
 */
static bool cl_slot_of (uint8_t node_id, uint32_t * p_slot)
{
    bool b_found = false;

    for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
    {
        if ((!b_found) && (g_session.b_active) &&
            (node_id == g_session.node_map[slot]))
        {
            *p_slot = slot;
            b_found = true;
        }
    }

    return b_found;
}

/**
 * @brief  Online bitmap from the registry.
 * @return Bit n - 1 set when Node ID n is online.
 */
static uint16_t cl_online_bits (void)
{
    uint16_t bits = 0u;

    for (uint32_t index = 0u; index < CL_MAX_NODES; index++)
    {
        if (g_registry[index].b_online)
        {
            bits |= (uint16_t)(UINT16_C(1) << index); /* index < 9. */
        }
    }

    return bits;
}

/**
 * @brief  Bitmap bit of a Node ID.
 * @param  node_id  0 to 9 (0 = blank position, no bit).
 * @return Bit n - 1, or 0 for 0 or out of range.
 */
static uint16_t cl_node_bit (uint8_t node_id)
{
    uint16_t bit = 0u;

    if ((node_id >= 1u) && (node_id <= CL_MAX_NODES))
    {
        bit = (uint16_t)(UINT16_C(1) << (node_id - 1u)); /* Below 0x200. */
    }

    return bit;
}

/**
 * @brief  State as a number for logs.
 * @return proto_coord_state_t value, 0 to 11.
 */
static uint32_t cl_state_code (void)
{
    return (uint32_t)g_state; /* 12 states, all non-negative. */
}

/**
 * @brief  Header type as the enum.
 * @param  p_header  A header proto_decode_header() accepted.
 * @return The message type.
 */
static proto_type_t cl_type_of (proto_header_t const * p_header)
{
    return (proto_type_t)p_header->type; /* The decoder checked < 18. */
}

/**
 * @brief  Pico time of tick n: origin + ceil(n x 1,000,000 / 60), the same
 *         rounded-up deadline the tiles use (timesync_DEFENCE.md interface
 *         item 3), split so n x 10^6 never overflows.
 * @param  origin  Pico time of frame 0, microseconds.
 * @param  tick    Tick number.
 * @return Deadline, microseconds.
 */
static uint64_t cl_tick_deadline (uint64_t origin, uint64_t tick)
{
    uint64_t const whole = (tick / CL_FPS) * CL_US_PER_S;
    uint64_t const part =
        (((tick % CL_FPS) * CL_US_PER_S) + (CL_FPS - 1u)) / CL_FPS;

    return origin + whole + part;
}

/**
 * @brief  Ticks since origin: floor((t - origin) x 60 / 1,000,000), 0 before
 *         origin (W6D-1a), split so the product never overflows.
 * @param  origin  Pico time of frame 0.
 * @param  t_us    Time, microseconds.
 * @return Tick number.
 */
static uint64_t cl_ticks_since (uint64_t origin, uint64_t t_us)
{
    uint64_t ticks = 0u;

    if (t_us > origin)
    {
        uint64_t const elapsed = t_us - origin;

        ticks = ((elapsed / CL_US_PER_S) * CL_FPS) +
                (((elapsed % CL_US_PER_S) * CL_FPS) / CL_US_PER_S);
    }

    return ticks;
}

/**
 * @brief  Due time of the earliest pending command copy.
 * @return Microseconds, or UINT64_MAX when none is pending.
 */
static uint64_t cl_earliest_due (void)
{
    uint64_t due = UINT64_MAX;

    for (uint32_t index = 0u; index < CL_SEND_SLOTS; index++)
    {
        if ((g_slots[index].b_busy) && (g_slots[index].due_us < due))
        {
            due = g_slots[index].due_us;
        }
    }

    return due;
}

/**
 * @brief  Queue one event for the log routine; never prints here.
 * @param  code     cl_event_code_t value.
 * @param  node_id  Node ID or 0.
 * @param  value    Event value.
 * @param  now      Time, microseconds.
 */
static void cl_log_event (cl_event_code_t code, uint8_t node_id, uint32_t value,
                          uint64_t now)
{
    if (g_event_count < CL_EVENT_LEN)
    {
        uint32_t const index = (g_event_head + g_event_count) % CL_EVENT_LEN;

        g_events[index].code    = code;
        g_events[index].node_id = node_id;
        g_events[index].value   = value;
        g_events[index].t_us    = now;
        g_event_count++;
    }
    else
    {
        g_stats.events_lost++;
    }
}

/**
 * @brief  Print one queued event, or the 10 s statistics when due.
 * @param  now  Current time, microseconds.
 */
static void cl_log_routine (uint64_t now)
{
    static char const * const p_names[CL_EV_COUNT] = {"JOIN",
                                                      "LOSS",
                                                      "BACK",
                                                      "STATE",
                                                      "SESSION_REJECTED",
                                                      "START_REFUSED",
                                                      "CRC_MISMATCH",
                                                      "TILE_NACK",
                                                      "LINK_DOWN",
                                                      "REJOIN_SENT",
                                                      "SUPPRESS",
                                                      "GAME_REFUSED",
                                                      "GAME_MOLE",
                                                      "GAME_TAP",
                                                      "GAME_FEEDBACK"};

    if (0u != g_event_count)
    {
        cl_event_t const * const p_event = &g_events[g_event_head];

        /* Milliseconds since boot fit uint32_t for 49.7 days, longer than
         * any show; the newlib of Ubuntu's arm-none-eabi has no PRIu64
         * (built without _WANT_IO_C99_FORMATS). */
        uint32_t const t_ms = (uint32_t)(p_event->t_us / 1000u);

        printf("CL_EVENT t_ms=%" PRIu32 " %s node=%" PRIu8 " value=%" PRIu32
               "\n",
               t_ms, p_names[p_event->code], p_event->node_id, p_event->value);
        g_event_head = (g_event_head + 1u) % CL_EVENT_LEN;
        g_event_count--;
    }
    else if (now >= g_next_log_us)
    {
        cl_print_stats();
        g_next_log_us += CL_LOG_PERIOD_US;
    }
    else
    {
        /* Nothing to print. */
    }
}

/**
 * @brief  Print CL_STATS, LATE_HIST, CB_HIST and DISPATCH_HIST, one line
 *         each. The beacon send log passes "0 missing seq" (T-B2-02) when
 *         last_seq - first_seq + 1 = sent and send_fail = 0.
 */
static void cl_print_stats (void)
{
    printf("CL_STATS state=%" PRIu32 " sent=%" PRIu32 " first_seq=%" PRIu16
           " last_seq=%" PRIu16 " send_fail=%" PRIu32 " skipped=%" PRIu32
           " rx_dropped=%" PRIu32 " rx_bad=%" PRIu32 " stale=%" PRIu32
           " cmd_fail=%" PRIu32 " events_lost=%" PRIu32 " exc_max=%" PRIu32
           " cb_max_us=%" PRIu32 " dispatch_max_us=%" PRIu32
           " online=0x%03" PRIx16 " ready=0x%03" PRIx16 " sync=0x%03" PRIx16
           "\n",
           cl_state_code(), g_stats.sent, g_stats.first_seq, g_stats.last_seq,
           g_stats.send_fail, g_stats.skipped, g_stats.rx_dropped,
           g_stats.rx_bad, g_stats.stale, g_stats.cmd_fail, g_stats.events_lost,
           g_stats.exc_max, g_stats.cb_max_us, g_stats.dispatch_max_us,
           cl_online_bits(), g_readiness, g_sync);

    printf("LATE_HIST");

    for (uint32_t bin = 0u; bin < CL_LATE_BINS; bin++)
    {
        printf(",%" PRIu32, g_lateness_hist[bin]);
    }

    printf("\nCB_HIST");

    for (uint32_t bin = 0u; bin < CL_TIME_BINS; bin++)
    {
        printf(",%" PRIu32, g_cb_hist[bin]);
    }

    printf("\nDISPATCH_HIST");

    for (uint32_t bin = 0u; bin < CL_TIME_BINS; bin++)
    {
        printf(",%" PRIu32, g_dispatch_hist[bin]);
    }

    printf("\n");
}

/**
 * @brief  Add one value to a histogram; the last bin collects the rest.
 * @param  p_hist    Histogram.
 * @param  bins      Number of bins.
 * @param  value_us  Value, microseconds.
 * @param  bin_us    Bin width, microseconds.
 */
static void cl_hist_add (uint32_t * p_hist, uint32_t bins, uint64_t value_us,
                         uint64_t bin_us)
{
    uint64_t bin = value_us / bin_us;

    if (bin >= bins)
    {
        bin = bins - 1u;
    }

    p_hist[bin]++;
}

/**
 * @brief  Copy one datagram into the ring (callback context: no decode, no
 *         print). A full ring drops the datagram and counts it.
 * @param  listen_port  Port it arrived on.
 * @param  src_addr     Sender address.
 * @param  src_port     Sender port.
 * @param  p_buf        Bytes.
 * @param  len          Length; above CL_RX_MAX_BYTES it is kept as is so
 *                      the decoder rejects the wrong length.
 * @param  t_rx         Receive time.
 */
static void cl_enqueue (uint16_t listen_port, uint32_t src_addr,
                        uint16_t src_port, uint8_t const * p_buf, uint32_t len,
                        uint64_t t_rx)
{
    if (g_ring_count < CL_RING_LEN)
    {
        cl_rx_entry_t * const p_entry =
            &g_ring[(g_ring_head + g_ring_count) % CL_RING_LEN];
        uint32_t const copy = (len < CL_RX_MAX_BYTES) ? len : CL_RX_MAX_BYTES;

        p_entry->t_rx_us     = t_rx;
        p_entry->src_addr    = src_addr;
        p_entry->src_port    = src_port;
        p_entry->listen_port = listen_port;
        p_entry->len         = len;
        (void)memcpy(p_entry->bytes, p_buf, copy); /* Returns bytes. */
        g_ring_count++;
    }
    else
    {
        g_stats.rx_dropped++;
    }
}

/**
 * @brief  ACK of a message: header plus node_id and acked_seq, from the ACK
 *         counter (an ACK is not itself acked, so it has its own seq).
 * @param  addr        Destination address.
 * @param  port        Destination port.
 * @param  node_id     0 for the PC, else the tile.
 * @param  acked_seq   Header seq of the message acknowledged.
 * @param  session_id  Session on the header.
 */
static void cl_send_ack (uint32_t addr, uint16_t port, uint8_t node_id,
                         uint16_t acked_seq, uint8_t session_id)
{
    uint8_t              buf[PROTO_ACK_BYTES];
    proto_ack_t const    msg = {
        .header    = {.type       = PROTO_TYPE_ACK,
                      .session_id = session_id,
                      .seq        = g_ack_seq},
        .node_id   = node_id,
        .acked_seq = acked_seq,
    };
    uint32_t const len = proto_encode_ack(&msg, buf, sizeof(buf));

    /* A lost ACK is recovered by the sender's resend. */
    (void)cl_udp_send(addr, port, buf, len);
    g_ack_seq++;
}

/**
 * @brief  A TAP from a tile: ACKed every time (a repeat too, so the tile
 *         stops resending), relayed once to the PC on the STATUS path, and
 *         offered to the game while a round runs.
 * @param  p_entry   The datagram.
 * @param  p_header  Its decoded header.
 */
static void cl_on_tap (cl_rx_entry_t const * p_entry,
                       proto_header_t const * p_header)
{
    proto_tap_t msg;

    if (!proto_decode_tap(p_entry->bytes, p_entry->len, &msg))
    {
        g_stats.rx_bad++;
    }
    else
    {
        uint32_t const index = (uint32_t)msg.node_id - 1u; /* 1 to 9. */
        bool const     b_new = (!g_b_tap_seen[index]) ||
                           (cl_seq_newer(p_header->seq, g_tap_seq[index]));

        cl_send_ack(p_entry->src_addr, CL_PORT_TILE_RX, msg.node_id,
                    p_header->seq, g_session.session_id);

        if (b_new)
        {
            g_b_tap_seen[index] = true;
            g_tap_seq[index]    = p_header->seq;

            if (g_b_pc_known)
            {
                /* Relayed unchanged (layout_table.md section 11, port 4213). */
                (void)cl_udp_send(g_pc_addr, g_pc_port, p_entry->bytes,
                                  p_entry->len);
            }

            if ((PROTO_COORD_COORDINATING_GAME == g_state) &&
                (p_header->session_id == g_session.session_id))
            {
                game_rules_tap_rx(&g_game, msg.node_id, msg.peak, msg.t_tap,
                                  p_entry->t_rx_us);
                /* Modulo 2^32 on purpose: the sign shows in the top bit. */
                cl_log_event(CL_EV_GAME_TAP, msg.node_id,
                             (uint32_t)(p_entry->t_rx_us - msg.t_tap),
                             p_entry->t_rx_us);
            }
        }
        else
        {
            g_stats.stale++;
        }
    }
}

/**
 * @brief  An ACK from a tile: frees the outbox message it names.
 * @param  p_entry  The datagram.
 */
static void cl_on_tile_ack (cl_rx_entry_t const * p_entry)
{
    proto_ack_t msg;

    if ((!proto_decode_ack(p_entry->bytes, p_entry->len, &msg)) ||
        (0u == msg.node_id))
    {
        g_stats.rx_bad++;
    }
    else
    {
        /* Freed, or nothing waited: either way the ACK is done. */
        (void)ack_outbox_ack(&g_outbox, msg.node_id, msg.acked_seq);
    }
}

/**
 * @brief  GAME_CONTROL from the PC (node_id 0): a mode switch, START or END.
 *         Every valid one is ACKed, a repeat too, so the PC stops resending;
 *         only a new seq acts.
 * @param  p_entry   The datagram.
 * @param  p_header  Its decoded header.
 */
static void cl_on_game_control (cl_rx_entry_t const * p_entry,
                                proto_header_t const * p_header)
{
    proto_game_control_t msg;

    if ((!proto_decode_game_control(p_entry->bytes, p_entry->len, &msg)) ||
        (0u != msg.node_id))
    {
        g_stats.rx_bad++;
    }
    else
    {
        bool const b_new = (!g_b_pc_seq_known) ||
                           (cl_seq_newer(p_header->seq, g_pc_seq));

        if (b_new)
        {
            g_b_pc_seq_known = true;
            g_pc_seq         = p_header->seq;
            cl_game_command(msg.command);
        }
        else
        {
            g_stats.stale++;
        }

        cl_send_ack(g_pc_addr, g_pc_port, 0u, p_header->seq,
                    g_session.session_id);
    }
}

/**
 * @brief  Act on one GAME_CONTROL command from the PC.
 * @param  command  proto_game_cmd_t value (the decoder checked 0 to 3).
 */
static void cl_game_command (uint8_t command)
{
    uint64_t const now = cl_now_us();

    if ((PROTO_GAME_MODE_VIDEO == command) || (PROTO_GAME_MODE_GAME == command))
    {
        /* The mode changes only while stopped (layout_table.md 5.10). */
        if (cl_stopped())
        {
            g_b_game_mode = (PROTO_GAME_MODE_GAME == command);
        }
        else
        {
            cl_log_event(CL_EV_GAME_REFUSED, 0u, cl_state_code(), now);
        }
    }
    else if (PROTO_GAME_START == command)
    {
        cl_game_start(now);
    }
    else if (PROTO_GAME_END == command)
    {
        if (PROTO_COORD_COORDINATING_GAME == g_state)
        {
            cl_game_end(now); /* The PC's abort. */
        }
        else
        {
            cl_log_event(CL_EV_GAME_REFUSED, 0u, cl_state_code(), now);
        }
    }
    else
    {
        g_stats.rx_bad++;
    }
}

/**
 * @brief  START: from READY, in game mode, with an active session. The round
 *         starts now, and START goes to every tile of the map.
 * @param  now  Current time, microseconds.
 */
static void cl_game_start (uint64_t now)
{
    if ((PROTO_COORD_READY == g_state) && (g_b_game_mode) &&
        (g_session.b_active))
    {
        /* Bits 0 to 8, so the narrowing keeps them. */
        uint16_t const mask = (uint16_t)(g_session.required & cl_online_bits());

        game_rules_start(&g_game, now, mask);
        cl_set_state(PROTO_COORD_COORDINATING_GAME);

        for (uint32_t index = 0u; index < CL_MAX_NODES; index++)
        {
            /* index < 9. */
            uint8_t const node_id = (uint8_t)(index + 1u);

            if (0u != (mask & cl_node_bit(node_id)))
            {
                cl_queue_control(node_id, PROTO_GAME_START, now,
                                 CL_CONTROL_LIFE_US);
            }
        }
    }
    else
    {
        cl_log_event(CL_EV_GAME_REFUSED, 0u, cl_state_code(), now);
    }
}

/**
 * @brief  End of a round, by the timer or by the PC's abort: no more moles,
 *         END to every tile of the map, and the final score drawn across the
 *         wall as one WALL_PICTURE per tile. Back to READY.
 * @param  now  Current time, microseconds.
 */
static void cl_game_end (uint64_t now)
{
    int32_t const score = game_rules_score(&g_game);

    game_rules_stop(&g_game);
    ack_outbox_cancel(&g_outbox, PROTO_TYPE_MOLE_ON);

    for (uint32_t index = 0u; index < CL_MAX_NODES; index++)
    {
        /* index < 9. */
        uint8_t const node_id = (uint8_t)(index + 1u);

        if (0u != (g_session.required & cl_node_bit(node_id)))
        {
            cl_queue_control(node_id, PROTO_GAME_END, now, CL_CONTROL_LIFE_US);
            cl_queue_score(node_id, score, now);
        }
    }

    cl_set_state(PROTO_COORD_READY);
    cl_evaluate();
}

/**
 * @brief  Run the game engine's next event, if one is due: MOLE_ON and
 *         FEEDBACK go to their tile through the outbox; a round end ends the
 *         round.
 * @param  now  Current time, microseconds.
 */
static void cl_game_routine (uint64_t now)
{
    game_event_t event;

    if (game_rules_poll(&g_game, now, &event))
    {
        if (GAME_EV_MOLE_ON == event.kind)
        {
            cl_queue_mole(&event, now);
            cl_log_event(CL_EV_GAME_MOLE, event.node_id, event.up_ms,
                         event.t_show_us);
        }
        else if (GAME_EV_FEEDBACK == event.kind)
        {
            cl_queue_feedback(event.node_id, event.result, now);
            cl_log_event(CL_EV_GAME_FEEDBACK, event.node_id, event.result,
                         now);
        }
        else if (GAME_EV_ROUND_END == event.kind)
        {
            cl_game_end(now);
        }
        else
        {
            /* GAME_EV_NONE is never returned with true. */
        }
    }
}

/**
 * @brief  Send the due message of the acked outbox: one datagram, then its
 *         next resend is set (CL_ACK_RETRY_US later).
 * @param  now  Current time, microseconds.
 */
static void cl_outbox_routine (uint64_t now)
{
    uint32_t index = 0u;

    if (ack_outbox_due(&g_outbox, now, &index))
    {
        ack_outbox_entry_t const * const p_entry = &g_outbox.slots[index];

        if (!cl_udp_send(p_entry->addr, p_entry->port, p_entry->buf,
                         p_entry->len))
        {
            g_stats.cmd_fail++;
        }

        ack_outbox_sent(&g_outbox, index, now, CL_ACK_RETRY_US);
    }
}

/**
 * @brief  Queue an acked message to one tile at its REGISTER address.
 * @param  node_id      Tile, 1 to 9.
 * @param  type         Message type.
 * @param  seq          Header seq.
 * @param  p_buf        Encoded message.
 * @param  len          Its length.
 * @param  now          Current time, microseconds.
 * @param  deadline_us  Dropped at or after this time.
 */
static void cl_queue_tile (uint8_t node_id, proto_type_t type, uint16_t seq,
                           uint8_t const * p_buf, uint32_t len, uint64_t now,
                           uint64_t deadline_us)
{
    uint32_t const addr = g_registry[node_id - 1u].addr;

    if (0u != addr)
    {
        /* Message types fit 5 bits, so uint8_t holds them. */
        if (!ack_outbox_add(&g_outbox, node_id, (uint8_t)type, seq, addr,
                            CL_PORT_TILE_RX, p_buf, len, now, deadline_us))
        {
            g_stats.cmd_fail++;
        }
    }
}

/**
 * @brief  GAME_CONTROL to one tile (START or END).
 * @param  node_id  Tile, 1 to 9.
 * @param  command  proto_game_cmd_t value.
 * @param  now      Current time, microseconds.
 * @param  life_us  Lifetime after this send, microseconds.
 */
static void cl_queue_control (uint8_t node_id, uint8_t command, uint64_t now,
                              uint64_t life_us)
{
    uint16_t const seq = cl_next_cmd_seq();
    proto_game_control_t const msg = {
        .header  = {.type       = PROTO_TYPE_GAME_CONTROL,
                    .session_id = g_session.session_id,
                    .seq        = seq},
        .node_id = node_id,
        .command = command,
    };
    uint8_t  buf[PROTO_GAME_CONTROL_BYTES];
    uint32_t const len = proto_encode_game_control(&msg, buf, sizeof(buf));

    cl_queue_tile(node_id, PROTO_TYPE_GAME_CONTROL, seq, buf, len, now,
                  now + life_us);
}

/**
 * @brief  MOLE_ON for one tile. It lives until the show time: a late mole is
 *         worse than none.
 * @param  p_event  The engine's MOLE_ON.
 * @param  now      Current time, microseconds.
 */
static void cl_queue_mole (game_event_t const * p_event, uint64_t now)
{
    uint16_t const seq = cl_next_cmd_seq();
    proto_mole_on_t const msg = {
        .header  = {.type       = PROTO_TYPE_MOLE_ON,
                    .session_id = g_session.session_id,
                    .seq        = seq},
        .node_id = p_event->node_id,
        .t_show  = p_event->t_show_us,
        .up_ms   = p_event->up_ms,
    };
    uint8_t  buf[PROTO_MOLE_ON_BYTES];
    uint32_t const len = proto_encode_mole_on(&msg, buf, sizeof(buf));

    cl_queue_tile(p_event->node_id, PROTO_TYPE_MOLE_ON, seq, buf, len, now,
                  p_event->t_show_us);
}

/**
 * @brief  FEEDBACK to one tile: green on a hit, red on a miss.
 * @param  node_id  Tile, 1 to 9.
 * @param  result   proto_feedback_t value.
 * @param  now      Current time, microseconds.
 */
static void cl_queue_feedback (uint8_t node_id, uint8_t result, uint64_t now)
{
    uint16_t const seq = cl_next_cmd_seq();
    proto_feedback_msg_t const msg = {
        .header  = {.type       = PROTO_TYPE_FEEDBACK,
                    .session_id = g_session.session_id,
                    .seq        = seq},
        .node_id = node_id,
        .result  = result,
    };
    uint8_t  buf[PROTO_FEEDBACK_BYTES];
    uint32_t const len = proto_encode_feedback(&msg, buf, sizeof(buf));

    cl_queue_tile(node_id, PROTO_TYPE_FEEDBACK, seq, buf, len, now,
                  now + CL_FEEDBACK_LIFE_US);
}

/**
 * @brief  WALL_PICTURE for one tile: its 8 x 8 slice of the final score.
 * @param  node_id  Tile, 1 to 9, which must be in the map.
 * @param  score    The round's final score.
 * @param  now      Current time, microseconds.
 */
static void cl_queue_score (uint8_t node_id, int32_t score, uint64_t now)
{
    uint32_t slot = 0u;

    if (cl_slot_of(node_id, &slot))
    {
        uint16_t const seq = cl_next_cmd_seq();
        proto_wall_picture_t const msg = {
            .header  = {.type       = PROTO_TYPE_WALL_PICTURE,
                        .session_id = g_session.session_id,
                        .seq        = seq},
            .node_id = node_id,
            .mask    = score_font_tile_mask(score, g_session.rows,
                                            g_session.cols, cl_active_slots(),
                                            (uint8_t)slot), /* slot < 9. */
        };
        uint8_t  buf[PROTO_WALL_PICTURE_BYTES];
        uint32_t const len = proto_encode_wall_picture(&msg, buf, sizeof(buf));

        cl_queue_tile(node_id, PROTO_TYPE_WALL_PICTURE, seq, buf, len, now,
                      now + CL_CONTROL_LIFE_US);
    }
}

/**
 * @brief  Keep the game's tile set in step with the online bitmap while a
 *         round runs: a tile that drops out is not chosen for moles.
 */
static void cl_game_sync_active (void)
{
    if (PROTO_COORD_COORDINATING_GAME == g_state)
    {
        /* Bits 0 to 8, so the narrowing keeps them. */
        game_rules_set_active(
            &g_game, (uint16_t)(g_session.required & cl_online_bits()));
    }
}

/**
 * @brief  Whether the wall is stopped, so the mode may change: no session
 *         yet, or a session that is not playing, synchronising or running a
 *         round.
 * @return true when stopped.
 */
static bool cl_stopped (void)
{
    return (PROTO_COORD_WAITING_FOR_SESSION == g_state) ||
           (PROTO_COORD_VALIDATING_SESSION == g_state) ||
           (PROTO_COORD_WAITING_FOR_NODES == g_state) ||
           (PROTO_COORD_WAITING_FOR_TILE_READINESS == g_state) ||
           (PROTO_COORD_READY == g_state);
}

/**
 * @brief  Score for STATUS: the live score during a round, 0 otherwise
 *         (layout_table.md, STATUS score).
 * @return -99 to 99.
 */
static int8_t cl_status_score (void)
{
    int8_t score = 0;

    if (PROTO_COORD_COORDINATING_GAME == g_state)
    {
        score = (int8_t)game_rules_score(&g_game); /* -99 to 99. */
    }

    return score;
}

/**
 * @brief  Map positions that hold a Node ID, bit p for position p.
 * @return The bitmap.
 */
static uint16_t cl_active_slots (void)
{
    uint16_t bits = 0u;

    for (uint32_t slot = 0u; slot < PROTO_MAP_SLOTS; slot++)
    {
        if (0u != g_session.node_map[slot])
        {
            bits |= (uint16_t)(UINT16_C(1) << slot); /* slot < 9. */
        }
    }

    return bits;
}

/**
 * @brief  Next seq of the command counter, the one all acked messages share
 *         (layout_table.md section 10 item 13).
 * @return The seq to use.
 */
static uint16_t cl_next_cmd_seq (void)
{
    uint16_t const seq = g_cmd_seq;

    g_cmd_seq++;

    return seq;
}

#ifdef CL_HOST_TEST
/**
 * @brief  Current time (host test: the test's fake clock).
 * @return Microseconds.
 */
static uint64_t cl_now_us (void)
{
    return coordinator_port_now_us();
}

/**
 * @brief  Send one datagram (host test: recorded by the test).
 * @param  addr   Destination address.
 * @param  port   Destination port.
 * @param  p_buf  Bytes.
 * @param  len    Length.
 * @return true when sent.
 */
static bool cl_udp_send (uint32_t addr, uint16_t port, uint8_t const * p_buf,
                         uint32_t len)
{
    return coordinator_port_send(addr, port, p_buf, len);
}

/**
 * @brief  Send an encoded BEACON with t_p read just before the send.
 * @param  p_buf  Encoded beacon; t_p is written here.
 * @param  p_t_p  Receives the t_p sent.
 * @return true when sent.
 */
static bool cl_send_beacon_bytes (uint8_t * p_buf, uint64_t * p_t_p)
{
    *p_t_p = cl_now_us();
    proto_beacon_set_t_p(p_buf, *p_t_p);

    return cl_udp_send(g_bcast_addr, CL_PORT_TILE_RX, p_buf,
                       PROTO_BEACON_BYTES);
}

/**
 * @brief  Test: reset to WAITING FOR SESSION at the fake time.
 * @param  bcast_addr  Subnet broadcast address.
 */
void coordinator_loop_test_reset (uint32_t bcast_addr)
{
    cl_reset_state(bcast_addr, cl_now_us());
    g_beacon_seq = 0u;
    g_cmd_seq    = 0u;
    g_status_seq = 0u;
}

/**
 * @brief  Test: a datagram arrives at the fake time.
 * @param  listen_port  Port.
 * @param  src_addr     Sender address.
 * @param  src_port     Sender port.
 * @param  p_buf        Bytes.
 * @param  len          Length.
 */
void coordinator_loop_test_rx (uint16_t listen_port, uint32_t src_addr,
                               uint16_t src_port, uint8_t const * p_buf,
                               uint32_t len)
{
    cl_enqueue(listen_port, src_addr, src_port, p_buf, len, cl_now_us());
}

/**
 * @brief  Test: one loop pass.
 */
void coordinator_loop_test_pass (void)
{
    cl_pass();
}

/**
 * @brief  Test: state.
 * @return proto_coord_state_t value.
 */
proto_coord_state_t coordinator_loop_test_state (void)
{
    return g_state;
}

/**
 * @brief  Test: online bitmap.
 * @return The bitmap.
 */
uint16_t coordinator_loop_test_online (void)
{
    return cl_online_bits();
}

/**
 * @brief  Test: readiness bitmap.
 * @return The bitmap.
 */
uint16_t coordinator_loop_test_readiness (void)
{
    return g_readiness;
}

/**
 * @brief  Test: stale drops.
 * @return The count.
 */
uint32_t coordinator_loop_test_stale (void)
{
    return g_stats.stale;
}

/**
 * @brief  Test: skipped beacon slots.
 * @return The count.
 */
uint32_t coordinator_loop_test_skipped (void)
{
    return g_stats.skipped;
}

/**
 * @brief  Test: when the last loss of a node was detected.
 * @param  node_id  1 to 9.
 * @return Microseconds, or 0.
 */
uint64_t coordinator_loop_test_loss_time (uint8_t node_id)
{
    uint64_t when = 0u;

    if ((node_id >= 1u) && (node_id <= CL_MAX_NODES))
    {
        when = g_registry[node_id - 1u].loss_time_us;
    }

    return when;
}

/**
 * @brief  Test: force the state (no event is logged, nothing is sent).
 * @param  state  The state to set.
 */
void coordinator_loop_test_set_state (proto_coord_state_t state)
{
    g_state = state;
}
#else
static struct udp_pcb * g_p_tx_pcb = NULL;
static uint32_t         g_join_failures = 0u; /* Retries scheduled so far. */
static uint64_t         g_next_join_us  = 0u; /* Next join attempt, us. */
static bool             g_b_pcbs_open   = false;

static bool cl_connect_router (void);
static bool cl_try_join (void);
static void cl_schedule_join (uint64_t now);
static void cl_rejoin_routine (uint64_t now);
static void cl_open_pcbs (void);
static void cl_rx_cb (void * p_arg, struct udp_pcb * p_pcb,
                      struct pbuf * p_pbuf, ip_addr_t const * p_addr,
                      u16_t port);

/**
 * @brief  Join the router and run forever (see the file comment). A failed
 *         join leaves the coordinator in FAULT, and cl_rejoin_routine()
 *         retries it on the join_backoff_ms() schedule.
 */
void coordinator_loop_run (void)
{
    cl_set_state(PROTO_COORD_CONNECTING_TO_ROUTER);
    (void)cl_try_join(); /* A failure schedules the next attempt. */

    for (;;)
    {
        cyw43_arch_poll();

        if (PROTO_COORD_FAULT != g_state)
        {
            cl_pass();
        }
        else
        {
            /* FAULT: retry the join when it is due; the log routine still
             * prints queued events. */
            cl_rejoin_routine(cl_now_us());
            cl_log_routine(cl_now_us());
        }
    }
}

/**
 * @brief  One router join attempt. On success the PCBs are opened once and
 *         the coordinator starts again from WAITING FOR SESSION: a session
 *         does not survive a lost link, so the PC sends SESSION_CONFIG
 *         again. On failure it stays in FAULT and schedules the next try.
 * @return true when joined with an IP address.
 */
static bool cl_try_join (void)
{
    bool const b_joined = cl_connect_router();

    if (b_joined)
    {
        struct netif * const p_netif = &cyw43_state.netif[CYW43_ITF_STA];
        uint32_t const       addr = ip4_addr_get_u32(netif_ip4_addr(p_netif));
        uint32_t const mask = ip4_addr_get_u32(netif_ip4_netmask(p_netif));

        if (!g_b_pcbs_open)
        {
            cl_open_pcbs();
            g_b_pcbs_open = true;
        }

        g_join_failures = 0u;
        cl_reset_state(addr | ~mask, cl_now_us());
        printf("COORDINATOR up: ip=%s, beacons on %u, tiles to %u, PC to %u\n",
               ip4addr_ntoa(netif_ip4_addr(p_netif)), CL_PORT_TILE_RX,
               CL_PORT_FROM_TILE, CL_PORT_FROM_PC);
    }
    else
    {
        cl_set_state(PROTO_COORD_FAULT);
        cl_schedule_join(cl_now_us());
    }

    return b_joined;
}

/**
 * @brief  Schedule the next join attempt: join_backoff_ms() of the retries
 *         so far, then one more retry is counted. Reports the wait.
 * @param  now  Current time, microseconds.
 */
static void cl_schedule_join (uint64_t now)
{
    uint32_t const wait_ms = join_backoff_ms(g_join_failures);

    g_join_failures++;
    g_next_join_us = now + ((uint64_t)wait_ms * 1000u); /* ms to us. */
    printf("COORDINATOR FAULT: router join not done in %u ms; retry in %u ms\n",
           CL_CONNECT_TIMEOUT_MS, wait_ms);
}

/**
 * @brief  In FAULT: retry the router join when it is due.
 * @param  now  Current time, microseconds.
 */
static void cl_rejoin_routine (uint64_t now)
{
    if (now >= g_next_join_us)
    {
        (void)cl_try_join(); /* The result is in g_state. */
    }
}

/**
 * @brief  The router link dropped: FAULT, then rejoin on the backoff
 *         schedule. The first retry waits 1 s, as after a cold boot
 *         (W6 statechart 2).
 * @param  now  Current time, microseconds.
 */
static void cl_on_link_lost (uint64_t now)
{
    game_rules_stop(&g_game); /* A round does not survive a lost router. */
    cl_set_state(PROTO_COORD_FAULT);
    g_join_failures = 0u;
    cl_schedule_join(now);
}

/**
 * @brief  CONNECTING TO ROUTER, bounded by the 10 s timeout (W6D-2d,
 *         T-B2-03); power save off once joined (SRM-8Mc).
 * @return true when joined with an IP address.
 */
static bool cl_connect_router (void)
{
    uint32_t const auth =
        ('\0' != ROUTER_PASS[0]) ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;

    cyw43_arch_enable_sta_mode();
    printf("Joining %s (timeout %u ms)\n", ROUTER_SSID, CL_CONNECT_TIMEOUT_MS);

    bool const b_joined =
        (0 == cyw43_arch_wifi_connect_timeout_ms(
                  ROUTER_SSID, ('\0' != ROUTER_PASS[0]) ? ROUTER_PASS : NULL,
                  auth, CL_CONNECT_TIMEOUT_MS));

    if (b_joined)
    {
        /* Radio never sleeps: fewer late and lost beacons (SRM-8Mc). */
        (void)cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
    }

    return b_joined;
}

/**
 * @brief  One send pcb and the two receive pcbs (tiles and PC).
 */
static void cl_open_pcbs (void)
{
    static uint16_t const ports[2] = {CL_PORT_FROM_TILE, CL_PORT_FROM_PC};

    g_p_tx_pcb = udp_new();

    for (uint32_t index = 0u; index < 2u; index++)
    {
        struct udp_pcb * const p_pcb = udp_new();

        if ((NULL != p_pcb) &&
            (ERR_OK == udp_bind(p_pcb, IP_ANY_TYPE, ports[index])))
        {
            /* The port number travels as the callback argument; casting a
             * small integer to a pointer and back keeps its value. */
            udp_recv(p_pcb, cl_rx_cb, (void *)(uintptr_t)ports[index]);
        }
        else
        {
            printf("COORDINATOR: cannot open port %u\n", ports[index]);
        }
    }
}

/**
 * @brief  lwIP receive callback, poll context (W6D-5b): timestamp, copy into
 *         the ring and return. Also records the exception number it runs
 *         under (OD-01 check: must be 0) and its own duration (T-B2-06).
 * @param  p_arg   Listening port, as an integer.
 * @param  p_pcb   Unused.
 * @param  p_pbuf  Datagram; freed here.
 * @param  p_addr  Sender address.
 * @param  port    Sender port.
 */
static void cl_rx_cb (void * p_arg, struct udp_pcb * p_pcb,
                      struct pbuf * p_pbuf, ip_addr_t const * p_addr,
                      u16_t port)
{
    uint64_t const t_rx      = time_us_64();
    uint32_t const exception = __get_current_exception();
    uint8_t        buf[CL_RX_MAX_BYTES];

    (void)p_pcb; /* The port comes from p_arg. */

    if (exception > g_stats.exc_max)
    {
        g_stats.exc_max = exception;
    }

    if (NULL != p_pbuf)
    {
        /* pbuf_copy_partial copies at most CL_RX_MAX_BYTES, a u16_t. */
        (void)pbuf_copy_partial(p_pbuf, buf, CL_RX_MAX_BYTES, 0);
        /* p_arg holds a port number below 65,536 (cl_open_pcbs()). */
        cl_enqueue((uint16_t)(uintptr_t)p_arg, ip4_addr_get_u32(p_addr), port,
                   buf, p_pbuf->tot_len, t_rx);
        (void)pbuf_free(p_pbuf); /* Returns the number of pbufs freed. */
    }

    uint64_t const spent = time_us_64() - t_rx;

    cl_hist_add(g_cb_hist, CL_TIME_BINS, spent, CL_TIME_BIN_US);

    if (spent > g_stats.cb_max_us)
    {
        /* A callback takes far less than 2^32 us. */
        g_stats.cb_max_us = (uint32_t)spent;
    }
}

/**
 * @brief  Current time: time_us_64(), the Pico timer (64-bit, LDC-6).
 * @return Microseconds since boot.
 */
static uint64_t cl_now_us (void)
{
    return time_us_64();
}

/**
 * @brief  Send one datagram from the send pcb.
 * @param  addr   Destination (network order u32).
 * @param  port   Destination port.
 * @param  p_buf  Bytes.
 * @param  len    Length.
 * @return true when lwIP accepted it.
 */
static bool cl_udp_send (uint32_t addr, uint16_t port, uint8_t const * p_buf,
                         uint32_t len)
{
    bool b_sent = false;

    /* len is a message length, at most 46 B, so it fits u16_t. */
    struct pbuf * const p_pbuf =
        pbuf_alloc(PBUF_TRANSPORT, (u16_t)len, PBUF_RAM);

    if ((NULL != p_pbuf) && (NULL != g_p_tx_pcb))
    {
        ip_addr_t dest;

        ip_addr_set_ip4_u32(&dest, addr);
        (void)memcpy(p_pbuf->payload, p_buf, len); /* Returns payload. */
        b_sent = (ERR_OK == udp_sendto(g_p_tx_pcb, p_pbuf, &dest, port));
    }

    if (NULL != p_pbuf)
    {
        (void)pbuf_free(p_pbuf); /* Returns the number of pbufs freed. */
    }

    return b_sent;
}

/**
 * @brief  Send an encoded BEACON (T-B2-02): the pbuf is prepared first, then
 *         t_p = time_us_64() is read and written into the payload, and
 *         udp_sendto follows with only that write in between.
 * @param  p_buf  Encoded beacon (t_p 0).
 * @param  p_t_p  Receives the t_p sent.
 * @return true when lwIP accepted it.
 */
static bool cl_send_beacon_bytes (uint8_t * p_buf, uint64_t * p_t_p)
{
    bool b_sent = false;

    struct pbuf * const p_pbuf =
        pbuf_alloc(PBUF_TRANSPORT, PROTO_BEACON_BYTES, PBUF_RAM);

    if ((NULL != p_pbuf) && (NULL != g_p_tx_pcb))
    {
        ip_addr_t       dest;
        uint8_t * const p_payload = (uint8_t *)p_pbuf->payload; /* PBUF_RAM:
                                                     one contiguous block. */

        ip_addr_set_ip4_u32(&dest, g_bcast_addr);
        (void)memcpy(p_payload, p_buf, PROTO_BEACON_BYTES); /* Returns it. */
        *p_t_p = time_us_64();
        proto_beacon_set_t_p(p_payload, *p_t_p);
        b_sent =
            (ERR_OK == udp_sendto(g_p_tx_pcb, p_pbuf, &dest, CL_PORT_TILE_RX));
    }
    else
    {
        *p_t_p = time_us_64();
    }

    if (NULL != p_pbuf)
    {
        (void)pbuf_free(p_pbuf); /* Returns the number of pbufs freed. */
    }

    return b_sent;
}
#endif

/*** end of file ***/
