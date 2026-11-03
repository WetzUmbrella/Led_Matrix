/**
 * @file    playback_control.c
 * @brief   Playback Control (T-B4-04). See playback_control.h.
 *
 * Owner: Ahsanul (Buddy 4).
 *
 * The core (everything above the wrapper section) runs only in the
 * Playback Control Task, so its state needs no lock. The one thing read
 * from another task is the timeline, published under its own sequence
 * counter with the VC-1 pattern: writer odd store, release fence, relaxed
 * 32-bit atomic words, even store with release; reader acquire load, copy,
 * acquire fence, reload, at most 3 attempts.
 *
 * DEVIATION D-1 (module-wide): C11 stdatomic.h for that seqlock.
 * DEVIATION D-2 (module-wide): ESP-IDF and FreeRTOS names in the wrapper.
 */
#include "playback_control.h"

#include <stdatomic.h>
#include <stddef.h>

#include "fault_word.h"
#include "proto.h"

#ifndef TIMESYNC_HOST_TEST
#include <inttypes.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "asset_store.h"
#include "tile_net.h"
#include "tile_tasks.h"
#include "timesync.h"
#else
#include "timesync.h"
#endif

/** Microseconds per second and frames per second (W6D-1a). */
#define PB_US_PER_S INT64_C(1000000)
#define PB_FPS      INT64_C(60)

/** Timeline words under the seqlock. */
#define PB_WORD_FLAGS       0u
#define PB_WORD_ORIGIN_LOW  1u
#define PB_WORD_ORIGIN_HIGH 2u
#define PB_WORD_FRAMES      3u
#define PB_WORD_COUNT       4u

/** Timeline flag bits and the hold frame's position in the flags word. */
#define PB_FLAG_RUNNING  0x1u
#define PB_FLAG_HELD     0x2u
#define PB_HOLD_SHIFT    16u
#define PB_SEQ_ODD       0x1u
#define PB_READ_ATTEMPTS 3u

/** The timeline seqlock. */
typedef struct
{
    _Atomic uint32_t seq;
    _Atomic uint32_t words[PB_WORD_COUNT];
} pb_timeline_lock_t;

/* Core state: Playback Control Task only. */
static playback_control_state_t g_state = PLAYBACK_INITIALISING;

/* Copy of g_state for other tasks (asset_store, tile_net): written with
 * release only where g_state changes, read with acquire (DEVIATION D-1). */
static _Atomic uint32_t g_state_pub   = PLAYBACK_INITIALISING;
static bool             g_b_seq_known = false;
static uint16_t         g_last_seq    = 0u;
static uint8_t          g_session     = 0u;
static bool             g_b_session   = false;
static int64_t          g_origin_us   = 0;
static int64_t          g_target_us   = 0;
static uint16_t         g_frames      = 1u;
static uint16_t         g_hold_frame  = 0u;
static bool             g_b_lost_set  = false;
static uint32_t         g_rejected    = 0u;

/* Written by the Playback Control Task, read by any task. */
static pb_timeline_lock_t g_timeline;

static void     pb_check_session (playback_control_inputs_t const * p_in);
static bool     pb_accept_seq (playback_control_command_t const * p_cmd);
static bool     pb_on_command (playback_control_command_t const * p_cmd,
                               playback_control_inputs_t const *  p_in);
static bool     pb_arm (int64_t origin_us, int64_t target_us);
static void     pb_pause_at (int64_t t_pico_us);
static void     pb_holdover (playback_control_inputs_t const * p_in);
static void     pb_set_state (playback_control_state_t state);
static void     pb_publish (void);
static int64_t  pb_tick_offset (uint64_t tick);
static uint32_t pb_frame_on (int64_t origin_us, int64_t t_pico_us);
static bool     pb_seq_newer (uint16_t seq, uint16_t last);

/**
 * @brief  Reset to INITIALISING.
 */
void playback_control_init (void)
{
    g_state = PLAYBACK_INITIALISING;
    /* Enum value 0 fits uint32_t. */
    atomic_store_explicit(&g_state_pub, (uint32_t)PLAYBACK_INITIALISING,
                          memory_order_release);
    g_b_seq_known = false;
    g_b_session   = false;
    g_origin_us   = 0;
    g_target_us   = 0;
    g_frames      = 1u;
    g_hold_frame  = 0u;
    g_b_lost_set  = false;
    g_rejected    = 0u;
    atomic_store_explicit(&g_timeline.seq, 0u, memory_order_relaxed);

    for (uint32_t word = 0u; word < PB_WORD_COUNT; word++)
    {
        atomic_store_explicit(&g_timeline.words[word], 0u,
                              memory_order_relaxed);
    }
}

