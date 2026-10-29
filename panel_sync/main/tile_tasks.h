/**
 * @file    tile_tasks.h
 * @brief   Tile task table (T-B5-04): name, priority, core and stack of every
 *          tile task. Every tile task is created with xTaskCreatePinnedToCore
 *          from these constants.
 *
 * Implemented by Ahsanul (B4) for Jia Jun, task T-B5-04.
 * Implemented: the six task rows with the PostTri priorities and cores, the
 * six sdkconfig.defaults options of the T-B5-04 row, and the boot log
 * (tile_tasks_log_boot(): CPU frequency, cycle-counter wrap time and
 * vTaskList() with the core column) for the T-B5-04 acceptance.
 * Not yet implemented: the Orientation, Playback Control and Asset Storage
 * tasks themselves (T-B5-05, T-B4-04, T-B3-04 check them with the same
 * vTaskList() method); stack sizes from measured high-water marks.
 *
 * CPU frequency (VC-8a, Round 0 ruling SC-5: recorded, not changed): the
 * built sdkconfig sets CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=160, so the 32-bit
 * cycle counter wraps every 2^32 / 160,000,000 Hz = 26.8 s. tile_tasks.c
 * stops the build if the configured value differs from TILE_CPU_FREQ_MHZ.
 *
 * Priorities and cores are PostTri "proposed" values (W6D-4a, PostTri
 * Defence snapshot row 4): Display Engine 20 and Orientation 5 on core 1;
 * Time Sync 16, Playback Control 15, Network 12 and Asset Storage 8 on core 0.
 * ESP-IDF WiFi (23) and esp_timer (22) stay on core 0; lwIP tcpip (18) is
 * pinned to core 0 by CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0.
 * PROVISIONAL (priorities and cores, owner Jia Jun): every value above is
 * (proposed, W6D-4a) until the vTaskList() boot log is checked on a tile.
 * PROVISIONAL (stack sizes, owner Jia Jun): 4096 B for every task, the size
 * the prototype's render and sync_rx tasks used; unmeasured, to be set from
 * uxTaskGetStackHighWaterMark() (the stack column of the boot log).
 */
#ifndef TILE_TASKS_H
#define TILE_TASKS_H

/** Configured CPU frequency, MHz (VC-8a: recorded, not changed). */
#define TILE_CPU_FREQ_MHZ 160u

/** Core 0: WiFi, lwIP and the network-facing tasks. */
#define TILE_CORE_NET 0

/** Core 1: the Display Engine and Orientation. */
#define TILE_CORE_DISPLAY 1

/** Display Engine Task (T-B5-01). */
#define TILE_DISPLAY_NAME  "display"
#define TILE_DISPLAY_PRIO  20u
#define TILE_DISPLAY_CORE  TILE_CORE_DISPLAY
#define TILE_DISPLAY_STACK 4096u

/** Orientation Task (T-B5-05). */
#define TILE_ORIENT_NAME  "orientation"
#define TILE_ORIENT_PRIO  5u
#define TILE_ORIENT_CORE  TILE_CORE_DISPLAY
#define TILE_ORIENT_STACK 4096u

/** Time Sync Task (T-B4-02). */
#define TILE_TIMESYNC_NAME  "timesync"
#define TILE_TIMESYNC_PRIO  16u
#define TILE_TIMESYNC_CORE  TILE_CORE_NET
#define TILE_TIMESYNC_STACK 4096u

/** Playback Control Task (T-B4-04). */
#define TILE_PLAYBACK_NAME  "playback"
#define TILE_PLAYBACK_PRIO  15u
#define TILE_PLAYBACK_CORE  TILE_CORE_NET
#define TILE_PLAYBACK_STACK 4096u

/** Network Task (T-B3-03). */
#define TILE_NET_NAME  "tile_net"
#define TILE_NET_PRIO  12u
#define TILE_NET_CORE  TILE_CORE_NET
#define TILE_NET_STACK 4096u

/** Asset Storage Task (T-B3-04). */
#define TILE_ASSET_NAME  "asset_store"
#define TILE_ASSET_PRIO  8u
#define TILE_ASSET_CORE  TILE_CORE_NET
#define TILE_ASSET_STACK 4096u

/**
 * @brief  Print the T-B5-04 boot log: the configured CPU frequency, the
 *         cycle-counter wrap time and vTaskList() (name, state, priority,
 *         stack high-water mark, task number, core). Call once from app_main
 *         after the Week 10 tasks are created.
 */
void tile_tasks_log_boot (void);

#endif /* TILE_TASKS_H */

/*** end of file ***/
