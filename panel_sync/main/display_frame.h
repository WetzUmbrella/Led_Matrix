/**
 * @file    display_frame.h
 * @brief   Frame preparation for the Display Engine (T-B5-02): unpack one
 *          packed asset frame straight into the WS2812B wire buffer, with
 *          the rotation remap, and the frame index of a tick. Pure C with no
 *          ESP-IDF dependency, so the T-B5-02 host test runs on a PC.
 *
 * Owner: Jia Jun (Buddy 5), tasks T-B5-02 and T-B5-03. Implemented by
 * Ahsanul (B4) for Jia Jun, written 29 Oct 2026 in Jia Jun's session.
 *
 * - Packed frame (layout_table.md section 6, OD-05 option (a)): 192 nibbles
 *   in R0 G0 B0 R1 ... order, nibble k at bit 4k. Each nibble (0 to 8) is
 *   written unchanged into one LED byte: no scaling, no floating point
 *   (VC-6). The brightness cap is applied on the PC (VC-6, OD-02).
 * - Wire buffer: 64 LEDs x 3 bytes in WS2812B order G, R, B, LED 0 first.
 * - LEDs are numbered row by row from the top-left, LED = 8 x row + column
 *   (PROVISIONAL, owner Jia Jun: matches the prototype's frame_px(); check
 *   on the board in T-B5-06).
 * - Rotation (2-bit orientation, layout_table.md 5.7): a tile turned r
 *   degrees clockwise shows source pixel (column, row) = for 0: (c, r);
 *   90: (r, 7 - c); 180: (7 - c, 7 - r); 270: (7 - r, c), so the picture
 *   stays upright (PROVISIONAL, owner Jia Jun: direction to be confirmed in
 *   the T-B5-06 trials).
 * - Frame index: frame k of the asset is shown on the k-th tick at or after
 *   the timeline origin, modulo frame_count; counting ticks instead of
 *   microseconds avoids the rounding of 1,000,000 / 60.
 */
#ifndef DISPLAY_FRAME_H
#define DISPLAY_FRAME_H

#include <stdbool.h>
#include <stdint.h>

/** LEDs on one tile (8 x 8 WS2812B). */
#define DISPLAY_FRAME_LEDS 64u

/** Bytes in the wire buffer: 64 x 3 (LDC-7). */
#define DISPLAY_FRAME_WIRE_BYTES 192u

/** Bytes in one packed frame: 64 x 12 bits / 8 (LDC-1). */
#define DISPLAY_FRAME_PACKED_BYTES 96u

/** Largest channel value after the PC cap (VC-6). */
#define DISPLAY_FRAME_VALUE_MAX 8u

/** Status shown when no frame is playing. */
typedef enum
{
    DISPLAY_FRAME_STATUS_DARK = 0, /**< All LEDs off. */
    DISPLAY_FRAME_STATUS_NO_ASSET  /**< LED 0 blue at 2: synced, no asset. */
} display_frame_status_t;

/**
 * @brief  Unpack one packed frame into the wire buffer with the rotation
 *         remap. Every output byte is the nibble value, 0 to 15 as stored
 *         (0 to 8 for a valid asset).
 * @param  p_packed     DISPLAY_FRAME_PACKED_BYTES bytes.
 * @param  orientation  0 to 3 (0, 90, 180, 270 degrees).
 * @param  p_wire       Receives DISPLAY_FRAME_WIRE_BYTES bytes.
 */
void display_frame_unpack (uint8_t const * p_packed, uint8_t orientation,
                           uint8_t * p_wire);

/**
 * @brief  DE_PROFILE build only (OD-15): copy one 8-bit frame (192 B in
 *         R, G, B order, values already scaled on the PC) into the wire
 *         buffer with the same remap, without unpacking.
 * @param  p_rgb        DISPLAY_FRAME_WIRE_BYTES bytes.
 * @param  orientation  0 to 3.
 * @param  p_wire       Receives DISPLAY_FRAME_WIRE_BYTES bytes.
 */
void display_frame_copy_8bit (uint8_t const * p_rgb, uint8_t orientation,
                              uint8_t * p_wire);

/**
 * @brief  Fill the wire buffer with a status pattern (values at most 8).
 * @param  status  What to show.
 * @param  p_wire  Receives DISPLAY_FRAME_WIRE_BYTES bytes.
 */
void display_frame_status (display_frame_status_t status, uint8_t * p_wire);

/**
 * @brief  Frame index shown on a tick.
 * @param  tick         Tick number n.
 * @param  first_tick   First tick at or after the timeline origin.
 * @param  frame_count  Frames in the asset, at least 1.
 * @param  p_frame      Receives the frame when true.
 * @return false before first_tick (nothing to show yet).
 */
bool display_frame_index (uint64_t tick, uint64_t first_tick,
                          uint16_t frame_count, uint16_t * p_frame);

/**
 * @brief  Source LED for a physical LED under a rotation (the remap above).
 * @param  led          Physical LED, 0 to 63.
 * @param  orientation  0 to 3.
 * @return Source LED, 0 to 63.
 */
uint32_t display_frame_source_led (uint32_t led, uint8_t orientation);

#endif /* DISPLAY_FRAME_H */

/*** end of file ***/