/**
 * @brief  Automatic transitions of statechart 4 and the holdover.
 * @param  p_in  Inputs read now.
 */
void playback_control_step (playback_control_inputs_t const * p_in)
{
    bool const b_inputs = (p_in->b_wifi) && (p_in->b_asset);

    pb_check_session(p_in);

    if (PLAYBACK_INITIALISING == g_state)
    {
        pb_set_state(PLAYBACK_WAITING_FOR_READINESS);
    }
    else if ((PLAYBACK_WAITING_FOR_READINESS == g_state) && (b_inputs))
    {
        pb_set_state(PLAYBACK_SYNCHRONISING);
    }
    else if (((PLAYBACK_SYNCHRONISING == g_state) ||
              (PLAYBACK_READY == g_state)) &&
             (!b_inputs))
    {
        pb_set_state(PLAYBACK_WAITING_FOR_READINESS);
    }
    else if ((PLAYBACK_SYNCHRONISING == g_state) && (p_in->b_sync_ready))
    {
        pb_set_state(PLAYBACK_READY);
    }
    else if ((PLAYBACK_READY == g_state) && (!p_in->b_sync_ready))
    {
        /* "Clock error exceeds tolerance" (statechart 4, dashed). */
        pb_set_state(PLAYBACK_SYNCHRONISING);
    }
    else if ((PLAYBACK_ARMED == g_state) && (p_in->now_pico_us >= g_target_us))
    {
        pb_set_state(PLAYBACK_PLAYING);
    }
    else
    {
        pb_holdover(p_in);
    }
}

/**
 * @brief  Act on one command once per (session_id, seq).
 * @param  p_cmd  The command.
 * @param  p_in   Inputs read now.
 * @return true when the command caused a state transition.
 */
bool playback_control_on_command (playback_control_command_t const * p_cmd,
                                  playback_control_inputs_t const *  p_in)
{
    bool b_moved = false;

    pb_check_session(p_in);

    if ((!g_b_session) || (p_cmd->session_id != g_session))
    {
        g_rejected++; /* Another session (IFM-2a, T-B4-04). */
    }
    else if (pb_accept_seq(p_cmd))
    {
        b_moved = pb_on_command(p_cmd, p_in);
    }
    else
    {
        /* A repeat copy (ignored) or an older seq (counted in
         * pb_accept_seq). */
    }

    return b_moved;
}

/**
 * @brief  Current state.
 * @return The state.
 */
playback_control_state_t playback_control_state (void)
{
    /* g_state_pub only ever holds a playback_control_state_t value. Any
     * task may call this (acquire load of the published copy). */
    return (playback_control_state_t)atomic_load_explicit(&g_state_pub,
                                                          memory_order_acquire);
}

/**
 * @brief  Frame at Pico time t on the current timeline.
 * @param  t_pico_us  Pico time.
 * @return Frame index.
 */
uint32_t playback_control_frame_at (int64_t t_pico_us)
{
    uint32_t frame = 0u;

    if (PLAYBACK_PAUSED == g_state)
    {
        frame = g_hold_frame;
    }
    else
    {
        frame = pb_frame_on(g_origin_us, t_pico_us);
    }

    return frame;
}

/**
 * @brief  Read the published timeline.
 * @param  p_out  Receives the timeline.
 * @return true when a stable set was read.
 */
bool playback_control_read_timeline (playback_control_timeline_t * p_out)
{
    bool     b_done  = false;
    uint32_t attempt = 0u;

    while ((!b_done) && (attempt < PB_READ_ATTEMPTS))
    {
        uint32_t       words[PB_WORD_COUNT];
        uint32_t const seq_1 =
            atomic_load_explicit(&g_timeline.seq, memory_order_acquire);

        for (uint32_t word = 0u; word < PB_WORD_COUNT; word++)
        {
            words[word] = atomic_load_explicit(&g_timeline.words[word],
                                               memory_order_relaxed);
        }

        atomic_thread_fence(memory_order_acquire);

        uint32_t const seq_2 =
            atomic_load_explicit(&g_timeline.seq, memory_order_relaxed);

        if ((0u == (seq_1 & PB_SEQ_ODD)) && (seq_1 == seq_2))
        {
            uint32_t const flags = words[PB_WORD_FLAGS];
            /* The two halves rebuild the int64_t origin bit for bit. */
            uint64_t const origin =
                ((uint64_t)words[PB_WORD_ORIGIN_HIGH] << 32u) |
                words[PB_WORD_ORIGIN_LOW];

            p_out->b_running = (0u != (flags & PB_FLAG_RUNNING));
            p_out->b_held    = (0u != (flags & PB_FLAG_HELD));
            p_out->origin_us = (int64_t)origin; /* Same bits back. */
            /* frame_count is at most 1023, stored in the low 16 bits. */
            p_out->frame_count = (uint16_t)words[PB_WORD_FRAMES];
            /* hold_frame is at most 1023, stored in the high 16 bits. */
            p_out->hold_frame = (uint16_t)(flags >> PB_HOLD_SHIFT);
            b_done            = true;
        }

        attempt++;
    }

    return b_done;
}

