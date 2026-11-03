/**
 * @file    test_playback.c
 * @brief   Host tests of playback_control (T-B4-04): the five host tests of
 *          the plan row, and the holdover rule run on a fake clock (the row
 *          tests it on 2 tiles; this host form checks the logic first).
 *
 * Owner: Ahsanul (Buddy 4). Written 29 Oct 2026 in Ben's (B2) session.
 */
#include "test_playback.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "fault_word.h"
#include "playback_control.h"
#include "proto.h"
#include "test_main.h"
#include "timesync.h"

/** Session, frame_count and a start time for the tests (plan choice). */
#define TP_SESSION 5u
#define TP_FRAMES  600u
#define TP_START   INT64_C(10000000)

static playback_control_inputs_t tp_inputs (int64_t now_us, int64_t age_us);
static bool tp_command (proto_type_t type, uint8_t session, uint16_t seq,
                        uint64_t t_us, uint16_t frame, int64_t now_us);
static bool tp_to_playing (void);

/**
 * @brief  T-B4-04 host test 1: three identical START_AT give 1 transition.
 * @return true when every check passed.
 */
bool test_playback_start_once (void)
{
    uint32_t moves = 0u;

    playback_control_init();

    playback_control_inputs_t const inputs = tp_inputs(TP_START - 600000, 0);

    playback_control_step(&inputs); /* INITIALISING to WAITING. */
    playback_control_step(&inputs); /* WAITING to SYNCHRONISING. */
    playback_control_step(&inputs); /* SYNCHRONISING to READY. */

    for (uint32_t copy = 0u; copy < 3u; copy++)
    {
        if (tp_command(PROTO_TYPE_START_AT, TP_SESSION, 7u, TP_START, 0u,
                       TP_START - 600000))
        {
            moves++;
        }
    }

    printf("    3 copies, %" PRIu32 " transition\n", moves);

    return test_main_check((1u == moves) &&
                               (PLAYBACK_ARMED == playback_control_state()),
                           __LINE__);
}

/**
 * @brief  T-B4-04 host test 2: a wrong session_id gives 0 transitions.
 * @return true when every check passed.
 */
bool test_playback_wrong_session (void)
{
    uint32_t moves = 0u;

    playback_control_init();

    playback_control_inputs_t const inputs = tp_inputs(TP_START - 600000, 0);

    playback_control_step(&inputs);
    playback_control_step(&inputs);
    playback_control_step(&inputs);

    for (uint32_t copy = 0u; copy < 3u; copy++)
    {
        if (tp_command(PROTO_TYPE_START_AT, TP_SESSION + 1u, 7u, TP_START, 0u,
                       TP_START - 600000))
        {
            moves++;
        }
    }

    printf("    wrong session: %" PRIu32 " transitions, %" PRIu32 " rejected\n",
           moves, playback_control_rejected());

    return test_main_check((0u == moves) &&
                               (PLAYBACK_READY == playback_control_state()) &&
                               (3u == playback_control_rejected()),
                           __LINE__);
}

/**
 * @brief  T-B4-04 host test 3: the frame at t = origin + k x 1,000,000 / 60
 *         (t from timesync_deadline_pico(), interface item 3) equals
 *         k mod 600 for k = 0 to 1199.
 * @return true when every check passed.
 */
