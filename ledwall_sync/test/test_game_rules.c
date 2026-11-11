/**
 * @file    test_game_rules.c
 * @brief   Host tests of game_rules (C17 timing, C18 scoring and dedupe) on a
 *          fake clock with an injected RNG seed.
 *
 * Owner: Benjamin (Buddy 2). Written for the whack-a-mole re-plan, build
 * step 7.
 */
#include "test_game_rules.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

#include "game_rules.h"
#include "test_main.h"

/** Round start, 1 s after boot (plan choice), and one millisecond. */
#define TST_T0 UINT64_C(1000000)
#define TST_MS UINT64_C(1000)

/** RNG seed, and the active tiles 1 to 5 (bits 0 to 4). */
#define TST_SEED        UINT32_C(0xC0FFEE)
#define TST_ACTIVE_FIVE UINT16_C(0x001F)

/** Events kept by a run, and the length of a round in milliseconds. */
#define TST_LOG_MAX 128u
#define TST_ROUND_MS 30000u

/** An event with the time it was reported. */
typedef struct
{
    game_event_t evt;
    uint64_t     t_us;
} tst_logged_t;

/** Events of a run, in order. */
typedef struct
{
    tst_logged_t items[TST_LOG_MAX];
    uint32_t     count;
} tst_log_t;

/**
 * @brief  Next event at or after *p_now, polling one millisecond at a time.
 *         Call again at the same time for the next event of that millisecond.
 * @param  p_game  Engine.
 * @param  p_now   Time; advanced to the time of the event.
 * @param  limit   Stop at this time.
 * @param  p_out   Receives the event.
 * @return true when an event was found by limit.
 */
static bool tst_next_event (game_t * p_game, uint64_t * p_now, uint64_t limit,
                            game_event_t * p_out)
{
    bool b_found = false;

    while ((!b_found) && (*p_now <= limit))
    {
        if (game_rules_poll(p_game, *p_now, p_out))
        {
            b_found = true;
        }
        else
        {
            *p_now += TST_MS;
        }
    }

    return b_found;
}

/**
 * @brief  Next event of a kind, skipping others (for scenarios that do not
 *         care about the rest).
 * @param  p_game  Engine.
 * @param  p_now   Time, advanced to the event.
 * @param  kind    Kind wanted.
 * @param  p_out   Receives the event.
 * @return true when found within 10 s.
 */
static bool tst_next_kind (game_t * p_game, uint64_t * p_now,
                           game_event_kind_t kind, game_event_t * p_out)
{
    bool b_found = false;

    while ((!b_found) &&
           (tst_next_event(p_game, p_now, *p_now + (10000u * TST_MS), p_out)))
    {
        b_found = (kind == p_out->kind);
    }

    return b_found;
}

/**
 * @brief  Run a round in 1 ms steps, recording every event, and check that
 *         game_rules_next_due() is never at or before a quiet poll.
 * @param  p_game  Engine.
 * @param  from    First time.
 * @param  to      Last time.
 * @param  p_log   Receives the events.
 * @return true when game_rules_next_due() was never at or before a quiet poll.
 */
static bool tst_run (game_t * p_game, uint64_t from, uint64_t last,
                     tst_log_t * p_log)
{
    bool         b_due_ok = true;
    game_event_t evt;

    for (uint64_t now = from; now <= last; now += TST_MS)
    {
        while (game_rules_poll(p_game, now, &evt))
        {
            if (p_log->count < TST_LOG_MAX)
            {
                p_log->items[p_log->count].evt  = evt;
                p_log->items[p_log->count].t_us = now;
                p_log->count++;
            }
        }

        if ((p_game->b_running) && (game_rules_next_due(p_game) <= now))
        {
            b_due_ok = false;
        }
    }

    return b_due_ok;
}

static bool tst_scenario_dedupe (game_t * p_game, uint64_t * p_now);