/**
 * @brief  Rejected commands.
 * @return The count.
 */
uint32_t playback_control_rejected (void)
{
    return g_rejected;
}

/**
 * @brief  Track the asset's session: a new session restarts the seq
 *         tracking; an invalid frame_count is FAULT; a new valid session
 *         leaves FAULT (statechart 4).
 * @param  p_in  Inputs read now.
 */
static void pb_check_session (playback_control_inputs_t const * p_in)
{
    bool const b_frames_ok =
        (p_in->frame_count >= 1u) && (p_in->frame_count <= PROTO_FRAME_MAX);

    if (!p_in->b_asset)
    {
        /* No asset: no session to check against yet. */
    }
    else if (!b_frames_ok)
    {
        pb_set_state(PLAYBACK_FAULT);
    }
    else if ((!g_b_session) || (p_in->session_id != g_session))
    {
        g_b_session   = true;
        g_session     = p_in->session_id;
        g_frames      = p_in->frame_count;
        g_b_seq_known = false;

        if (PLAYBACK_FAULT == g_state)
        {
            pb_set_state(PLAYBACK_WAITING_FOR_READINESS);
        }
    }
    else
    {
        g_frames = p_in->frame_count;
    }
}

/**
 * @brief  The once-per-(session_id, seq) rule (T-B2-05 sends 3 copies).
 * @param  p_cmd  The command (session already matched).
 * @return true for a newer seq, recorded as accepted.
 */
static bool pb_accept_seq (playback_control_command_t const * p_cmd)
{
    bool b_new = false;

    if ((g_b_seq_known) && (p_cmd->seq == g_last_seq))
    {
        b_new = false; /* Copy 2 or 3 of a command already acted on. */
    }
    else if ((g_b_seq_known) && (!pb_seq_newer(p_cmd->seq, g_last_seq)))
    {
        g_rejected++; /* Older than the last accepted: stale. */
    }
    else
    {
        g_b_seq_known = true;
        g_last_seq    = p_cmd->seq;
        b_new         = true;
    }

    return b_new;
}

/**
 * @brief  The command transitions of statechart 4.
 * @param  p_cmd  A new command of the active session.
 * @param  p_in   Inputs read now.
 * @return true when the state changed.
 */
static bool pb_on_command (playback_control_command_t const * p_cmd,
                           playback_control_inputs_t const *  p_in)
{
    playback_control_state_t const before = g_state;
    bool const                     b_playable =
        (PLAYBACK_READY == g_state) || (PLAYBACK_ARMED == g_state) ||
        (PLAYBACK_PLAYING == g_state) || (PLAYBACK_CLOCK_HOLDOVER == g_state) ||
        (PLAYBACK_PAUSED == g_state);
    bool const b_moving = (PLAYBACK_ARMED == g_state) ||
                          (PLAYBACK_PLAYING == g_state) ||
                          (PLAYBACK_CLOCK_HOLDOVER == g_state);
    /* t_start and t_resume are Pico times below 2^63 us (292,000 years). */
    int64_t const t_cmd = (int64_t)p_cmd->t_us;
    /* tile_net queues only decoded commands: type is below 18. */
    proto_type_t const type = (proto_type_t)p_cmd->type;

    if ((PROTO_TYPE_START_AT == type) && (b_playable))
    {
        (void)pb_arm(t_cmd, t_cmd); /* Frame 0 at t_start. */
    }
    else if ((PROTO_TYPE_RESUME_AT == type) && (b_playable) &&
             (p_cmd->frame_index < g_frames))
    {
        /* The saved frame shows at t_resume (T-B4-04). */
        (void)pb_arm(t_cmd - pb_tick_offset(p_cmd->frame_index), t_cmd);
    }
    else if ((PROTO_TYPE_PAUSE == type) && (b_moving))
    {
        pb_pause_at(p_in->now_pico_us);
    }
    else if ((PROTO_TYPE_STOP == type) &&
             ((b_moving) || (PLAYBACK_PAUSED == g_state)))
    {
        pb_set_state(PLAYBACK_READY); /* W6D-2e: also from PAUSED. */
    }
    else
    {
        /* Not valid in this state: no transition. */
    }

    return (before != g_state);
}

