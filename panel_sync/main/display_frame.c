/**
 * @file    display_frame.c
 * @brief   Frame preparation for the Display Engine (T-B5-02). See
 *          display_frame.h.
 *
 * Owner: Jia Jun (Buddy 5). Implemented by Ahsanul (B4) for Jia Jun, task
 * T-B5-02.
 *
 * No ESP-IDF includes: this module is host-testable.
 */
#include "display_frame.h"

#include <string.h>

/** Side of the square matrix. */
#define DF_SIDE 8u

/** Largest row or column index. */
#define DF_LAST (DF_SIDE - 1u)

/** Channels per LED and their positions in the wire buffer (G, R, B). */
#define DF_CHANNELS 3u
#define DF_WIRE_G   0u
#define DF_WIRE_R   1u
#define DF_WIRE_B   2u

/** Nibble bits and mask. */
#define DF_NIBBLE_BITS 4u
#define DF_NIBBLE_MASK 0x0Fu

/** Nibbles per byte. */
#define DF_NIBBLES_PER_BYTE 2u

/** Orientation codes (layout_table.md section 5.7). */
#define DF_ORIENT_90  1u
#define DF_ORIENT_180 2u
#define DF_ORIENT_270 3u

/** Status pattern: LED 0 blue at 2 (at most 8, VC-6). */
#define DF_STATUS_VALUE 2u

static uint8_t df_nibble (uint8_t const * p_packed, uint32_t nibble);

/**
 * @brief  Unpack one packed frame into the wire buffer with the remap.
 * @param  p_packed     96 bytes.
 * @param  orientation  0 to 3.
 * @param  p_wire       Receives 192 bytes.
 */
void display_frame_unpack (uint8_t const * p_packed, uint8_t orientation,
                           uint8_t * p_wire)
{
    for (uint32_t led = 0u; led < DISPLAY_FRAME_LEDS; led++)
    {
        uint32_t const  src   = display_frame_source_led(led, orientation);
        uint32_t const  base  = src * DF_CHANNELS; /* Nibble of R. */
        uint8_t * const p_out = &p_wire[led * DF_CHANNELS];

        p_out[DF_WIRE_R] = df_nibble(p_packed, base);
        p_out[DF_WIRE_G] = df_nibble(p_packed, base + 1u);
        p_out[DF_WIRE_B] = df_nibble(p_packed, base + 2u);
    }
}

/**
 * @brief  DE_PROFILE: copy one 8-bit RGB frame with the remap.
 * @param  p_rgb        192 bytes, R, G, B per LED.
 * @param  orientation  0 to 3.
 * @param  p_wire       Receives 192 bytes.
 */
void display_frame_copy_8bit (uint8_t const * p_rgb, uint8_t orientation,
                              uint8_t * p_wire)
{
    for (uint32_t led = 0u; led < DISPLAY_FRAME_LEDS; led++)
    {
        uint8_t const * const p_in =
            &p_rgb[display_frame_source_led(led, orientation) * DF_CHANNELS];
        uint8_t * const p_out = &p_wire[led * DF_CHANNELS];

        p_out[DF_WIRE_R] = p_in[0];
        p_out[DF_WIRE_G] = p_in[1];
        p_out[DF_WIRE_B] = p_in[2];
    }
}

/**
 * @brief  Fill the wire buffer with a status pattern.
 * @param  status  What to show.
 * @param  p_wire  Receives 192 bytes.
 */
void display_frame_status (display_frame_status_t status, uint8_t * p_wire)
{
    (void)memset(p_wire, 0, DISPLAY_FRAME_WIRE_BYTES); /* Returns p_wire. */

    if (DISPLAY_FRAME_STATUS_NO_ASSET == status)
    {
        p_wire[DF_WIRE_B] = DF_STATUS_VALUE;
    }
}

/**
 * @brief  Frame index shown on a tick.
 * @param  tick         Tick number.
 * @param  first_tick   First tick at or after the origin.
 * @param  frame_count  At least 1.
 * @param  p_frame      Receives the frame when true.
 * @return false before first_tick.
 */
bool display_frame_index (uint64_t tick, uint64_t first_tick,
                          uint16_t frame_count, uint16_t * p_frame)
{
    bool const b_started = (tick >= first_tick) && (frame_count > 0u);

    if (b_started)
    {
        /* The remainder is below frame_count (at most 1023), so it fits. */
        *p_frame = (uint16_t)((tick - first_tick) % frame_count);
    }

    return b_started;
}

/**
 * @brief  Source LED for a physical LED under a rotation.
 * @param  led          0 to 63.
 * @param  orientation  0 to 3.
 * @return Source LED.
 */
uint32_t display_frame_source_led (uint32_t led, uint8_t orientation)
{
    uint32_t const row     = led / DF_SIDE;
    uint32_t const col     = led % DF_SIDE;
    uint32_t       src_row = row;
    uint32_t       src_col = col;

    /* 90 and 270 directions measured on the bench 5 Nov 2026: the earlier
     * mapping turned a tile rotated 90 degrees clockwise upside down. */
    if (DF_ORIENT_90 == orientation)
    {
        src_col = DF_LAST - row;
        src_row = col;
    }
    else if (DF_ORIENT_180 == orientation)
    {
        src_col = DF_LAST - col;
        src_row = DF_LAST - row;
    }
    else if (DF_ORIENT_270 == orientation)
    {
        src_col = row;
        src_row = DF_LAST - col;
    }
    else
    {
        /* 0 degrees: unchanged. */
    }

    return (src_row * DF_SIDE) + src_col;
}

/**
 * @brief  Nibble k of a packed frame (bit 4k, layout_table.md section 6).
 * @param  p_packed  96 bytes.
 * @param  nibble    0 to 191.
 * @return The 4-bit value.
 */
static uint8_t df_nibble (uint8_t const * p_packed, uint32_t nibble)
{
    uint32_t const shift = (nibble % DF_NIBBLES_PER_BYTE) * DF_NIBBLE_BITS;

    /* Masked to 4 bits, so it fits uint8_t. */
    return (
        uint8_t)(((uint32_t)p_packed[nibble / DF_NIBBLES_PER_BYTE] >> shift) &
                 DF_NIBBLE_MASK);
}

/*** end of file ***/
