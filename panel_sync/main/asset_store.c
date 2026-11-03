/**
 * @file    asset_store.c
 * @brief   Asset Storage (T-B3-01): heap probe for OD-04. See asset_store.h.
 *
 * Owner: Cheston (Buddy 3). Implemented by Ahsanul (B4) for Cheston.
 *
 * DEVIATION D-2 (module-wide): ESP-IDF names and types (size_t returns of
 * heap_caps_*, MALLOC_CAP_*).
 */
#include "asset_store.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "esp_heap_caps.h"

/**
 * @brief  Log the three heap values and the OD-04 rule (T-B3-01).
 */
void asset_store_probe_heap (void)
{
    /* DEVIATION D-2: heap_caps_* return size_t. Each value is below the
     * 512 KB of internal SRAM or the 2 MB of PSRAM, so it fits uint32_t. */
    uint32_t const largest_internal =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    /* Same range. */
    uint32_t const free_internal =
        (uint32_t)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    /* Same range. */
    uint32_t const largest_spiram =
        (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    printf("HEAP largest_internal=%" PRIu32 " free_internal=%" PRIu32
           " largest_spiram=%" PRIu32 " od04=%s\n",
           largest_internal, free_internal, largest_spiram,
           (largest_internal >= ASSET_STORE_RAM_NEEDED) ? "internal" : "psram");
}

/**
 * @brief  The validated RAM asset (T-B3-04): none until the RAM copy and the
 *         upload path exist.
 * @param  p_session_id   Unused until then.
 * @param  p_frame_count  Unused until then.
 * @return false.
 */
bool asset_store_active (uint8_t * p_session_id, uint16_t * p_frame_count)
{
    (void)p_session_id;  /* Set by T-B3-04 once an asset exists. */
    (void)p_frame_count; /* Same. */

    return false;
}

/*** end of file ***/