/**
 * @brief  ARMED until the corrected Pico time reaches target_us.
 * @param  origin_us  Pico time of frame 0.
 * @param  target_us  t_start or t_resume.
 * @return true.
 */
static bool pb_arm (int64_t origin_us, int64_t target_us)
{
    g_origin_us = origin_us;
    g_target_us = target_us;
    pb_set_state(PLAYBACK_ARMED);

    return true;
}

/**
 * @brief  PAUSED: hold the frame shown at t and keep it for RESUME_AT.
 * @param  t_pico_us  Pico time of the pause.
 */
static void pb_pause_at (int64_t t_pico_us)
{
    /* The frame is below frame_count (at most 1023), so it fits. */
    g_hold_frame = (uint16_t)pb_frame_on(g_origin_us, t_pico_us);
    pb_set_state(PLAYBACK_PAUSED);
}

/**
 * @brief  PLAYING and CLOCK HOLDOVER by beacon age; the 1 s holdover limit
 *         pauses safely and raises BEACON_LOSS (VA-4.3, W6D-2b); beacons
 *         back clear it (owner: this module).
 * @param  p_in  Inputs read now.
 */
static void pb_holdover (playback_control_inputs_t const * p_in)
{
    bool const b_absent = (p_in->beacon_age_us > PLAYBACK_HOLDOVER_ENTER_US);

    if ((PLAYBACK_PLAYING == g_state) && (b_absent))
    {
        pb_set_state(PLAYBACK_CLOCK_HOLDOVER);
    }
    else if ((PLAYBACK_CLOCK_HOLDOVER == g_state) &&
             (p_in->beacon_age_us > TIMESYNC_HOLDOVER_US))
    {
        pb_pause_at(p_in->now_pico_us);
        fault_word_set(FAULT_BIT_BEACON_LOSS);
        g_b_lost_set = true;
    }
    else if ((PLAYBACK_CLOCK_HOLDOVER == g_state) && (!b_absent))
    {
        pb_set_state(PLAYBACK_PLAYING); /* Beacon restored. */
    }
    else if ((g_b_lost_set) && (!b_absent))
    {
        fault_word_clear_condition(FAULT_BIT_BEACON_LOSS);
        g_b_lost_set = false;
    }
    else
    {
        /* Nothing changes. */
    }
}

/**
 * @brief  Change state and publish the timeline.
 * @param  state  New state.
 */
static void pb_set_state (playback_control_state_t state)
{
    g_state = state;
    /* An enum value of 0 to 8 fits uint32_t. */
    atomic_store_explicit(&g_state_pub, (uint32_t)state, memory_order_release);
    pb_publish();
}

/**
 * @brief  Publish the timeline (single writer, VC-1 pattern).
 */
static void pb_publish (void)
{
    bool const b_running = (PLAYBACK_ARMED == g_state) ||
                           (PLAYBACK_PLAYING == g_state) ||
                           (PLAYBACK_CLOCK_HOLDOVER == g_state);
    uint32_t flags = ((uint32_t)g_hold_frame) << PB_HOLD_SHIFT; /* 10 bits. */
    /* The int64_t origin is stored as its two 32-bit halves, bit for bit. */
    uint64_t const origin = (uint64_t)g_origin_us;

    if (b_running)
    {
        flags |= PB_FLAG_RUNNING;
    }

    if (PLAYBACK_PAUSED == g_state)
    {
        flags |= PB_FLAG_HELD;
    }

    uint32_t const seq =
        atomic_load_explicit(&g_timeline.seq, memory_order_relaxed);

    atomic_store_explicit(&g_timeline.seq, seq + 1u, memory_order_relaxed);
    atomic_thread_fence(memory_order_release);
    atomic_store_explicit(&g_timeline.words[PB_WORD_FLAGS], flags,
                          memory_order_relaxed);
    /* Low half: the mask keeps 32 bits. */
    atomic_store_explicit(&g_timeline.words[PB_WORD_ORIGIN_LOW],
                          (uint32_t)(origin & UINT32_MAX),
                          memory_order_relaxed);
    /* High half: the shift leaves 32 bits. */
    atomic_store_explicit(&g_timeline.words[PB_WORD_ORIGIN_HIGH],
                          (uint32_t)(origin >> 32u), memory_order_relaxed);
    atomic_store_explicit(&g_timeline.words[PB_WORD_FRAMES], g_frames,
                          memory_order_relaxed);
    atomic_store_explicit(&g_timeline.seq, seq + 2u, memory_order_release);
}

