/**
 * @file    game_rules.h
 * @brief   Whack-a-mole rules v1 on the Pico (whack-a-mole re-plan, build
 *          step 7): one mole at a time on a random active tile, mole time
 *          shrinking over the round, hit and miss scoring, tap dedupe within
 *          30 ms. Pure C with no hardware call and no clock of its own: every
 *          function takes the time, so the host tests run it on a fake clock
 *          with an injected RNG seed.
 *
 * Owner: Benjamin (Buddy 2), game engine (re-plan table: "Benjamin (B2):
 * Pico COORDINATING_GAME, game engine"). Written for build step 7.
 *
 * Rules (re-plan "v1 rules"):
 * - One mole at a time on a random active tile. Never the same tile twice
 *   in a row, unless only one tile is active (then the rule cannot hold and
 *   the same tile repeats; plan choice).
 * - The mole stays up for 1200 ms at the start of the round, shrinking
 *   linearly to 500 ms at GAME_ROUND_MS (up_ms is an integer, rounded down).
 * - 300 ms gap between moles (plan choice: the gap starts at the hit, the
 *   timeout or the start of the round).
 * - A hit (a tap on the mole tile while it is up) scores +1 and ends the
 *   mole at once. A tap on any other active tile, or any tap while no mole
 *   is up, scores -1. A mole that times out with no tap scores 0. A mole
 *   times out GAME_TAP_GRACE_MS after its hide time, so a tap stamped before
 *   the hide that arrives late still hits it.
 * - Taps from several tiles within GAME_DEDUPE_MS (30 ms, from the first
 *   tap of the group, by the tile's own corrected time) count once: the
 *   highest peak wins, the others are dropped (plan choice for ties: the
 *   earlier tap, then the lower Node ID).
 * - MOLE_ON is sent GAME_LEAD_MS before the mole shows, so the tile has the
 *   message in time (plan choice). The 300 ms gap runs from the hit tap, or
 *   from the hide time of a mole that timed out. A mole is planned only if
 *   it hides before the round ends.
 */
#ifndef GAME_RULES_H
#define GAME_RULES_H

#include <stdbool.h>
#include <stdint.h>

#include "proto.h"

/** Round length, milliseconds (re-plan: a fixed 30 s round). */
#define GAME_ROUND_MS 30000u

/** Mole time at the start and at the end of the round, milliseconds. */
#define GAME_UP_FIRST_MS 1200u
#define GAME_UP_LAST_MS  500u

/** Gap between a mole ending and the next one showing, milliseconds (plan
 *  choice; the re-plan says only "300 ms gap between moles"). */
#define GAME_GAP_MS 300u

/** Lead time of MOLE_ON before t_show, milliseconds (plan choice: four resends
 *  at the 50 ms retry interval fit inside it; the gap stays exact). */
#define GAME_LEAD_MS 200u

/** Tap dedupe window, milliseconds (re-plan: 50 ms; 30 ms from DS-07, 11 Nov:
 *  bench echoes came 2 to 3 ms after the tap, so the flash comes sooner). */
#define GAME_DEDUPE_MS 30u

/** Grace after a mole's hide time before it is retired, milliseconds (plan
 *  choice). A tap stamped before the hide can still be in flight: the radio
 *  hop plus the 50 ms resend. The mole stays up until hide + this grace and
 *  no waiting tap stamped before the hide is left, so such a tap still hits.
 *  The next show stays at hide + GAME_GAP_MS, so the grace must not pass the
 *  planning point, GAME_GAP_MS - GAME_LEAD_MS (checked in game_rules.c). */
#define GAME_TAP_GRACE_MS 100u

/** Taps waiting for their dedupe window, at most (plan choice; 9 tiles tap
 *  at most once or twice per window). */
#define GAME_TAP_SLOTS 16u

/** Output of game_rules_poll(): at most one per call. */
typedef enum
{
    GAME_EV_NONE = 0,  /**< Nothing to send. */
    GAME_EV_MOLE_ON,   /**< Send MOLE_ON to node_id: t_show_us, up_ms. */
    GAME_EV_FEEDBACK,  /**< Send FEEDBACK to node_id: result. */
    GAME_EV_ROUND_END  /**< The round is over: send END, then SCORE_MASK. */
} game_event_kind_t;

/** One output event. */
typedef struct
{
    game_event_kind_t kind;
    uint8_t           node_id;  /**< Addressed tile, 1 to 9. */
    uint64_t          t_show_us; /**< MOLE_ON: show time, Pico clock. */
    uint16_t          up_ms;    /**< MOLE_ON: how long it stays up. */
    uint8_t           result;   /**< FEEDBACK: proto_feedback_t. */
    int32_t           score;    /**< Score after the event. */
} game_event_t;

