/**
 * @file    game_rules.c
 * @brief   Whack-a-mole rules v1 (see game_rules.h for the rules).
 *
 * Owner: Benjamin (Buddy 2), game engine. Written for build step 7.
 *
 * Each call of game_rules_poll() reports at most one event, after the state
 * changes that come before it: a dedupe window that has closed is resolved
 * first, then a mole may expire, the next mole may be planned, and the round
 * may end. A window is resolved before a mole expires, so a tap made before
 * the hide time still counts when the mole times out.
 */
#include "game_rules.h"

#include <string.h>

/** Mole times per level, milliseconds: first and last (B4). */
static uint16_t const g_up_first_ms[GAME_LEVELS] = {2000u, 1500u, 1200u};
static uint16_t const g_up_last_ms[GAME_LEVELS]  = {1200u, 800u, 500u};

/** Percent. */
#define GAME_PERCENT 100u

/** Microseconds per second. */
#define GAME_US_PER_S UINT64_C(1000000)

/** Microseconds per millisecond, and the rule times in microseconds. */
#define GAME_US_PER_MS  UINT64_C(1000)
#define GAME_ROUND_US   (GAME_US_PER_MS * GAME_ROUND_MS)
#define GAME_GAP_US     (GAME_US_PER_MS * GAME_GAP_MS)
#define GAME_LEAD_US    (GAME_US_PER_MS * GAME_LEAD_MS)
#define GAME_DEDUPE_US  (GAME_US_PER_MS * GAME_DEDUPE_MS)
#define GAME_COLLECT_US (GAME_US_PER_MS * GAME_COLLECT_MS)
#define GAME_GRACE_US   (GAME_US_PER_MS * GAME_TAP_GRACE_MS)

/* The grace ends where the next mole is planned, so the next show stays at
 * hide + GAME_GAP_MS (see game_rules.h). */
_Static_assert(GAME_TAP_GRACE_MS <= (GAME_GAP_MS - GAME_LEAD_MS),
               "tap grace must end by the planning point of the next mole");

/** Score limits (layout_table.md, STATUS score: 8 bits, -99 to 99). */
#define GAME_SCORE_LIMIT ((int32_t)PROTO_SCORE_MAX)

/** Marks "no mole is ever planned again this round". */
#define GAME_NO_MOLE_US UINT64_MAX

/** Index of no tap. */
#define GAME_NO_TAP UINT32_MAX

static uint32_t game_next_rand (game_t * p_game);
static uint16_t game_bit (uint8_t node_id);
static uint8_t  game_pick_node (game_t * p_game);
static uint64_t game_earliest_tap (game_t const * p_game);
static uint64_t game_group_due (game_t const * p_game);
static bool     game_window_due (game_t const * p_game, uint64_t now_us);
static bool     game_resolve_window (game_t * p_game, game_event_t * p_event);
static bool     game_tap_better (game_tap_t const * p_a,
                                 game_tap_t const * p_b);
static bool     game_mole_expired (game_t const * p_game, uint64_t now_us);
static bool     game_plan_due (game_t const * p_game, uint64_t now_us);
static bool     game_plan_mole (game_t * p_game, uint64_t now_us,
                                game_event_t * p_event);
static bool     game_round_over (game_t const * p_game, uint64_t now_us);
static int32_t  game_clamp (int32_t score);

/**
 * @brief  Reset the engine: no round, score 0, the RNG seeded.
 * @param  p_game  Engine; must not be NULL.
 * @param  seed    RNG seed; 0 is replaced by 1.
 */
void game_rules_init (game_t * p_game, uint32_t seed)
{
    (void)memset(p_game, 0, sizeof(*p_game)); /* Returns the engine. */
    p_game->rng = (0u != seed) ? seed : 1u;
    p_game->next_show_us = GAME_NO_MOLE_US;
}

/**
 * @brief  Start a round at now_us with the tiles in active_mask.
 * @param  p_game       Engine; must not be NULL.
 * @param  now_us       Round start, microseconds.
 * @param  active_mask  Bit n - 1 for each Node ID in the wall.
 * @param  level        proto_level_t; out of range is taken as Hard.
 */
