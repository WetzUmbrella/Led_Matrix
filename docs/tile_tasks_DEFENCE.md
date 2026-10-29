# tile_tasks: design defence (T-B5-04)

Owner: Jia Jun (Buddy 5). Implemented by Ahsanul (B4) for Jia Jun, task T-B5-04. Files: `panel_sync/main/tile_tasks.h` (task table), `panel_sync/main/tile_tasks.c` (boot log), `panel_sync/sdkconfig.defaults` (the six options of the row). Coding standard: Barr-C:2018, `docs/coding_standard.md` (deviation D-2 only, for FreeRTOS names and the `vTaskList()` char buffer).

**Names.** The plan names one file, "`panel_sync/main/tile_tasks.h` (name, priority, core, stack per task)", and no functions. Each task row is four macros with the `TILE_` prefix (Barr-C 6.1.i applies the module name to public names; macros are upper case, 6.1.h): Display Engine Task = `TILE_DISPLAY_NAME/_PRIO/_CORE/_STACK`; Orientation Task = `TILE_ORIENT_*`; Time Sync Task = `TILE_TIMESYNC_*`; Playback Control Task = `TILE_PLAYBACK_*`; Network Task = `TILE_NET_*`; Asset Storage Task = `TILE_ASSET_*`. The cores are `TILE_CORE_NET` (0) and `TILE_CORE_DISPLAY` (1). The boot log that the acceptance needs is `tile_tasks_log_boot()`, a name this module adds because the plan gives none. The recorded CPU frequency is `TILE_CPU_FREQ_MHZ`.

Each entry gives the design choice, the PostTri row it comes from (inventory ID and the row's wording, quoted from `Plan/commitment_inventory.md` or the T-B5-04 plan row), and the question an examiner is likely to ask. Numbers not in the plan are labelled (plan choice).

## 1. Numeric priorities and cores

- Source: W6D-4a, "Priorities **(proposed)**: Display Engine 20 and Orientation 5 on core 1; Time Sync 16, Playback Control 15, Network 12, Asset Storage 8 on core 0."; VA-5.1, "Display Engine priority 20 on core 1 **(proposed)**; WiFi 23 and esp_timer 22 stay on core 0; lwIP tcpip (18) pinned to core 0 in menuconfig."; lecturer feedback (`SE15 (1).txt`), "pin the Display Engine to core 1 and state priorities relative to ESP-IDF system tasks".
- Choice: the six rows of `tile_tasks.h` copy those numbers. Every tile task is created with `xTaskCreatePinnedToCore()` from the four macros of its row (`display_engine.c`, `tile_net.c`, `panel_sync.c` for the Time Sync Task). Core 1 carries only the Display Engine and Orientation, so the 60 Hz path never competes with WiFi (23), esp_timer (22) or tcpip (18) on core 0. All values stay (proposed) until the boot log is read on a tile.
- Examiner: "Time Sync is at 16 and tcpip at 18 on the same core. What happens to the Time Sync Task while a burst of four beacons is being received, and does it matter for the skew?"

## 2. lwIP tcpip task pinned to core 0

- Source: W6D-4b, "lwIP tcpip task pinned to core 0 in menuconfig."; SRM-4M, "Display pinned to core 1; tcpip pinned to core 0; timestamp at the earliest RX hook."
- Choice: `CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0=y` in `sdkconfig.defaults`. The boot log's core column shows `tiT` (the tcpip task's name in ESP-IDF) with core 0.
- Examiner: "If tcpip were left unpinned, what could it do to the Display Engine on core 1, and how would you see it in the missed-tick counter?"

## 3. IRAM-safe tick and RMT interrupts

- Source: VA-5.2, "Tick from a gptimer alarm ISR registered from the core-1 task, `CONFIG_GPTIMER_ISR_IRAM_SAFE`, alarm callback in IRAM (not an esp_timer callback)."; SRM-3M, "tick and RMT ISRs IRAM-safe".
- Choice: `CONFIG_GPTIMER_ISR_IRAM_SAFE=y` and `CONFIG_RMT_ISR_IRAM_SAFE=y`. A flash write or erase disables the cache on both cores; an IRAM-safe interrupt still runs then, so a tick is not delayed by the stall.
- Examiner: "The shipped firmware never writes flash while PLAYING. Why make the interrupts IRAM-safe anyway, and what does it cost?"

