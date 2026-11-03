/**
 * @file    asset_store.h
 * @brief   Asset Storage (T-B3-01 now; T-B3-04 later): the heap probe that
 *          settles OD-04 (internal SRAM or PSRAM for the RAM asset copy).
 *
 * Owner: Cheston (Buddy 3), task T-B3-01. Implemented by Ahsanul (B4) for
 * Cheston, written 29 Oct 2026 in Ben's (B2) session.
 * Implemented: asset_store_probe_heap(), called by tile_net once, after the
 * first IP_EVENT_STA_GOT_IP, when every Week 10 tile task exists.
 * Not yet implemented: the Asset Storage Task, the RAM copy, the pointer
 * publication and asset_store_flash_allowed() (T-B3-04); chunk receive and
 * verification (T-B3-05).
 *
 * DEVIATION D-2 (module-wide): ESP-IDF names and types.
 */
#ifndef ASSET_STORE_H
#define ASSET_STORE_H

#include <stdint.h>

/** RAM needed for the packed asset copy: 57,600 B payload plus the 32,768 B
 *  margin (proposed, RAR-4) = 90,368 B (T-B3-01 decision rule for OD-04). */
#define ASSET_STORE_RAM_NEEDED 90368u

/**
 * @brief  Log the largest free internal block, the free internal heap and
 *         the largest free PSRAM block on one line, with the OD-04 rule's
 *         answer: "HEAP largest_internal=<B> free_internal=<B>
 *         largest_spiram=<B> od04=<internal|psram>".
 */
void asset_store_probe_heap (void);

#endif /* ASSET_STORE_H */

/*** end of file ***/