void game_rules_start (game_t * p_game, uint64_t now_us, uint16_t active_mask,
                       uint8_t level)
{
    p_game->level          = (level < GAME_LEVELS) ? level : (GAME_LEVELS - 1u);
    p_game->hits           = 0u;
    p_game->missed         = 0u;
    p_game->wrong          = 0u;
    p_game->shown          = 0u;
    p_game->reaction_us    = 0u;
    p_game->b_running      = true;
    p_game->round_start_us = now_us;
    p_game->round_end_us   = now_us + GAME_ROUND_US;
    p_game->score          = 0;
    p_game->active         = active_mask;
    p_game->last_node      = 0u;
    p_game->b_mole_up      = false;
    p_game->next_show_us   = now_us + GAME_GAP_US; /* First mole after a gap. */
    p_game->tap_count      = 0u;
    p_game->b_window_seen  = false;
}

/**
 * @brief  Stop the round now (abort): no further events, taps ignored.
 * @param  p_game  Engine; must not be NULL.
 */
void game_rules_stop (game_t * p_game)
{
    p_game->b_running   = false;
    p_game->b_mole_up   = false;
    p_game->next_show_us = GAME_NO_MOLE_US;
    p_game->tap_count   = 0u;
}

/**
 * @brief  Stop and clear the score and counts (see game_rules.h).
 * @param  p_game  Engine; must not be NULL.
 */
void game_rules_clear (game_t * p_game)
{
    game_rules_stop(p_game);
    p_game->score       = 0;
    p_game->hits        = 0u;
    p_game->missed      = 0u;
    p_game->wrong       = 0u;
    p_game->shown       = 0u;
    p_game->reaction_us = 0u;
}

/**
 * @brief  Set the tiles that moles and taps count for.
 * @param  p_game       Engine; must not be NULL.
 * @param  active_mask  Bit n - 1 for each Node ID that counts.
 */
void game_rules_set_active (game_t * p_game, uint16_t active_mask)
{
    p_game->active = active_mask;
}

/**
 * @brief  A tap from a tile. A tap inside the window of a closed group is a
 *         duplicate and is dropped (counted); so is one with no room to wait.
 * @param  p_game    Engine; must not be NULL.
 * @param  node_id   Tapped tile, 1 to 9.
 * @param  peak      tap_detect peak.
 * @param  t_tap_us  Tap time, Pico corrected clock, microseconds.
 */
void game_rules_tap (game_t * p_game, uint8_t node_id, uint16_t peak,
               uint64_t t_tap_us)
{
    /* No arrival time: taken as arriving at its stamp (host tests). */
    game_rules_tap_rx(p_game, node_id, peak, t_tap_us, t_tap_us);
}

/**
 * @brief  A tap with its arrival time (see game_rules.h). A tap stamped
 *         within GAME_DEDUPE_MS either side of a closed group's first tap is
 *         a late duplicate and is dropped (counted).
 * @param  p_game    Engine; must not be NULL.
 * @param  node_id   Tapped tile, 1 to 9.
 * @param  peak      tap_detect peak, mg.
 * @param  t_tap_us  Tap time, Pico corrected clock, microseconds.
 * @param  t_rx_us   Arrival at the Pico, microseconds.
 */
void game_rules_tap_rx (game_t * p_game, uint8_t node_id, uint16_t peak,
                        uint64_t t_tap_us, uint64_t t_rx_us)
{
    if ((p_game->b_running) && (0u != game_bit(node_id)) &&
        (0u != (p_game->active & game_bit(node_id))))
    {
        /* Either side of the closed group's first tap (bench 11 Nov: an
         * echo stamped 3 ms before the real tap arrived after it). */
        bool const b_dup = (p_game->b_window_seen) &&
                           ((t_tap_us + GAME_DEDUPE_US) > p_game->window_us) &&
                           (t_tap_us < (p_game->window_us + GAME_DEDUPE_US));

        if (b_dup)
        {
            p_game->dedupe_drops++;
        }
        else if (p_game->tap_count >= GAME_TAP_SLOTS)
        {
            p_game->tap_drops++;
        }
        else
        {
            game_tap_t * const p_tap = &p_game->taps[p_game->tap_count];

            p_tap->node_id   = node_id;
            p_tap->peak      = peak;
            p_tap->t_tap_us  = t_tap_us;
            p_tap->t_rx_us   = t_rx_us;
            p_game->tap_count++;
        }
    }
}