/**
 * @brief  Check one mole against the timing rules: its up time from its
 *         show time, its tile, that it is not the tile before, and that it
 *         hides before the round ends.
 * @param  p_evt     The MOLE_ON.
 * @param  last_node Tile of the previous mole, 0 before the first.
 * @return true when every check passed.
 */
static bool tst_check_mole (game_event_t const * p_evt, uint8_t last_node)
{
    bool b_ok = true;

    b_ok = (test_main_check(game_rules_up_ms(p_evt->t_show_us - TST_T0) ==
                                p_evt->up_ms, __LINE__)) && b_ok;
    b_ok = (test_main_check((p_evt->node_id >= 1u) &&
                                (p_evt->node_id <= 5u), __LINE__)) && b_ok;
    b_ok = (test_main_check(p_evt->node_id != last_node, __LINE__)) && b_ok;
    b_ok = (test_main_check(p_evt->t_show_us + (p_evt->up_ms * TST_MS) <=
                                TST_T0 + (TST_ROUND_MS * TST_MS),
                            __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  C17: a 30 s round with no taps: mole time 1193 ms at the first
 *         mole, 1200 ms at the start and 500 ms at the end, a 300 ms gap
 *         after each timeout, never the same tile twice in a row, only active
 *         tiles, the round ends at 30 s, and game_rules_next_due() never lies.
 * @return true when every check passed.
 */
bool test_game_rules_timing (void)
{
    static game_t    game;
    static tst_log_t log;
    bool     b_ok      = true;
    uint32_t moles     = 0u;
    uint8_t  last_node = 0u;

    game_rules_init(&game, TST_SEED);
    game_rules_start(&game, TST_T0, TST_ACTIVE_FIVE);
    b_ok = (test_main_check(game_rules_next_due(&game) <=
                                (TST_T0 + (100u * TST_MS)), __LINE__)) && b_ok;
    b_ok = (test_main_check(tst_run(&game, TST_T0,
                                    TST_T0 + (TST_ROUND_MS + 1000u) * TST_MS,
                                    &log), __LINE__)) && b_ok;

    /* The rule times. */
    b_ok = (test_main_check(1200u == game_rules_up_ms(0u), __LINE__)) && b_ok;
    b_ok = (test_main_check(850u == game_rules_up_ms(15000u * TST_MS),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(500u == game_rules_up_ms(30000u * TST_MS),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(500u == game_rules_up_ms(45000u * TST_MS),
                            __LINE__)) && b_ok;

    /* The first mole shows 300 ms into the round, up 1193 ms (elapsed 300
     * ms: 1200 - 700 x 300 / 30000 = 1193). */
    b_ok = (test_main_check(log.count > 0u, __LINE__)) && b_ok;
    if (log.count > 0u)
    {
        game_event_t const * const p_first = &log.items[0].evt;

        b_ok = (test_main_check(GAME_EV_MOLE_ON == p_first->kind, __LINE__)) &&
               b_ok;
        b_ok = (test_main_check((TST_T0 + (300u * TST_MS)) ==
                                    p_first->t_show_us, __LINE__)) && b_ok;
        b_ok = (test_main_check(1193u == p_first->up_ms, __LINE__)) && b_ok;
    }

    /* Every mole: its own timing, and the next show 300 ms after the hide. */
    for (uint32_t index = 0u; index < log.count; index++)
    {
        game_event_t const * const p_evt = &log.items[index].evt;

        if (GAME_EV_MOLE_ON == p_evt->kind)
        {
            moles++;
            b_ok = (tst_check_mole(p_evt, last_node)) && b_ok;
            last_node = p_evt->node_id;

            for (uint32_t next = index + 1u; next < log.count; next++)
            {
                if (GAME_EV_MOLE_ON == log.items[next].evt.kind)
                {
                    uint64_t const hide =
                        p_evt->t_show_us + (p_evt->up_ms * TST_MS);

                    b_ok = (test_main_check(log.items[next].evt.t_show_us ==
                                                (hide + (300u * TST_MS)),
                                            __LINE__)) && b_ok;
                    break;
                }
            }
        }
        else if (GAME_EV_FEEDBACK == p_evt->kind)
        {
            b_ok = (test_main_check(false, __LINE__)) && b_ok; /* No taps. */
        }
        else
        {
            /* ROUND_END is checked below. */
        }
    }

    b_ok = (test_main_check(moles >= 20u, __LINE__)) && b_ok;
    printf("    %" PRIu32 " moles in a 30 s round with no taps\n", moles);

    /* The round ends at 30 s with score 0 and nothing still running. */
    for (uint32_t index = 0u; index < log.count; index++)
    {
        if (GAME_EV_ROUND_END == log.items[index].evt.kind)
        {
            b_ok = (test_main_check((TST_T0 + (TST_ROUND_MS * TST_MS)) ==
                                        log.items[index].t_us, __LINE__)) &&
                   b_ok;
            b_ok = (test_main_check(0 == log.items[index].evt.score,
                                    __LINE__)) && b_ok;
        }
    }

    b_ok = (test_main_check(!game.b_running, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Score a hit: the first mole, tapped 10 ms after it shows. The mole
 *         ends at once, the gap runs from the tap, and the next mole shows
 *         300 ms after it.
 * @param  p_game  Engine, started.
 * @param  p_now   Time, advanced.
 * @param  p_show  Receives the first mole's show time.
 * @param  p_mole  Receives its tile.
 * @return true when every check passed.
 */
static bool tst_scenario_hit (game_t * p_game, uint64_t * p_now,
                              uint64_t * p_show, uint8_t * p_mole)
{
    game_event_t evt;
    bool         b_ok = true;

    b_ok = (test_main_check(tst_next_kind(p_game, p_now, GAME_EV_MOLE_ON, &evt),
                            __LINE__)) && b_ok;
    *p_show = evt.t_show_us;
    *p_mole = evt.node_id;
    game_rules_tap(p_game, *p_mole, 500u, *p_show + (10u * TST_MS));
    b_ok = (test_main_check(tst_next_kind(p_game, p_now, GAME_EV_FEEDBACK,
                                          &evt), __LINE__)) && b_ok;
    b_ok = (test_main_check((*p_mole == evt.node_id) &&
                                (PROTO_FEEDBACK_HIT == evt.result) &&
                                (1 == evt.score), __LINE__)) && b_ok;
    /* The verdict comes when the 10 ms tap's dedupe window closes. */
    uint64_t const verdict = *p_show + ((10u + GAME_DEDUPE_MS) * TST_MS);

    b_ok = (test_main_check(verdict == *p_now, __LINE__)) && b_ok;
    b_ok = (test_main_check(tst_next_kind(p_game, p_now, GAME_EV_MOLE_ON,
                                          &evt), __LINE__)) && b_ok;
    b_ok = (test_main_check((*p_show + (310u * TST_MS)) == evt.t_show_us,
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(evt.node_id != *p_mole, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  C18: hits (+1, the mole ends at once), wrong tiles and taps with no
 *         mole up (-1), dedupe within GAME_DEDUPE_MS (highest peak counts,
 *         later taps count separately), and a stopped round ignores taps.
 * @return true when every check passed.
 */
bool test_game_rules_scoring (void)
{
    static game_t game;
    game_event_t evt;
    uint64_t     now   = TST_T0;
    uint64_t     show  = 0u;
    uint8_t      mole  = 0u;
    uint8_t      other = 0u;
    bool         b_ok  = true;

    game_rules_init(&game, TST_SEED);
    game_rules_start(&game, TST_T0, UINT16_C(0x0003)); /* Tiles 1 and 2. */

    b_ok = (tst_scenario_hit(&game, &now, &show, &mole)) && b_ok;
    b_ok = (test_main_check(1 == game_rules_score(&game), __LINE__)) && b_ok;

    /* Before the show time, a tap on the coming mole's tile is a miss. */
    b_ok = (test_main_check(tst_next_kind(&game, &now, GAME_EV_MOLE_ON, &evt),
                            __LINE__)) && b_ok;
    show = evt.t_show_us;
    mole = evt.node_id;
    other = (1u == mole) ? 2u : 1u;
    game_rules_tap(&game, mole, 300u, show - (50u * TST_MS));
    b_ok = (test_main_check(tst_next_kind(&game, &now, GAME_EV_FEEDBACK, &evt),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check((PROTO_FEEDBACK_MISS == evt.result) &&
                                (0 == evt.score), __LINE__)) && b_ok;

    /* A tap on the other tile while the mole is up is a miss: -1. */
    game_rules_tap(&game, other, 300u, show + (100u * TST_MS));
    b_ok = (test_main_check(tst_next_kind(&game, &now, GAME_EV_FEEDBACK, &evt),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check((other == evt.node_id) && (-1 == evt.score),
                            __LINE__)) && b_ok;

    /* The mole is still up: a tap on it now is a hit, back to 0. */
    game_rules_tap(&game, mole, 300u, show + (200u * TST_MS));
    b_ok = (test_main_check(tst_next_kind(&game, &now, GAME_EV_FEEDBACK, &evt),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check((PROTO_FEEDBACK_HIT == evt.result) &&
                                (0 == evt.score), __LINE__)) && b_ok;

    return (b_ok) && (tst_scenario_dedupe(&game, &now));
}

/**
 * @brief  Dedupe: the mole's tile (peak 100) and the other tile (peak 900)
 *         tap 20 ms apart. Only the higher peak counts, once: one miss, -1.
 *         A late duplicate inside the closed window is dropped and counted.
 *         A tap 70 ms after the first is outside its window and counts on
 *         its own. Net: -1, then +1 and -1 on the mole, score -1.
 * @param  p_game  Engine, in a round with a mole up or planned.
 * @param  p_now   Time, advanced.
 * @return true when every check passed.
 */
static bool tst_scenario_dedupe (game_t * p_game, uint64_t * p_now)
{
    game_event_t evt;
    uint64_t     show  = 0u;
    uint8_t      mole  = 0u;
    uint8_t      other = 0u;
    bool         b_ok  = true;

    b_ok = (test_main_check(tst_next_kind(p_game, p_now, GAME_EV_MOLE_ON,
                                          &evt), __LINE__)) && b_ok;
    show  = evt.t_show_us;
    mole  = evt.node_id;
    other = (1u == mole) ? 2u : 1u;
    game_rules_tap(p_game, mole, 100u, show + (10u * TST_MS));
    /* Inside the first tap's window (from 10 ms, GAME_DEDUPE_MS long). */
    game_rules_tap(p_game, other, 900u,
                   show + ((10u + (GAME_DEDUPE_MS / 2u)) * TST_MS));
    b_ok = (test_main_check(tst_next_kind(p_game, p_now, GAME_EV_FEEDBACK,
                                          &evt), __LINE__)) && b_ok;
    b_ok = (test_main_check((other == evt.node_id) &&
                                (-1 == evt.score), __LINE__)) && b_ok;

    /* The dropped tap made no second feedback. */
    b_ok = (test_main_check((!tst_next_event(p_game, p_now,
                                            *p_now + (40u * TST_MS), &evt)) ||
                                (GAME_EV_FEEDBACK != evt.kind), __LINE__)) &&
           b_ok;

    /* Late duplicate: arrives after the window closed, stamped inside it. */
    game_rules_tap(p_game, other, 900u,
                   show + ((10u + GAME_DEDUPE_MS - 1u) * TST_MS));
    b_ok = (test_main_check(1u == p_game->dedupe_drops, __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == p_game->tap_count, __LINE__)) && b_ok;

    /* Outside the window: the same tile counts as a hit, then a miss. */
    game_rules_tap(p_game, mole, 100u, show + (200u * TST_MS));
    game_rules_tap(p_game, mole, 100u, show + (270u * TST_MS));
    b_ok = (test_main_check(tst_next_kind(p_game, p_now, GAME_EV_FEEDBACK,
                                          &evt), __LINE__)) && b_ok;
    b_ok = (test_main_check(PROTO_FEEDBACK_HIT == evt.result, __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(tst_next_kind(p_game, p_now, GAME_EV_FEEDBACK,
                                          &evt), __LINE__)) && b_ok;
    b_ok = (test_main_check((PROTO_FEEDBACK_MISS == evt.result) &&
                                (-1 == evt.score), __LINE__)) && b_ok;
    b_ok = (test_main_check(-1 == game_rules_score(p_game), __LINE__)) && b_ok;

    /* A stopped round ignores taps and reports nothing. */
    game_rules_stop(p_game);
    game_rules_tap(p_game, 1u, 900u, *p_now);
    b_ok = (test_main_check(!game_rules_poll(p_game, *p_now + (500u * TST_MS),
                                             &evt), __LINE__)) && b_ok;
    b_ok = (test_main_check(0u == p_game->tap_count, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Start a round on tiles 1 and 2 and read its first mole.
 * @param  p_game  Engine; set up here.
 * @param  p_now   Time, advanced to the MOLE_ON.
 * @param  p_evt   Receives the MOLE_ON.
 * @return true when a MOLE_ON was found.
 */
static bool tst_first_mole (game_t * p_game, uint64_t * p_now,
                            game_event_t * p_evt)
{
    game_rules_init(p_game, TST_SEED);
    game_rules_start(p_game, TST_T0, UINT16_C(0x0003));
    *p_now = TST_T0;

    return tst_next_kind(p_game, p_now, GAME_EV_MOLE_ON, p_evt);
}

/**
 * @brief  Tap grace (C18b): a tap stamped 10 ms before the hide and delivered
 *         at hide + 60 ms still hits; a tap stamped 5 ms after the hide and
 *         delivered at hide + 60 ms misses; a timeout with no tap keeps the
 *         score and the next mole shows 300 ms after the hide.
 * @return true when every check passed.
 */
bool test_game_rules_grace (void)
{
    static game_t game;
    game_event_t evt;
    uint64_t     now  = TST_T0;
    uint64_t     hide = 0u;
    uint64_t     show = 0u;
    uint8_t      mole = 0u;
    bool         b_ok = true;

    /* (a) Tap stamped hide - 10 ms, delivered at hide + 60 ms: hit, +1. */
    b_ok = (test_main_check(tst_first_mole(&game, &now, &evt), __LINE__)) &&
           b_ok;
    show = evt.t_show_us;
    mole = evt.node_id;
    hide = show + (evt.up_ms * TST_MS);
    now  = hide + (60u * TST_MS);
    b_ok = (test_main_check(game.b_mole_up, __LINE__)) && b_ok;
    game_rules_tap(&game, mole, 500u, hide - (10u * TST_MS));
    b_ok = (test_main_check(tst_next_kind(&game, &now, GAME_EV_FEEDBACK,
                                          &evt), __LINE__)) && b_ok;
    b_ok = (test_main_check((PROTO_FEEDBACK_HIT == evt.result) &&
                                (mole == evt.node_id) && (1 == evt.score),
                            __LINE__)) && b_ok;

    /* (b) Tap stamped hide + 5 ms, delivered at hide + 60 ms: miss, -1. */
    b_ok = (test_main_check(tst_first_mole(&game, &now, &evt), __LINE__)) &&
           b_ok;
    show = evt.t_show_us;
    mole = evt.node_id;
    hide = show + (evt.up_ms * TST_MS);
    now  = hide + (60u * TST_MS);
    game_rules_tap(&game, mole, 500u, hide + (5u * TST_MS));
    b_ok = (test_main_check(tst_next_kind(&game, &now, GAME_EV_FEEDBACK,
                                          &evt), __LINE__)) && b_ok;
    b_ok = (test_main_check((PROTO_FEEDBACK_MISS == evt.result) &&
                                (-1 == evt.score), __LINE__)) && b_ok;

    /* (c) No tap: the mole stays up through the grace, retires at hide +
     * 100 ms with no score, and the next mole shows at hide + 300 ms. */
    b_ok = (test_main_check(tst_first_mole(&game, &now, &evt), __LINE__)) &&
           b_ok;
    show = evt.t_show_us;
    hide = show + (evt.up_ms * TST_MS);
    b_ok = (test_main_check(game_rules_next_due(&game) == (hide + (100u *
                                TST_MS)), __LINE__)) && b_ok;
    now = hide + (99u * TST_MS);
    b_ok = (test_main_check(!game_rules_poll(&game, now, &evt), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(game.b_mole_up, __LINE__)) && b_ok;
    now = hide + (100u * TST_MS);
    b_ok = (test_main_check(game_rules_poll(&game, now, &evt), __LINE__)) &&
           b_ok;
    b_ok = (test_main_check((GAME_EV_MOLE_ON == evt.kind) &&
                                (hide + (300u * TST_MS) == evt.t_show_us),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(0 == game_rules_score(&game), __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  C18c (bench 11 Nov 2026, round 11): a tap and its echo stamped
 *         3 ms apart but delivered late by WiFi (the echo 61 ms after its
 *         stamp, the real tap 10 ms after that) are one group: one HIT, no
 *         MISS for the echo. A late tap stamped just before a closed group
 *         is a duplicate too.
 * @return true when every check passed.
 */
bool test_game_rules_late_echo (void)
{
    static game_t game;
    game_event_t  evt;
    uint64_t      now  = TST_T0;
    bool          b_ok = true;

    b_ok = (test_main_check(tst_first_mole(&game, &now, &evt), __LINE__)) &&
           b_ok;

    uint64_t const show   = evt.t_show_us;
    uint8_t const  mole   = evt.node_id;
    uint8_t const  other  = (1u == mole) ? 2u : 1u;
    uint64_t const t_real = show + (40u * TST_MS);
    uint64_t const t_echo = t_real + (3u * TST_MS);

    /* The echo arrives first: no verdict until the group has collected. */
    now = t_echo + (61u * TST_MS);
    game_rules_tap_rx(&game, other, 300u, t_echo, now);
    b_ok = (test_main_check((!game_rules_poll(&game, now, &evt)) ||
                                (GAME_EV_FEEDBACK != evt.kind),
                            __LINE__)) && b_ok;

    now += 10u * TST_MS;
    game_rules_tap_rx(&game, mole, 9000u, t_real, now);
    b_ok = (test_main_check(tst_next_kind(&game, &now, GAME_EV_FEEDBACK,
                                          &evt), __LINE__)) && b_ok;
    b_ok = (test_main_check((PROTO_FEEDBACK_HIT == evt.result) &&
                                (mole == evt.node_id) && (1 == evt.score),
                            __LINE__)) && b_ok;

    /* A straggler stamped 5 ms before the group's first tap: a duplicate. */
    game_rules_tap_rx(&game, other, 300u, t_real - (5u * TST_MS), now);
    b_ok = (test_main_check(1u == game.dedupe_drops, __LINE__)) && b_ok;
    b_ok = (test_main_check((!tst_next_event(&game, &now,
                                             now + (200u * TST_MS), &evt)) ||
                                (GAME_EV_FEEDBACK != evt.kind),
                            __LINE__)) && b_ok;
    b_ok = (test_main_check(1 == game_rules_score(&game), __LINE__)) && b_ok;

    return b_ok;
}

/*** end of file ***/
