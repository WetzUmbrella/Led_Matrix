/**
 * @file    test_display.c
 * @brief   Host tests of display_frame (T-B5-02) and orient_filter
 *          (T-B5-05), run by test_main.c.
 *
 * Implemented by Ahsanul (B4) for Jia Jun, tasks T-B5-02 and T-B5-05.
 * Build and run: make -C panel_sync/test (from the repository root, so the
 * vector path below resolves; the Makefile passes it as VECTOR_DIR).
 */
#include "test_display.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "test_main.h"
#include "../main/display_frame.h"
#include "../main/fault_word.h"
#include "../main/orient_filter.h"

#ifndef VECTOR_DIR
#define VECTOR_DIR "../../protocol/vectors/"
#endif

/** Channel values 0 to 8 in the pattern vector: nibble k = k mod 9. */
#define D_PATTERN_MOD 9u

/** Channels per LED and wire positions (G, R, B). */
#define D_CH     3u
#define D_WIRE_R 0u /* The Waveshare panel is RGB (bench 5 Nov 2026). */
#define D_WIRE_G 1u
#define D_WIRE_B 2u

/** One full turn in 90 degree steps. */
#define D_TURNS 4u

/** Orientation codes. */
#define D_ORIENT_90  1u
#define D_ORIENT_180 2u
#define D_ORIENT_270 3u

/** Corner LEDs, numbered row by row from the top-left. */
#define D_LED_TOP_RIGHT   7u
#define D_LED_BOTTOM_LEFT 56u

/** Accelerometer readings: 1 g and a weak 0.25 g at 16,384 LSB/g. */
#define O_ONE_G  16384
#define O_WEAK_G 4096

/** Failures injected in O2 (plan choice: 2 s at 50 Hz). */
#define O_INJECT 100u

static bool read_vector (char const * p_name, uint8_t * p_buf);

/**
 * @brief  Test D1: vector unpack.
 * @return true when every check passed.
 */
