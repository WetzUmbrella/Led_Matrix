/**
 * @file    playback_control.h
 * @brief   Playback Control (T-B4-04): W6 statechart 4 on the tile. Acts on
 *          START_AT, RESUME_AT, PAUSE and STOP once per (session_id, seq),
 *          keeps the timeline origin, enters CLOCK HOLDOVER when beacons
 *          stop and pauses safely after the 1 s holdover, and publishes the
 *          timeline the Display Engine reads.
 *
 * Owner: Ahsanul (Buddy 4), task T-B4-04. Written 29 Oct 2026 in Ben's (B2)
 * session for Ahsanul to review and defend.
 * Implemented: the 9 states of W6 statechart 4; the pure-C core
 * (playback_control_init(), _step(), _on_command(), _frame_at(), _state())
 * host-tested in panel_sync/test/test_playback.c; the timeline seqlock
 * (playback_control_read_timeline()) for the Display Engine on core 1; the
 * BEACON_LOSS condition through fault_word (RAR-1); the FreeRTOS wrapper
 * playback_task() at priority 15 on core 0 (tile_tasks.h), reading the
 * Command Queue of tile_net.
 * Not yet implemented: the asset input comes from asset_store, whose RAM
 * asset and session (T-B3-04, T-B3-05) do not exist yet, so on a tile the
 * core stays in WAITING FOR READINESS until they land (playback_task()
 * reads asset_store_active()); the Display Engine does not read the
 * timeline yet (T-B5-02, T-B5-03).
 *
 * PROVISIONAL (owner Ahsanul): CLOCK HOLDOVER is entered when the newest
 * beacon is older than 200 ms (two beacon periods, plan choice) and left
 * when a beacon is newer than that; the pause after the holdover uses the
 * one holdover constant TIMESYNC_HOLDOVER_US (1 s, proposed, W6D-2b),
 * which T-B4-05 alone may change. FAULT is entered when the asset reports
 * a frame_count outside 1 to 1023 (inconsistent session data); a new valid
 * session leaves it. START_AT is accepted in READY and in every playing or
 * paused state (a tile that missed a STOP must follow a new start);
 * RESUME_AT also in READY (a rejoining tile, OD-14 option (a)).
 *
 * DEVIATION D-1 (module-wide): C11 stdatomic.h for the timeline seqlock
 * read on core 1 (same pattern as timesync, VC-1).
 * DEVIATION D-2 (module-wide): ESP-IDF and FreeRTOS names in the wrapper.
 */
#ifndef PLAYBACK_CONTROL_H
#define PLAYBACK_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

/** CLOCK HOLDOVER entry: newest beacon older than 200 ms (plan choice). */
#define PLAYBACK_HOLDOVER_ENTER_US INT64_C(200000)

/** States of W6 statechart 4, in its order. */
typedef enum
{
    PLAYBACK_INITIALISING = 0,
    PLAYBACK_WAITING_FOR_READINESS,
    PLAYBACK_SYNCHRONISING,
    PLAYBACK_READY,
    PLAYBACK_ARMED,
    PLAYBACK_PLAYING,
    PLAYBACK_PAUSED,
    PLAYBACK_CLOCK_HOLDOVER,
    PLAYBACK_FAULT,
    PLAYBACK_STATE_COUNT
} playback_control_state_t;

/** What the core needs from the rest of the tile, read once per step. */
typedef struct
{
    bool     b_wifi;        /**< tile_net has an IP address. */
    bool     b_asset;       /**< A validated asset is in RAM. */
    uint8_t  session_id;    /**< Session of that asset. */
    uint16_t frame_count;   /**< Its frame_count, 1 to 1023. */
    bool     b_sync_ready;  /**< timesync_sync_ready() (OD-10 rule). */
    int64_t  now_pico_us;   /**< timesync_now_corrected(). */
    int64_t  beacon_age_us; /**< timesync_newest_age_us(). */
} playback_control_inputs_t;

/** One command from the Command Queue (the fields of tile_net_command_t). */
typedef struct
{
    uint8_t  type;        /**< proto_type_t code. */
    uint8_t  session_id;  /**< Header session_id. */
    uint16_t seq;         /**< Header seq. */
    uint16_t frame_index; /**< RESUME_AT saved frame. */
    uint64_t t_us;        /**< t_start or t_resume, Pico time. */
} playback_control_command_t;

/** The timeline as the Display Engine reads it. */
typedef struct
{
    bool     b_running;   /**< ARMED, PLAYING or CLOCK HOLDOVER: frame from
                               origin once a tick is at or after it. */
    bool     b_held;      /**< PAUSED: show hold_frame. */
    int64_t  origin_us;   /**< Pico time of frame 0. */
    uint16_t frame_count; /**< 1 to 1023. */
    uint16_t hold_frame;  /**< Frame shown while held. */
} playback_control_timeline_t;

/**
 * @brief  Reset to INITIALISING with no accepted command.
 */
void playback_control_init (void);

/**
 * @brief  Automatic transitions (solid arrows of statechart 4) and the
 *         holdover; called by playback_task() every pass.
 * @param  p_in  Inputs read now.
 */
void playback_control_step (playback_control_inputs_t const * p_in);

/**
 * @brief  Act on one command, once per (session_id, seq). Another
 *         session_id or an older seq is rejected; a repeat of the last seq
 *         is ignored.
 * @param  p_cmd  The command.
 * @param  p_in   Inputs read now.
 * @return true when the command caused a state transition.
 */
bool playback_control_on_command (playback_control_command_t const * p_cmd,
                                  playback_control_inputs_t const *  p_in);

/**
 * @brief  Current state.
 * @return The state.
 */
playback_control_state_t playback_control_state (void);

/**
 * @brief  Frame at Pico time t on the current timeline (W6D-1a):
 *         floor((t - origin) x 60 / 1,000,000) mod frame_count in integers;
 *         the held frame while PAUSED; 0 before origin or without a timeline.
 * @param  t_pico_us  Pico time, microseconds.
 * @return Frame index.
 */
uint32_t playback_control_frame_at (int64_t t_pico_us);

/**
 * @brief  Read the published timeline (any task, either core): acquire
 *         load, copy, acquire fence, reload, at most 3 attempts.
 * @param  p_out  Receives the timeline.
 * @return true when a stable set was read.
 */
bool playback_control_read_timeline (playback_control_timeline_t * p_out);

/**
 * @brief  Commands rejected (another session or an older seq) since init.
 * @return The count.
 */
uint32_t playback_control_rejected (void);

/**
 * @brief  Create the Playback Control Task (priority 15, core 0). Call
 *         after tile_net_start().
 * @return true when the task was created.
 */
bool playback_control_start (void);

#endif /* PLAYBACK_CONTROL_H */

/*** end of file ***/