/**
 * @brief  Advance the rules to now_us and report at most one event.
 * @param  p_game   Engine; must not be NULL.
 * @param  now_us   Current time, microseconds.
 * @param  p_event  Receives the event; kind is GAME_EV_NONE when none.
 * @return true when an event was produced.
 */
bool game_rules_poll (game_t * p_game, uint64_t now_us, game_event_t * p_event)
{
    bool b_event = false;

    p_event->kind       = GAME_EV_NONE;
    p_event->node_id    = 0u;
    p_event->t_show_us  = 0u;
    p_event->up_ms      = 0u;
    p_event->result     = PROTO_FEEDBACK_MISS;
    p_event->score      = p_game->score;

    if (p_game->b_running)
    {
        if (game_window_due(p_game, now_us))
        {
            b_event = game_resolve_window(p_game, p_event);
        }
        else
        {
            /* A timeout scores 0 and starts the gap; it reports nothing, so
             * the planning and the round end below run in the same call. */
            if (game_mole_expired(p_game, now_us))
            {
                p_game->missed++; /* C3: a timed-out mole. */
                p_game->b_mole_up    = false;
                p_game->next_show_us = p_game->hide_us + GAME_GAP_US;
            }

            if (game_plan_due(p_game, now_us))
            {
                b_event = game_plan_mole(p_game, now_us, p_event);
            }

            if ((!b_event) && (game_round_over(p_game, now_us)))
            {
                p_game->b_running = false;
                p_event->kind     = GAME_EV_ROUND_END;
                p_event->score    = p_game->score;
                b_event           = true;
            }
        }
    }

    return b_event;
}

/**
 * @brief  Score, clamped to -99 to 99.
 * @param  p_game  Engine; must not be NULL.
 * @return The score.
 */
int32_t game_rules_score (game_t const * p_game)
{
    return game_clamp(p_game->score);
}

/**
 * @brief  Earliest time game_rules_poll() can produce an event or change state.
 *         Candidates: a waiting dedupe window, a mole's retire time (its hide
 *         time plus GAME_TAP_GRACE_MS, unless a waiting tap could still hit
 *         it, whose window is then the candidate), the next planning time, and
 *         the round end (once no mole or tap is left to judge).
 * @param  p_game  Engine; must not be NULL.
 * @return Microseconds, or UINT64_MAX when no round is running.
 */
uint64_t game_rules_next_due (game_t const * p_game)
{
    uint64_t due = UINT64_MAX;

    if (p_game->b_running)
    {
        if (0u != p_game->tap_count)
        {
            due = game_group_due(p_game);
        }

        if (p_game->b_mole_up)
        {
            bool b_tap_before_hide = false;

            for (uint32_t index = 0u; index < p_game->tap_count; index++)
            {
                if (p_game->taps[index].t_tap_us < p_game->hide_us)
                {
                    b_tap_before_hide = true;
                }
            }

            uint64_t const retire_us = p_game->hide_us + GAME_GRACE_US;

            if ((!b_tap_before_hide) && (retire_us < due))
            {
                due = retire_us;
            }
        }
        else
        {
            if (GAME_NO_MOLE_US != p_game->next_show_us)
            {
                uint64_t const plan_us = (p_game->next_show_us > GAME_LEAD_US)
                                             ? (p_game->next_show_us -
                                                GAME_LEAD_US)
                                             : 0u;

                if (plan_us < due)
                {
                    due = plan_us;
                }
            }

            if ((0u == p_game->tap_count) && (p_game->round_end_us < due))
            {
                due = p_game->round_end_us;
            }
        }
    }

    return due;
}

/**
 * @brief  Mole time for a moment of the round at a level.
 * @param  level       proto_level_t; out of range is taken as Hard.
 * @param  elapsed_us  Time since the round started, microseconds.
 * @return Milliseconds, 500 to 2,000.
 */
uint16_t game_rules_up_ms (uint8_t level, uint64_t elapsed_us)
{
    uint8_t const  lvl        = (level < GAME_LEVELS) ? level
                                                      : (GAME_LEVELS - 1u);
    uint64_t const first      = g_up_first_ms[lvl];
    uint64_t const last       = g_up_last_ms[lvl];
    uint64_t       elapsed_ms = elapsed_us / GAME_US_PER_MS;

    if (elapsed_ms > GAME_ROUND_MS)
    {
        elapsed_ms = GAME_ROUND_MS;
    }

    /* At most 800 x 30,000: fits uint64_t many times over. */
    uint64_t const drop = ((first - last) * elapsed_ms) / GAME_ROUND_MS;

    return (uint16_t)(first - drop); /* 500 to 2,000: fits. */
}