## 4. Power management off

- Source: VC-8a, "confirm the CPU frequency in sdkconfig and keep power management off during the run"; T-B5-04, "keeps `CONFIG_PM_ENABLE` unset".
- Choice: `# CONFIG_PM_ENABLE is not set` in `sdkconfig.defaults`, so the CPU clock never scales and the cycle counter and gptimer keep one rate.
- Examiner: "With power management on, which of your timing measurements would no longer be comparable between runs?"

## 5. Trace options for `vTaskList()` with a core column

- Source: T-B5-04, "`CONFIG_FREERTOS_USE_TRACE_FACILITY=y`, `CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS=y` and `CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID=y` (needed for the core column of `vTaskList()`)"; Round 1 finding 15.
- Choice: all three set. `tile_tasks_log_boot()` sizes the `vTaskList()` buffer as (task count + 4) x 48 B + 1 B and frees it afterwards. 48 B per line: the ESP-IDF v5.3 line with the core column is at most 42 B (name padded to 15, five tab-separated numbers, CR LF), plus 6 B spare (plan choice). 4 spare lines (plan choice): `vTaskList()` takes no length, so the margin covers a task created between counting and listing; the Week 10 build creates none at that point.
- Examiner: "`vTaskList()` has no length parameter. What stops it writing past your buffer, and why is a heap buffer acceptable here when the plan avoids allocation elsewhere?"

## 6. CPU frequency recorded, not changed

- Source: VC-8a, "confirm the CPU frequency in sdkconfig and keep power management off during the run; cycle deltas use unsigned 32-bit subtraction (counter wraps every 26.8 s at the 160 MHz default, 17.9 s at 240 MHz)."; Round 0 ruling SC-5, "Neither is changed. Task records the configured CPU frequency from the built sdkconfig and states the counter wrap time for that value."
- Choice: the built `sdkconfig` holds `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ=160` (checked on 29 Oct 2026 from a fresh build). `TILE_CPU_FREQ_MHZ` records 160, and `tile_tasks.c` stops the build with `#error` if the two differ, so the record cannot go stale silently. The boot log prints `cpu_mhz=160 cycle_wrap_s=26.8`, computed in integers as round(2^32 x 10 / (160 x 10^6)) = 268 tenths.
- Examiner: "Your preparation time is a 32-bit cycle-count difference. Up to what duration is that difference correct, and why is one frame always inside it?"

## 7. Stack size 4096 B per task

- Source: T-B5-04 row, "(name, priority, core, stack per task)"; no PostTri value.
- Choice: 4096 B for every task (plan choice), the value the prototype's render and sync tasks used. Unmeasured: the stack column of `vTaskList()` is the high-water mark (the least free stack seen), so the boot log and a log taken after 10 min of running give the data to set each size.
- Examiner: "What is the high-water mark of the Display Engine after 10 minutes, and how much margin would you keep above it?"

## PROVISIONAL for Jia Jun to confirm

1. Every priority and core in the table: (proposed, W6D-4a) until the boot log on a tile shows them.
2. Stack 4096 B for every task, unmeasured.
3. `vTaskList()` buffer: 48 B per line and 4 spare lines (plan choice, section 5).
4. The boot log is printed from `app_main` right after the three Week 10 tasks are created; Orientation, Playback Control and Asset Storage are checked the same way in T-B5-05, T-B4-04 and T-B3-04.

## Questions for Jia Jun before Tribunal 2

Answer each in your own words, without reading the code.

1. Why is the Display Engine at priority 20 on core 1 rather than above WiFi (23) on core 0, and what would a priority above WiFi on core 0 break?
2. A flash erase stalls the cache for tens of milliseconds. Which parts of the 60 Hz path still run during it with the options in this table, and which do not?
3. The cycle counter wraps every 26.8 s at 160 MHz. Explain why an unsigned 32-bit subtraction still gives the right preparation time across a wrap, and when it would not.
