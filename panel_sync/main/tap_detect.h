/**
 * @file    tap_detect.h
 * @brief   Software tap detection from the accelerometer samples (DS-02,
 *          build step 2): a spike over the threshold starts a tap, its peak
 *          is taken over a short window, and the detector re-arms only once
 *          the tile is quiet again, so a ringing slap counts once. Pure C
 *          (no ESP-IDF includes), host-testable on a PC.
 *
 * Owner: Jia Jun (Buddy 5). Rewritten 11 Nov 2026 from the bench data of
 * docs/bench/9nov (F-05, F-10): the QMI8658 tap engine at 100 Hz caught about
 * 70% of taps, and its 150 ms refractory let a slap that rings up to 600 ms
 * count twice.
 *
 * - Samples: about 1 kHz, QMI8658 at +/-16 g (2,048 LSB per g): slaps reach
 *   15 to 28 g, and at +/-8 g a slap and its echo would both clip and tie
 *   under "highest peak wins" (DS-03).
 * - Deviation: |a - baseline| over the three axes, in mg. The baseline is a
 *   per-axis average (500 ms time constant) that stands for gravity; it is
 *   held while a tap or a loud sample is under way, and reseeded from the
 *   sample after TAP_DETECT_HOLD_MAX_SAMPLES held samples (a bad first sample,
 *   or a tile turned to a new orientation).
 * - A tap: the first sample at or over the threshold; its peak is the
 *   largest deviation in the TAP_DETECT_PEAK_WINDOW_US after it; its time is
 *   that first sample's. The event is reported when the window ends.
 * - Re-arm: no sooner than TAP_DETECT_DEAD_US after the tap, then after
 *   TAP_DETECT_QUIET_US under half the threshold, or at most
 *   TAP_DETECT_REARM_MAX_US after the tap.
 */
#ifndef TAP_DETECT_H
#define TAP_DETECT_H

#include <stdbool.h>
#include <stdint.h>

/** Accelerometer scale at +/-16 g, LSB per g. */
#define TAP_DETECT_LSB_PER_G 2048

/** Default threshold, mg. 200 mg from DS-02 (F-05: 17 to 20 of 20 soft taps
 *  at 150 to 300 mg) until 12 Nov 2026; raised to 300 mg on the wall (step 3,
 *  round 5): moving a held tile gave peaks of 200 to 300 mg, real taps a
 *  median of 561 mg, and one soft tap of 30 went unseen. ARM can set
 *  another value (A12). */
#define TAP_DETECT_THRESHOLD_MG 300u

/** The threshold the detector's unit tests are written for, mg (their
 *  edge cases sit at 199 and 200 mg). */
#define TAP_DETECT_TEST_THRESHOLD_MG 200u

/** Threshold limits, mg. */
#define TAP_DETECT_THRESHOLD_MIN_MG 50u
#define TAP_DETECT_THRESHOLD_MAX_MG 5000u

/** Peak window after the first sample over the threshold, microseconds. */
#define TAP_DETECT_PEAK_WINDOW_US INT64_C(10000)

/** Least time after a tap before the detector can re-arm, us (bench 11 Nov
 *  2026, round 13: a 16.9 g slap rebounded with 652 mg 146 ms later, after a
 *  short quiet spell, and counted again; a mole is gone after its hit and the
 *  next is never on the same tile, so a second tap within 250 ms means
 *  nothing). */
#define TAP_DETECT_DEAD_US INT64_C(250000)

/** Quiet time under half the threshold that re-arms the detector, us. */
#define TAP_DETECT_QUIET_US INT64_C(30000)

/** Longest time after a tap before the detector re-arms anyway, us (F-05:
 *  slaps stay over 150 mg for up to 610 ms). */
#define TAP_DETECT_REARM_MAX_US INT64_C(600000)

/** Deviation over which the baseline is held, mg. */
#define TAP_DETECT_HOLD_MG 100u

/** Held samples after which the baseline is reseeded (1 s at 1 kHz: longer
 *  than any tap, so a ringing slap never reseeds it). */
#define TAP_DETECT_HOLD_MAX_SAMPLES 1000u

/** Baseline time constant in samples (500 ms at 1 kHz). */
#define TAP_DETECT_EMA_SAMPLES 500

/** One tap. */
typedef struct
{
    int64_t  t_us;  /**< First sample at or over the threshold. */
    uint32_t count; /**< Taps so far, this one included. */
    uint16_t peak;  /**< Largest deviation in the peak window, mg. */
} tap_detect_event_t;

/** Detector phase. */
typedef enum
{
    TAP_DETECT_ARMED = 0, /**< Waiting for a sample over the threshold. */
    TAP_DETECT_PEAK,      /**< In the peak window of a tap. */
    TAP_DETECT_SETTLING   /**< Tap reported; waiting for quiet to re-arm. */
} tap_detect_phase_t;

/** Detector state. Zero it with tap_detect_init(). */
typedef struct
{
    int64_t            base_q[3];    /**< Per-axis baseline, LSB x 65,536. */
    bool               b_seeded;     /**< A baseline exists. */
    uint32_t           held;         /**< Consecutive held samples. */
    uint32_t           threshold_mg; /**< Tap threshold, mg. */
    tap_detect_phase_t phase;        /**< Current phase. */
    int64_t            t_first_us;   /**< First sample of the current tap. */
    uint32_t           peak_mg;      /**< Peak so far in the window, mg. */
    bool               b_quiet;      /**< Quiet run under way (settling). */
    int64_t            t_quiet_us;   /**< Start of the quiet run. */
    uint32_t           last_mg;      /**< Deviation of the last sample, mg. */
    uint32_t           count;        /**< Taps reported so far. */
} tap_detect_t;

/**
 * @brief  Reset the detector: no baseline, armed, default threshold.
 * @param  p_detect  The detector; must not be NULL.
 */
void tap_detect_init (tap_detect_t * p_detect);

/**
 * @brief  Set the threshold, clamped to TAP_DETECT_THRESHOLD_MIN_MG to
 *         TAP_DETECT_THRESHOLD_MAX_MG.
 * @param  p_detect      The detector; must not be NULL.
 * @param  threshold_mg  Threshold, mg.
 */
void tap_detect_set_threshold (tap_detect_t * p_detect, uint32_t threshold_mg);

/**
 * @brief  One accelerometer sample (raw, +/-16 g).
 * @param  p_detect  The detector; must not be NULL.
 * @param  t_us      Sample time, microseconds.
 * @param  accel_x   Raw X.
 * @param  accel_y   Raw Y.
 * @param  accel_z   Raw Z.
 * @param  p_event   Receives the tap when the return is true.
 * @return true when a tap's peak window ended with this sample.
 */
bool tap_detect_sample (tap_detect_t * p_detect, int64_t t_us, int16_t accel_x,
                        int16_t accel_y, int16_t accel_z,
                        tap_detect_event_t * p_event);

/**
 * @brief  Deviation of the last sample, mg (for logging).
 * @param  p_detect  The detector; must not be NULL.
 * @return mg.
 */
uint32_t tap_detect_last_mg (tap_detect_t const * p_detect);

#endif /* TAP_DETECT_H */

/*** end of file ***/
