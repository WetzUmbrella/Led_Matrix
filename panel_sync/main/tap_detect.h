/**
 * @file    tap_detect.h
 * @brief   QMI8658 tap detection: turns the tap interrupt into a validated
 *          event with a peak size, for the hardest-hit tile TAP message.
 *          Pure C (no ESP-IDF includes), host-testable on a PC.
 *
 * Owner: Jia Jun (Buddy 5). Implemented by Ahsanul (B4) for Jia Jun.
 */
#ifndef TAP_DETECT_H
#define TAP_DETECT_H

#include <stdbool.h>
#include <stdint.h>

/** Samples in the detection window (200 ms at the 100 Hz sample rate). */
#define TAP_DETECT_WINDOW_SAMPLES 20u

/** Minimum accepted peak, LSB (0.5 g at 4,096 LSB/g, +/- 8 g range).
 *  PROVISIONAL: tune on the board. */
#define TAP_DETECT_MIN_PEAK_LSB 2048u

/** Refractory period after an accepted tap, microseconds.
 *  PROVISIONAL: tune on the board. */
#define TAP_DETECT_REFRACTORY_US INT64_C(150000)

/** Bytes in each QMI8658 CTRL_CMD_CONFIGURE_TAP page. */
#define TAP_DETECT_CONFIG_BYTES 8u

/** No axis (invalid). */
#define TAP_DETECT_AXIS_NONE 0u
/** X axis. */
#define TAP_DETECT_AXIS_X 1u
/** Y axis. */
#define TAP_DETECT_AXIS_Y 2u
/** Z axis. */
#define TAP_DETECT_AXIS_Z 3u

/** Result of an interrupt or a status decode. */
typedef enum
{
    TAP_DETECT_ACCEPT = 0,        /**< Tap accepted as a new event. */
    TAP_DETECT_REJECT_STATUS,     /**< Invalid tap count or axis. */
    TAP_DETECT_REJECT_REFRACTORY, /**< Within the refractory period. */
    TAP_DETECT_REJECT_WEAK        /**< Peak below the minimum. */
} tap_detect_result_t;

/** One validated tap event. */
typedef struct
{
    int64_t  t_us;       /**< Interrupt time. */
    uint32_t count;      /**< Accepted taps so far, including this one. */
    uint16_t peak;       /**< Peak window value, LSB. */
    uint8_t  axis;       /**< TAP_DETECT_AXIS_*. */
    bool     b_positive; /**< Polarity: true = positive. */
} tap_detect_event_t;

/** Detector state: sliding window, slow baselines, accepted/rejected counts. */
typedef struct
{
    uint16_t window[TAP_DETECT_WINDOW_SAMPLES]; /**< Deviation per sample. */
    uint32_t next;                              /**< Next window slot. */
    int32_t  base_x;                            /**< 16x slow average, X. */
    int32_t  base_y;                            /**< 16x slow average, Y. */
    int32_t  base_z;                            /**< 16x slow average, Z. */
    bool     b_seeded;                          /**< True after first sample. */
    bool     b_has_last;                        /**< True after a tap. */
    int64_t  last_us;                           /**< Last accepted tap time. */
    uint32_t accepted;                          /**< Accepted taps so far. */
    uint32_t rejected;                          /**< Rejected interrupts. */
} tap_detect_t;

/**
 * @brief  Zero the detector state.
 * @param  p_detect  The detector.
 */
void tap_detect_init (tap_detect_t * p_detect);

/**
 * @brief  One 100 Hz accelerometer sample: update the baselines and the
 *         sliding window.
 * @param  p_detect  The detector.
 * @param  accel_x   Raw X, 16-bit.
 * @param  accel_y   Raw Y, 16-bit.
 * @param  accel_z   Raw Z, 16-bit.
 */
void tap_detect_sample (tap_detect_t * p_detect, int16_t accel_x,
                        int16_t accel_y, int16_t accel_z);

/**
 * @brief  Largest value in the sliding window.
 * @param  p_detect  The detector.
 * @return 0 to 65,535.
 */
uint16_t tap_detect_peak (tap_detect_t const * p_detect);

/**
 * @brief  Handle a tap interrupt: validate status, refractory and peak.
 * @param  p_detect   The detector.
 * @param  t_us       Interrupt time, microseconds.
 * @param  tap_status QMI8658 tap status byte.
 * @param  p_event    Filled only on TAP_DETECT_ACCEPT.
 * @return TAP_DETECT_ACCEPT or a TAP_DETECT_REJECT_* reason.
 */
tap_detect_result_t tap_detect_on_irq (tap_detect_t * p_detect, int64_t t_us,
                                       uint8_t tap_status,
                                       tap_detect_event_t * p_event);

/**
 * @brief  Write the two QMI8658 tap-config pages (register order,
 *         little-endian 16-bit fields).
 * @param  p_first   Buffer of TAP_DETECT_CONFIG_BYTES (page 1).
 * @param  p_second  Buffer of TAP_DETECT_CONFIG_BYTES (page 2).
 */
void tap_detect_encode_config (uint8_t * p_first, uint8_t * p_second);

#endif /* TAP_DETECT_H */

/*** end of file ***/