/**
 * @brief  Stars for the round (C6).
 * @param  p_game  Engine; must not be NULL.
 * @return 1 to 3.
 */
uint8_t game_rules_stars (game_t const * p_game)
{
    uint8_t stars = 1u;

    if (0u != p_game->shown)
    {
        /* hits <= shown, both below 2^16: the product fits. */
        uint32_t const pct = (p_game->hits * GAME_PERCENT) / p_game->shown;

        if (pct >= GAME_STARS_3_PCT)
        {
            stars = 3u;
        }
        else if (pct >= GAME_STARS_2_PCT)
        {
            stars = 2u;
        }
        else
        {
            /* Below 50%: the one star every child gets. */
        }
    }

    return stars;
}

/**
 * @brief  Average show-to-tap time of this round's hits.
 * @param  p_game  Engine; must not be NULL.
 * @return Milliseconds, 0 with no hit.
 */
uint32_t game_rules_reaction_ms (game_t const * p_game)
{
    uint32_t avg = 0u;

    if (0u != p_game->hits)
    {
        /* A round's hits sum to at most 30 s: the quotient fits 32 bits. */
        avg = (uint32_t)((p_game->reaction_us / p_game->hits) /
                         GAME_US_PER_MS);
    }

    return avg;
}

/**
 * @brief  Whole seconds left in the running round, rounded up.
 * @param  p_game  Engine; must not be NULL.
 * @param  now_us  Current time, microseconds.
 * @return Seconds, 0 when no round runs or it is over.
 */
uint32_t game_rules_time_left_s (game_t const * p_game, uint64_t now_us)
{
    uint32_t left = 0u;

    if ((p_game->b_running) && (now_us < p_game->round_end_us))
    {
        /* At most 30 s ahead: the second count fits 32 bits. */
        left = (uint32_t)(((p_game->round_end_us - now_us) +
                           (GAME_US_PER_S - 1u)) /
                          GAME_US_PER_S);
    }

    return left;
}

/**
 * @brief  Next xorshift32 value; the state never becomes 0.
 * @param  p_game  Engine; must not be NULL.
 * @return The new value.
 */
static uint32_t game_next_rand (game_t * p_game)
{
    uint32_t state = p_game->rng;

    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    p_game->rng = state;

    return state;
}

/**
 * @brief  Bitmap bit of a Node ID.
 * @param  node_id  0 to 9.
 * @return Bit n - 1, or 0 for 0 or out of range.
 */
static uint16_t game_bit (uint8_t node_id)
{
    uint16_t bit = 0u;

    if ((node_id >= 1u) && (node_id <= PROTO_MAP_SLOTS))
    {
        bit = (uint16_t)(UINT16_C(1) << (node_id - 1u)); /* Below 0x200. */
    }

    return bit;
}

/**
 * @brief  Random active tile for the next mole, not the last one when another
 *         tile is active.
 * @param  p_game  Engine; must not be NULL.
 * @return Node ID 1 to 9, or 0 when no tile is active.
 */
static uint8_t game_pick_node (game_t * p_game)
{
    uint16_t candidates = p_game->active;
    uint8_t  node_id    = 0u;
    uint32_t count      = 0u;
    uint32_t pick       = 0u;

    if (0u != p_game->last_node)
    {
        /* Bits 0 to 8 only, so the narrowing keeps every bit. */
        uint16_t const others =
            (uint16_t)(candidates & ~game_bit(p_game->last_node));

        if (0u != others)
        {
            candidates = others;
        }
    }

    for (uint8_t node = 1u; node <= PROTO_MAP_SLOTS; node++)
    {
        if (0u != (candidates & game_bit(node)))
        {
            count++;
        }
    }

    if (0u != count)
    {
        pick = game_next_rand(p_game) % count;

        for (uint8_t node = 1u; (0u == node_id) && (node <= PROTO_MAP_SLOTS);
             node++)
        {
            if (0u != (candidates & game_bit(node)))
            {
                if (0u == pick)
                {
                    node_id = node;
                }
                else
                {
                    pick--;
                }
            }
        }
    }

    return node_id;
}