bool test_playback_frame_index (void)
{
    uint32_t wrong = 0u;
    bool     b_ok  = tp_to_playing();

    for (uint64_t tick = 0u; tick < 1200u; tick++)
    {
        int64_t const  t_us  = timesync_deadline_pico(TP_START, tick);
        uint32_t const frame = playback_control_frame_at(t_us);

        if ((tick % TP_FRAMES) != frame)
        {
            wrong++;
        }
    }

    printf("    1200 ticks, %" PRIu32 " wrong frames\n", wrong);
    b_ok = (test_main_check(0u == wrong, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  T-B4-04 host test 4: STOP is accepted from PAUSED (W6D-2e).
 * @return true when every check passed.
 */
bool test_playback_stop_from_paused (void)
{
    bool b_ok = tp_to_playing();

    /* The result is checked through the state below. */
    (void)tp_command(PROTO_TYPE_PAUSE, TP_SESSION, 8u, 0u, 0u,
                     TP_START + 2000000); /* Checked by the state below. */
    b_ok = (test_main_check(PLAYBACK_PAUSED == playback_control_state(),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(tp_command(PROTO_TYPE_STOP, TP_SESSION, 9u, 0u, 0u,
                                       TP_START + 3000000),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(PLAYBACK_READY == playback_control_state(),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  T-B4-04 host test 5: RESUME_AT shows the saved index at t_resume
 *         (and the next frame one tick later).
 * @return true when every check passed.
 */
bool test_playback_resume_frame (void)
{
    bool          b_ok     = tp_to_playing();
    int64_t const pause_us = TP_START + 2500000; /* 150 ticks inputs. */

    /* The result is checked through the state below. */
    (void)tp_command(PROTO_TYPE_PAUSE, TP_SESSION, 8u, 0u, 0u,
                     pause_us); /* Checked below. */

    uint32_t const saved = playback_control_frame_at(pause_us + 999999);
    int64_t const  t_res = pause_us + 5000000;
    /* saved is below 600, so it fits uint16_t. */
    uint16_t const frame16 = (uint16_t)saved;

    /* t_res is positive, so the cast keeps the value. */
    (void)tp_command(PROTO_TYPE_RESUME_AT, TP_SESSION, 9u, (uint64_t)t_res,
                     frame16, pause_us + 4400000); /* Checked below. */

    playback_control_inputs_t const inputs = tp_inputs(t_res, 0);

    playback_control_step(&inputs); /* ARMED to PLAYING at t_resume. */

    uint32_t const at_resume = playback_control_frame_at(t_res);
    uint32_t const next      = playback_control_frame_at(
        t_res + timesync_deadline_pico(0, 1u)); /* One tick later. */

    printf("    saved %" PRIu32 ", shown at t_resume %" PRIu32 ", then %" PRIu32
           "\n",
           saved, at_resume, next);
    b_ok = (test_main_check((150u == saved) && (saved == at_resume) &&
                                (((saved + 1u) % TP_FRAMES) == next) &&
                                (PLAYBACK_PLAYING == playback_control_state()),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  T-B4-04 holdover: beacons absent 0.9 s keep the tile playing
 *         (PLAYING or CLOCK HOLDOVER) and 1.1 s pause it, 10 of 10 trials
 *         each at different phases (plan choice), with BEACON_LOSS set on the
 *         pause and cleared when beacons return.
 * @return true when every check passed.
 */
bool test_playback_holdover (void)
{
    uint32_t keeps  = 0u;
    uint32_t pauses = 0u;

    for (uint32_t trial = 0u; trial < 10u; trial++)
    {
        for (uint32_t gap = 0u; gap < 2u; gap++)
        {
            int64_t const limit =
                (0u == gap) ? INT64_C(900000) : INT64_C(1100000);
            bool const    b_ok = tp_to_playing();
            int64_t const base = TP_START + 1000000 + (trial * 37000);

            for (int64_t age = 0; age <= limit; age += 10000)
            {
                playback_control_inputs_t const inputs =
                    tp_inputs(base + age, age);

                playback_control_step(&inputs);
            }

            playback_control_state_t const state = playback_control_state();

            if ((b_ok) && (0u == gap) &&
                ((PLAYBACK_PLAYING == state) ||
                 (PLAYBACK_CLOCK_HOLDOVER == state)))
            {
                keeps++;
            }

            if ((b_ok) && (1u == gap) && (PLAYBACK_PAUSED == state) &&
                (0u != (fault_word_snapshot() & FAULT_BIT_BEACON_LOSS)))
            {
                pauses++;
            }

            /* Beacons return: the condition bit clears. */
            playback_control_inputs_t const back =
                tp_inputs(base + limit + 100000, 0);

            playback_control_step(&back);
        }
    }

    printf("    0.9 s: %" PRIu32 " of 10 keep playing; 1.1 s: %" PRIu32
           " of 10 pause\n",
           keeps, pauses);

    return test_main_check(
        (10u == keeps) && (10u == pauses) &&
            (0u == (fault_word_snapshot() & FAULT_BIT_BEACON_LOSS)),
        __LINE__);
}

/**
 * @brief  Inputs: WiFi, the test asset, sync ready, the given time and age.
 * @param  now_us  Corrected Pico time.
 * @param  age_us  Age of the newest beacon.
 * @return The inputs.
 */
static playback_control_inputs_t tp_inputs (int64_t now_us, int64_t age_us)
{
    playback_control_inputs_t const inputs = {
        .b_wifi        = true,
        .b_asset       = true,
        .session_id    = TP_SESSION,
        .frame_count   = TP_FRAMES,
        .b_sync_ready  = (age_us <= TIMESYNC_HOLDOVER_US),
        .now_pico_us   = now_us,
        .beacon_age_us = age_us,
    };

    return inputs;
}

/**
 * @brief  Feed one command.
 * @param  type     Message type.
 * @param  session  session_id.
 * @param  seq      seq.
 * @param  t_us     t_start or t_resume.
 * @param  frame    RESUME_AT frame.
 * @param  now_us   Corrected Pico time.
 * @return true when it caused a transition.
 */
static bool tp_command (proto_type_t type, uint8_t session, uint16_t seq,
                        uint64_t t_us, uint16_t frame, int64_t now_us)
{
    playback_control_command_t const cmd = {
        .type        = (uint8_t)type, /* Type codes are below 18. */
        .session_id  = session,
        .seq         = seq,
        .frame_index = frame,
        .t_us        = t_us,
    };
    playback_control_inputs_t const inputs = tp_inputs(now_us, 0);

    return playback_control_on_command(&cmd, &inputs);
}

/**
 * @brief  Init, reach READY, START_AT at TP_START, step past it.
 * @return true when PLAYING.
 */
static bool tp_to_playing (void)
{
    playback_control_init();

    playback_control_inputs_t const before = tp_inputs(TP_START - 600000, 0);

    playback_control_step(&before);
    playback_control_step(&before);
    playback_control_step(&before);
    /* The result is checked through the state below. */
    (void)tp_command(PROTO_TYPE_START_AT, TP_SESSION, 7u, TP_START, 0u,
                     TP_START - 600000); /* Checked by the state below. */

    playback_control_inputs_t const after = tp_inputs(TP_START, 0);

    playback_control_step(&after);

    return (PLAYBACK_PLAYING == playback_control_state());
}

/*** end of file ***/