/**
 * @brief  Offset of tick n from origin: ceil(n x 1,000,000 / 60), the
 *         rounded-up deadline (timesync_DEFENCE.md interface item 3).
 * @param  tick  Tick number, at most 1023 here.
 * @return Microseconds.
 */
static int64_t pb_tick_offset (uint64_t tick)
{
    /* tick is at most 1023, so the product is far below 2^63. */
    int64_t const ticks = (int64_t)tick;

    return ((ticks * PB_US_PER_S) + (PB_FPS - 1)) / PB_FPS;
}

/**
 * @brief  floor((t - origin) x 60 / 1,000,000) mod frame_count, 0 before
 *         origin (W6D-1a), split so the product never overflows.
 * @param  origin_us  Pico time of frame 0.
 * @param  t_pico_us  Pico time.
 * @return Frame index.
 */
static uint32_t pb_frame_on (int64_t origin_us, int64_t t_pico_us)
{
    uint32_t frame = 0u;

    if (t_pico_us > origin_us)
    {
        int64_t const elapsed = t_pico_us - origin_us;
        int64_t const ticks =
            ((elapsed / PB_US_PER_S) * PB_FPS) +
            (((elapsed % PB_US_PER_S) * PB_FPS) / PB_US_PER_S);

        /* ticks >= 0 and g_frames is 1 to 1023: the remainder fits. */
        frame = (uint32_t)(ticks % (int64_t)g_frames);
    }

    return frame;
}

/**
 * @brief  Wrap-aware seq comparison (layout_table.md section 2).
 * @param  seq   Received seq.
 * @param  last  Last accepted seq.
 * @return true when (uint16_t)(seq - last) is 1 to 32,767.
 */
static bool pb_seq_newer (uint16_t seq, uint16_t last)
{
    uint16_t const ahead = (uint16_t)(seq - last); /* 16-bit wrap. */

    return (0u != ahead) && (ahead < UINT16_C(0x8000));
}

#ifndef TIMESYNC_HOST_TEST
/** Wait for a command at most 5 ms per pass (plan choice: ARMED to PLAYING
 *  is seen within 5 ms; the Display Engine starts from the timeline on its
 *  own tick, not from this state change). */
#define PB_WAIT_MS 5u

static void playback_task (void * p_arg);

/**
 * @brief  Create the Playback Control Task.
 * @return true when created.
 */
bool playback_control_start (void)
{
    playback_control_init();

    return (pdPASS == xTaskCreatePinnedToCore(playback_task, TILE_PLAYBACK_NAME,
                                              TILE_PLAYBACK_STACK, NULL,
                                              TILE_PLAYBACK_PRIO, NULL,
                                              TILE_PLAYBACK_CORE));
}

/**
 * @brief  Playback Control Task (T-B4-04 plan name playback_task()): wait
 *         for a command, read the inputs, run the core.
 * @param  p_arg  Unused.
 */
static void playback_task (void * p_arg)
{
    timesync_reader_t reader   = {0};
    int64_t           age_kept = INT64_MAX; /* Last readable beacon age. */

    (void)p_arg; /* The task takes no parameter. */

    for (;;)
    {
        tile_net_command_t        cmd;
        playback_control_inputs_t inputs = {0};
        bool const                b_cmd =
            (pdTRUE == xQueueReceive(tile_net_command_queue(), &cmd,
                                     pdMS_TO_TICKS(PB_WAIT_MS)));

        inputs.b_wifi = tile_net_has_ip();
        inputs.b_asset =
            asset_store_active(&inputs.session_id, &inputs.frame_count);
        inputs.b_sync_ready = timesync_sync_ready();
        inputs.now_pico_us  = timesync_now_corrected(&reader);

        /* A read that never saw a stable set also returns INT64_MAX; keep
         * the last readable age instead, so one unlucky read does not look
         * like lost beacons (R1-13). Real silence still reads as a growing
         * age on the next pass. */
        int64_t const age_us = timesync_newest_age_us();

        if ((INT64_MAX != age_us) || (INT64_MAX == age_kept))
        {
            age_kept = age_us;
        }

        inputs.beacon_age_us = age_kept;

        if (b_cmd)
        {
            playback_control_command_t const command = {
                cmd.type, cmd.session_id, cmd.seq, cmd.frame_index, cmd.t_us};

            if (playback_control_on_command(&command, &inputs))
            {
                printf("PB state=%" PRIu32 " after command type %" PRIu8
                       " seq %" PRIu16 "\n",
                       (uint32_t)g_state, cmd.type, cmd.seq); /* 0 to 8. */
            }
        }

        playback_control_step(&inputs);
    }
}
#endif

/*** end of file ***/