/**
 * @brief  Earliest tap waiting.
 * @param  p_game  Engine; must not be NULL; tap_count above 0.
 * @return Its tap time, microseconds.
 */
static uint64_t game_earliest_tap (game_t const * p_game)
{
    uint64_t earliest = UINT64_MAX;

    for (uint32_t index = 0u; index < p_game->tap_count; index++)
    {
        if (p_game->taps[index].t_tap_us < earliest)
        {
            earliest = p_game->taps[index].t_tap_us;
        }
    }

    return earliest;
}

/**
 * @brief  When the waiting group can be judged: GAME_DEDUPE_MS after its
 *         first tap time and GAME_COLLECT_MS after its first arrival.
 * @param  p_game  Engine; must not be NULL; tap_count above 0.
 * @return Microseconds.
 */
static uint64_t game_group_due (game_t const * p_game)
{
    uint64_t first_rx = UINT64_MAX;

    for (uint32_t index = 0u; index < p_game->tap_count; index++)
    {
        if (p_game->taps[index].t_rx_us < first_rx)
        {
            first_rx = p_game->taps[index].t_rx_us;
        }
    }

    uint64_t const by_stamp   = game_earliest_tap(p_game) + GAME_DEDUPE_US;
    uint64_t const by_arrival = first_rx + GAME_COLLECT_US;

    return (by_stamp > by_arrival) ? by_stamp : by_arrival;
}

/**
 * @brief  Whether the first waiting tap's window has closed.
 * @param  p_game  Engine; must not be NULL.
 * @param  now_us  Current time, microseconds.
 * @return true when a window can be resolved now.
 */
static bool game_window_due (game_t const * p_game, uint64_t now_us)
{
    return (0u != p_game->tap_count) && (now_us >= game_group_due(p_game));
}

/**
 * @brief  Resolve the window that has closed: the best tap of the group wins
 *         and is judged; the other taps of the group are dropped.
 * @param  p_game   Engine; must not be NULL.
 * @param  p_event  Receives the FEEDBACK.
 * @return true (a FEEDBACK is always produced).
 */
static bool game_resolve_window (game_t * p_game, game_event_t * p_event)
{
    uint64_t const start = game_earliest_tap(p_game);
    uint64_t const limit = start + GAME_DEDUPE_US;
    uint32_t       best  = GAME_NO_TAP;
    game_tap_t     win;
    uint32_t       kept  = 0u;
    bool           b_hit = false;

    for (uint32_t index = 0u; index < p_game->tap_count; index++)
    {
        game_tap_t const * const p_tap = &p_game->taps[index];

        if ((p_tap->t_tap_us < limit) &&
            ((GAME_NO_TAP == best) ||
             (game_tap_better(p_tap, &p_game->taps[best]))))
        {
            best = index;
        }
    }

    win = p_game->taps[best];

    for (uint32_t index = 0u; index < p_game->tap_count; index++)
    {
        if (p_game->taps[index].t_tap_us >= limit)
        {
            p_game->taps[kept] = p_game->taps[index];
            kept++;
        }
    }

    p_game->tap_count     = kept;
    p_game->b_window_seen = true;
    p_game->window_us     = start;

    b_hit = (p_game->b_mole_up) && (win.node_id == p_game->mole_node) &&
            (win.t_tap_us >= p_game->show_us) &&
            (win.t_tap_us < p_game->hide_us);

    p_event->kind    = GAME_EV_FEEDBACK;
    p_event->node_id = win.node_id;

    if (b_hit)
    {
        p_game->b_mole_up    = false;
        p_game->next_show_us = win.t_tap_us + GAME_GAP_US;
        p_game->score        = game_clamp(p_game->score + 1);
        p_game->hits++;
        p_game->reaction_us += win.t_tap_us - p_game->show_us; /* tap >= show */
        p_event->result      = PROTO_FEEDBACK_HIT;
    }
    else
    {
        /* C2: no penalty; the red X and the wrong count only. */
        p_game->wrong++;
        p_event->result = PROTO_FEEDBACK_MISS;
    }

    p_event->score = p_game->score;

    return true;
}

/**
 * @brief  Whether tap a should win over tap b: higher peak, then the earlier
 *         tap, then the lower Node ID.
 * @param  p_a  Candidate.
 * @param  p_b  Current best.
 * @return true when a wins.
 */
