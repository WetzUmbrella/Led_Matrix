/**
 * @file    panel_sync.c
 * @brief   Tile firmware entry point (the same firmware on every tile):
 *          app_main starts the tile tasks at their tile_tasks.h priorities
 *          and cores and prints the T-B5-04 boot log.
 *
 * Shared file (all tile buddies). WiFi start-up, the board number and
 * wifi_event moved into tile_net (T-B3-03, plan section 0.1) on 29 Oct 2026;
 * the prototype's own-access-point option (USE_ROUTER 0) is removed (the
 * Pico access point is out of scope, OUT-3).
 *
 * DEVIATION D-2 (module-wide): ESP-IDF and FreeRTOS names and types.
 */
#include "panel_sync.h"

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "asset_store.h"
#include "display_engine.h"
#include "playback_control.h"
#include "tile_net.h"
#include "tile_tasks.h"
#include "timesync.h"

/**
 * @brief  ESP-IDF entry point: Network 12, Time Sync 16 and Playback
 *         Control 15 on core 0,
 *         Display Engine 20 on core 1 (T-B5-04), then the boot log.
 *         app_main returns; the tasks own the tile.
 */
void app_main (void)
{
    /* Before tile_net, which posts to its Upload Queue (T-B3-04). */
    if (!asset_store_start())
    {
        printf("panel_sync: asset_store_start failed\n");
    }

    if (!tile_net_start())
    {
        printf("panel_sync: tile_net_start failed\n");
    }

    if (pdPASS !=
        xTaskCreatePinnedToCore(timesync_task, TILE_TIMESYNC_NAME,
                                TILE_TIMESYNC_STACK, tile_net_timesync_queue(),
                                TILE_TIMESYNC_PRIO, NULL, TILE_TIMESYNC_CORE))
    {
        printf("panel_sync: timesync_task not created\n");
    }

    if (!playback_control_start())
    {
        printf("panel_sync: playback_control_start failed\n");
    }

    if (!display_engine_start())
    {
        printf("panel_sync: display_engine_start failed\n");
    }

    /* T-B5-04 acceptance: CPU frequency, cycle-counter wrap and vTaskList().
     * xTaskCreatePinnedToCore() returns after the task exists, so all three
     * tasks above are already in the list. */
    tile_tasks_log_boot();
}

/*** end of file ***/