bool test_display_unpack_vector (void)
{
    bool     b_ok = true;
    uint8_t  packed[DISPLAY_FRAME_PACKED_BYTES];
    uint8_t  wire[DISPLAY_FRAME_WIRE_BYTES];
    uint32_t equal = 0u;

    b_ok =
        (test_main_check(read_vector("frame_pattern.bin", packed), __LINE__)) &&
        b_ok;
    display_frame_unpack(packed, 0u, wire);

    for (uint32_t led = 0u; led < DISPLAY_FRAME_LEDS; led++)
    {
        uint32_t const nib = led * D_CH; /* Nibble of R. */

        equal +=
            (wire[(led * D_CH) + D_WIRE_R] == (nib % D_PATTERN_MOD)) ? 1u : 0u;
        equal += (wire[(led * D_CH) + D_WIRE_G] == ((nib + 1u) % D_PATTERN_MOD))
                     ? 1u
                     : 0u;
        equal += (wire[(led * D_CH) + D_WIRE_B] == ((nib + 2u) % D_PATTERN_MOD))
                     ? 1u
                     : 0u;
    }

    b_ok =
        (test_main_check(DISPLAY_FRAME_WIRE_BYTES == equal, __LINE__)) && b_ok;

    for (uint32_t idx = 0u; idx < DISPLAY_FRAME_WIRE_BYTES; idx++)
    {
        b_ok =
            (test_main_check(wire[idx] <= DISPLAY_FRAME_VALUE_MAX, __LINE__)) &&
            b_ok;
    }

    printf("    frame_pattern: %" PRIu32 " of %" PRIu32 " bytes equal\n", equal,
           DISPLAY_FRAME_WIRE_BYTES);

    b_ok = (test_main_check(read_vector("frame_od05_worked.bin", packed),
                            __LINE__)) &&
           b_ok;
    display_frame_unpack(packed, 0u, wire);
    b_ok = (test_main_check((1u == wire[D_WIRE_R]) && (2u == wire[D_WIRE_G]) &&
                                (3u == wire[D_WIRE_B]),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check((4u == wire[D_CH + D_WIRE_R]) &&
                                (5u == wire[D_CH + D_WIRE_G]) &&
                                (6u == wire[D_CH + D_WIRE_B]),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Test D2: rotation remap.
 * @return true when every check passed.
 */
bool test_display_rotation (void)
{
    bool b_ok = true;

    for (uint32_t led = 0u; led < DISPLAY_FRAME_LEDS; led++)
    {
        uint32_t src = led;

        for (uint32_t turn = 0u; turn < D_TURNS; turn++)
        {
            src = display_frame_source_led(src, D_ORIENT_90);
        }

        b_ok = (test_main_check(led == src, __LINE__)) && b_ok;
        b_ok = (test_main_check(((DISPLAY_FRAME_LEDS - 1u) - led) ==
                                    display_frame_source_led(led, D_ORIENT_180),
                                __LINE__)) &&
               b_ok;
    }

    for (uint8_t orient = 0u; orient < D_TURNS; orient++)
    {
        uint64_t seen = 0u;

        for (uint32_t led = 0u; led < DISPLAY_FRAME_LEDS; led++)
        {
            seen |= UINT64_C(1) << display_frame_source_led(led, orient);
        }

        b_ok = (test_main_check(UINT64_MAX == seen, __LINE__)) && b_ok;
    }

    /* Direction measured on the bench 5 Nov 2026: at 90 degrees the
     * top-left LED shows source LED 7 (top-right), at 270 source LED 56. */
    b_ok = (test_main_check(D_LED_TOP_RIGHT ==
                                display_frame_source_led(0u, D_ORIENT_90),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(D_LED_BOTTOM_LEFT ==
                                display_frame_source_led(0u, D_ORIENT_270),
                            __LINE__)) &&
           b_ok;

    /* Mounting offset measured on the bench 5 Nov 2026: a measured 0 draws
     * the 270 remap and every orientation stays in 0 to 3. */
    b_ok = (test_main_check(D_ORIENT_270 == display_frame_mounted(0u),
                            __LINE__)) &&
           b_ok;
    for (uint8_t orient = 0u; orient < D_TURNS; orient++)
    {
        b_ok = (test_main_check(display_frame_mounted(orient) < D_TURNS,
                                __LINE__)) &&
               b_ok;
    }

    return b_ok;
}

/**
 * @brief  Test D3: frame index.
 * @return true when every check passed.
 */
bool test_display_frame_index (void)
{
    bool     b_ok  = true;
    uint16_t frame = 0u;

    b_ok = (test_main_check(!display_frame_index(99u, 100u, 600u, &frame),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check((display_frame_index(100u, 100u, 600u, &frame)) &&
                                (0u == frame),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check((display_frame_index(699u, 100u, 600u, &frame)) &&
                                (599u == frame),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check((display_frame_index(700u, 100u, 600u, &frame)) &&
                                (0u == frame),
                            __LINE__)) &&
           b_ok;

    return b_ok;
}

/**
 * @brief  Test O1: 25 samples change nothing, 26 publish once.
 * @return true when every check passed.
 */
bool test_display_orient_debounce (void)
{
    bool            b_ok    = true;
    uint32_t        changes = 0u;
    orient_filter_t filter;

    orient_filter_init(&filter);

    for (uint32_t idx = 0u; idx < (ORIENT_FILTER_STABLE_SAMPLES - 1u); idx++)
    {
        changes += orient_filter_step(&filter, true, O_ONE_G, 0) ? 1u : 0u;
    }

    b_ok = (test_main_check(0u == changes, __LINE__)) && b_ok;

    /* Sample 26 publishes 90 degrees; more equal samples change nothing. */
    for (uint32_t idx = 0u; idx < ORIENT_FILTER_STABLE_SAMPLES; idx++)
    {
        changes += orient_filter_step(&filter, true, O_ONE_G, 0) ? 1u : 0u;
    }

    b_ok = (test_main_check(1u == changes, __LINE__)) && b_ok;
    b_ok = (test_main_check(D_ORIENT_90 == filter.stable, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test O2: forced I2C failures.
 * @return true when every check passed.
 */
bool test_display_orient_nack (void)
{
    bool            b_ok = true;
    uint32_t        kept = 0u;
    orient_filter_t filter;

    fault_word_clear_condition(FAULT_CONDITION_MASK);
    orient_filter_init(&filter);

    for (uint32_t idx = 0u; idx < ORIENT_FILTER_STABLE_SAMPLES; idx++)
    {
        (void)orient_filter_step(&filter, true, 0, -O_ONE_G); /* To 180. */
    }

    for (uint32_t idx = 0u; idx < O_INJECT; idx++)
    {
        (void)orient_filter_step(&filter, false, 0, 0); /* Injected NACK. */
        kept += (D_ORIENT_180 == filter.stable) ? 1u : 0u;
    }

    b_ok = (test_main_check(O_INJECT == kept, __LINE__)) && b_ok;
    b_ok = (test_main_check(0u != (fault_word_snapshot() & FAULT_BIT_IMU),
                            __LINE__)) &&
           b_ok;

    (void)orient_filter_step(&filter, true, 0, -O_ONE_G); /* Injection off. */
    b_ok = (test_main_check(0u == (fault_word_snapshot() & FAULT_BIT_IMU),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(D_ORIENT_180 == filter.stable, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Test O3: classification and flicker.
 * @return true when every check passed.
 */
bool test_display_orient_classify (void)
{
    bool            b_ok    = true;
    uint32_t        changes = 0u;
    orient_filter_t filter;

    b_ok =
        (test_main_check(0u == orient_filter_classify(0, O_ONE_G), __LINE__)) &&
        b_ok;
    b_ok =
        (test_main_check(1u == orient_filter_classify(O_ONE_G, 0), __LINE__)) &&
        b_ok;
    b_ok = (test_main_check(2u == orient_filter_classify(0, -O_ONE_G),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(3u == orient_filter_classify(-O_ONE_G, 0),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(ORIENT_FILTER_NONE ==
                                orient_filter_classify(O_WEAK_G, O_WEAK_G),
                            __LINE__)) &&
           b_ok;
    b_ok = (test_main_check(ORIENT_FILTER_NONE ==
                                orient_filter_classify(INT16_MIN, INT16_MIN),
                            __LINE__)) &&
           b_ok;

    orient_filter_init(&filter);

    /* 25 samples at 270, one weak sample, then 25 more: never 26 in a row. */
    for (uint32_t idx = 0u; idx < (ORIENT_FILTER_STABLE_SAMPLES - 1u); idx++)
    {
        changes += orient_filter_step(&filter, true, -O_ONE_G, 0) ? 1u : 0u;
    }

    changes += orient_filter_step(&filter, true, O_WEAK_G, 0) ? 1u : 0u;

    for (uint32_t idx = 0u; idx < (ORIENT_FILTER_STABLE_SAMPLES - 1u); idx++)
    {
        changes += orient_filter_step(&filter, true, -O_ONE_G, 0) ? 1u : 0u;
    }

    b_ok = (test_main_check(0u == changes, __LINE__)) && b_ok;

    return b_ok;
}

/**
 * @brief  Read one 96 B frame vector from protocol/vectors.
 * @param  p_name  File name.
 * @param  p_buf   Receives DISPLAY_FRAME_PACKED_BYTES bytes.
 * @return true when the whole frame was read.
 */
static bool read_vector (char const * p_name, uint8_t * p_buf)
{
    char path[128];
    bool b_ok = false;

    /* The path fits: VECTOR_DIR plus a short file name. */
    (void)snprintf(path, sizeof(path), "%s%s", VECTOR_DIR, p_name);

    FILE * const p_file = fopen(path, "rb");

    if (NULL != p_file)
    {
        b_ok = (DISPLAY_FRAME_PACKED_BYTES ==
                fread(p_buf, 1u, DISPLAY_FRAME_PACKED_BYTES, p_file));
        (void)fclose(p_file); /* Read-only file: nothing to flush. */
    }

    return b_ok;
}

/*** end of file ***/