static bool game_tap_better (game_tap_t const * p_a, game_tap_t const * p_b)
{
    bool b_better = (p_a->peak > p_b->peak);

    if (p_a->peak == p_b->peak)
    {
        b_better = (p_a->t_tap_us < p_b->t_tap_us) ||
                   ((p_a->t_tap_us == p_b->t_tap_us) &&
                    (p_a->node_id < p_b->node_id));
    }

    return b_better;
}

/**
 * @brief  Whether the mole is up past its hide time plus the tap grace, with
 *         no tap still waiting that could hit it.
 * @param  p_game  Engine; must not be NULL.
 * @param  now_us  Current time, microseconds.
 * @return true when the mole times out now.
 */
static bool game_mole_expired (game_t const * p_game, uint64_t now_us)
{
    bool b_expired = (p_game->b_mole_up) &&
                     (now_us >= (p_game->hide_us + GAME_GRACE_US));

    for (uint32_t index = 0u; (b_expired) && (index < p_game->tap_count);
         index++)
    {
        if (p_game->taps[index].t_tap_us < p_game->hide_us)
        {
            b_expired = false;
        }
    }

    return b_expired;
}

/**
 * @brief  Whether the next mole is due to be planned.
 * @param  p_game  Engine; must not be NULL.
 * @param  now_us  Current time, microseconds.
 * @return true when MOLE_ON should go out now.
 */
static bool game_plan_due (game_t const * p_game, uint64_t now_us)
{
    return (!p_game->b_mole_up) && (GAME_NO_MOLE_US != p_game->next_show_us) &&
           (now_us < p_game->round_end_us) &&
           ((now_us + GAME_LEAD_US) >= p_game->next_show_us);
}

/**
 * @brief  Plan the next mole: a random tile, shown at the next show time or
 *         GAME_LEAD_MS from now, whichever is later. A mole that would
 *         outlast the round is not planned, and no more moles follow.
 * @param  p_game   Engine; must not be NULL.
 * @param  now_us   Current time, microseconds.
 * @param  p_event  Receives MOLE_ON.
 * @return true when a mole was planned.
 */
static bool game_plan_mole (game_t * p_game, uint64_t now_us,
                            game_event_t * p_event)
{
    uint64_t show    = p_game->next_show_us;
    uint64_t hide    = 0u;
    uint16_t up_ms   = 0u;
    uint8_t  node_id = 0u;
    bool     b_planned = false;

    if (show < (now_us + GAME_LEAD_US))
    {
        show = now_us + GAME_LEAD_US;
    }

    up_ms = game_rules_up_ms(p_game->level, show - p_game->round_start_us);
    hide  = show + (GAME_US_PER_MS * up_ms);
    node_id = game_pick_node(p_game);

    if ((hide > p_game->round_end_us) || (0u == node_id))
    {
        p_game->next_show_us = GAME_NO_MOLE_US;
    }
    else
    {
        p_game->b_mole_up    = true;
        p_game->shown++;
        p_game->mole_node    = node_id;
        p_game->show_us      = show;
        p_game->hide_us      = hide;
        p_game->last_node    = node_id;

        p_event->kind       = GAME_EV_MOLE_ON;
        p_event->node_id    = node_id;
        p_event->t_show_us  = show;
        p_event->up_ms      = up_ms;
        p_event->score      = p_game->score;
        b_planned           = true;
    }

    return b_planned;
}

/**
 * @brief  Whether the round time is up and nothing is left to judge.
 * @param  p_game  Engine; must not be NULL.
 * @param  now_us  Current time, microseconds.
 * @return true when the round ends now.
 */
static bool game_round_over (game_t const * p_game, uint64_t now_us)
{
    return (now_us >= p_game->round_end_us) && (0u == p_game->tap_count) &&
           (!p_game->b_mole_up);
}

/**
 * @brief  Clamp a score to -99 to 99.
 * @param  score  Score.
 * @return Clamped score.
 */
static int32_t game_clamp (int32_t score)
{
    int32_t clamped = score;

    if (clamped > GAME_SCORE_LIMIT)
    {
        clamped = GAME_SCORE_LIMIT;
    }
    else if (clamped < -GAME_SCORE_LIMIT)
    {
        clamped = -GAME_SCORE_LIMIT;
    }
    else
    {
        /* Inside the limits: unchanged. */
    }

    return clamped;
}

/*** end of file ***/