/** One tap waiting for its dedupe window. */
typedef struct
{
    uint8_t  node_id; /**< Tapped tile, 1 to 9. */
    uint16_t peak;    /**< tap_detect peak, LSB. */
    uint64_t t_tap_us; /**< Tap time, Pico corrected clock. */
} game_tap_t;

/** The engine. Zero it with game_rules_init(). */
typedef struct
{
    bool        b_running;      /**< A round is in progress. */
    uint32_t    rng;            /**< xorshift32 state, never 0. */
    uint64_t    round_start_us; /**< Round start, Pico clock. */
    uint64_t    round_end_us;   /**< Round end, Pico clock. */
    int32_t     score;          /**< -99 to 99. */
    uint16_t    active;         /**< Bit n - 1: Node ID n is in the wall. */
    uint8_t     last_node;      /**< Last mole tile, 0 before the first. */
    bool        b_mole_up;      /**< A mole is planned or up, not yet hit. */
    uint8_t     mole_node;      /**< Its tile. */
    uint64_t    show_us;        /**< Its show time. */
    uint64_t    hide_us;        /**< Its hide time. */
    uint64_t    next_show_us;   /**< Earliest show of the next mole. */
    uint32_t    tap_count;      /**< Taps waiting in taps[]. */
    game_tap_t  taps[GAME_TAP_SLOTS];
    bool        b_window_seen;  /**< A dedupe window has closed. */
    uint64_t    window_us;      /**< Start of the last closed window. */
    uint32_t    dedupe_drops;   /**< Taps dropped as duplicates. */
    uint32_t    tap_drops;      /**< Taps dropped: no room to wait. */
} game_t;

/**
 * @brief  Reset the engine: no round, score 0, the RNG seeded.
 * @param  p_game  Engine; must not be NULL.
 * @param  seed    RNG seed; 0 is replaced by 1 (xorshift needs a non-zero
 *                 state).
 */
void game_rules_init (game_t * p_game, uint32_t seed);

/**
 * @brief  Start a round at now_us with the tiles in active_mask.
 * @param  p_game       Engine; must not be NULL.
 * @param  now_us       Round start, Pico clock, microseconds.
 * @param  active_mask  Bit n - 1 for each Node ID in the wall.
 */
void game_rules_start (game_t * p_game, uint64_t now_us, uint16_t active_mask);

/**
 * @brief  Stop the round now (abort): no further events, taps ignored.
 * @param  p_game  Engine; must not be NULL.
 */
void game_rules_stop (game_t * p_game);

/**
 * @brief  Set the tiles that moles and taps count for (a tile was lost or
 *         came back during the round).
 * @param  p_game       Engine; must not be NULL.
 * @param  active_mask  Bit n - 1 for each Node ID that counts.
 */
void game_rules_set_active (game_t * p_game, uint16_t active_mask);

/**
 * @brief  A tap from a tile. Ignored outside a round and for a tile that is
 *         not active.
 * @param  p_game    Engine; must not be NULL.
 * @param  node_id   Tapped tile, 1 to 9.
 * @param  peak      tap_detect peak.
 * @param  t_tap_us  Tap time, Pico corrected clock, microseconds.
 */
void game_rules_tap (game_t * p_game, uint8_t node_id, uint16_t peak,
               uint64_t t_tap_us);

/**
 * @brief  Advance the rules to now_us and report at most one event.
 * @param  p_game   Engine; must not be NULL.
 * @param  now_us   Current time, Pico clock, microseconds.
 * @param  p_event  Receives the event; kind is GAME_EV_NONE when none.
 * @return true when an event was produced.
 */
bool game_rules_poll (game_t * p_game, uint64_t now_us, game_event_t * p_event);

/**
 * @brief  Score, clamped to -99 to 99.
 * @param  p_game  Engine; must not be NULL.
 * @return The score.
 */
int32_t game_rules_score (game_t const * p_game);

/**
 * @brief  Earliest time game_rules_poll() can produce an event or change state.
 *         The coordinator calls game_rules_poll() only from that time, so a
 *         round that is waiting does not take passes from the receive path.
 * @param  p_game  Engine; must not be NULL.
 * @return Microseconds, or UINT64_MAX when no round is running.
 */
uint64_t game_rules_next_due (game_t const * p_game);

/**
 * @brief  Mole time for a moment of the round: 1200 ms at the start, 500 ms
 *         at GAME_ROUND_MS, linear in between, rounded down.
 * @param  elapsed_us  Time since the round started, microseconds (values
 *                     past the round use the end value).
 * @return Milliseconds, 500 to 1200.
 */
uint16_t game_rules_up_ms (uint64_t elapsed_us);

#endif /* GAME_RULES_H */

/*** end of file ***/
