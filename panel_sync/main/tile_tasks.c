/**
 * @file    tile_tasks.c
 * @brief   Tile task table (T-B5-04): the boot log that the T-B5-04
 *          acceptance reads (CPU frequency, cycle-counter wrap time and
 *          vTaskList() with the core column).
 *
 * Implemented by Ahsanul (B4) for Jia Jun, task T-B5-04.
 * Implemented and not yet implemented: see tile_tasks.h.
 * PROVISIONAL choices: see tile_tasks.h.
 *
 * DEVIATION D-2 (module-wide): FreeRTOS names and types (UBaseType_t,
 * pvPortMalloc(), vPortFree(), vTaskList() and its char buffer).
 */
#include "tile_tasks.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if (TILE_CPU_FREQ_MHZ != CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)
#error "CPU frequency differs from TILE_CPU_FREQ_MHZ: update VC-8a record"
#endif

/** Hertz per megahertz. */
#define HZ_PER_MHZ UINT64_C(1000000)

/** Cycle-counter range: the counter is 32 bits wide, so it wraps at 2^32. */
#define CYCLE_RANGE (UINT64_C(1) << 32u)

/** Tenths of a second per second, for the one-decimal wrap time. */
#define TENTHS_PER_SECOND 10u

/** Bytes per vTaskList() line. The ESP-IDF v5.3 format with the core
 *  column (components/freertos/FreeRTOS-Kernel/tasks.c, vTaskList(),
 *  configTASKLIST_INCLUDE_COREID) is the name padded to 15 characters
 *  (configMAX_TASK_NAME_LEN 16 minus 1), then tab, state (1), tab, priority
 *  (at most 2 digits, 25 levels), tab, stack high-water mark (uint32_t,
 *  configSTACK_DEPTH_TYPE in FreeRTOSConfig.h, at most 10 digits), tab, task
 *  number (32-bit, at most 10 digits), tab, core (at most 2: -1, 0 or 1),
 *  CR LF: 15 + 1 + 1 + 1 + 2 + 1 + 10 + 1 + 10 + 1 + 2 + 2 = 47 B. 64 B
 *  leaves 17 B spare per line (plan choice). */
#define LIST_LINE_BYTES 64u

/** Extra lines for tasks created between the count and the list (plan
 *  choice: the Week 10 build creates none after app_main's three). */
#define LIST_SPARE_TASKS 4u

static uint32_t wrap_tenths (uint64_t freq_mhz);

/**
 * @brief  Print the T-B5-04 boot log. vTaskList() takes no buffer length, so
 *         the buffer is sized from the live task count and freed afterwards;
 *         on an allocation failure only the frequency lines are printed.
 */
void tile_tasks_log_boot (void)
{
    uint32_t const tenths = wrap_tenths(TILE_CPU_FREQ_MHZ);

    printf("TILE_TASKS cpu_mhz=%" PRIu32 " cycle_wrap_s=%" PRIu32 ".%" PRIu32
           "\n",
           (uint32_t)TILE_CPU_FREQ_MHZ, /* 160 fits uint32_t. */
           tenths / TENTHS_PER_SECOND, tenths % TENTHS_PER_SECOND);

    /* The task count is small (under 30 on a tile), so it fits uint32_t. */
    uint32_t const tasks  = (uint32_t)uxTaskGetNumberOfTasks();
    uint32_t const bytes  = ((tasks + LIST_SPARE_TASKS) * LIST_LINE_BYTES) + 1u;
    char * const   p_list = pvPortMalloc(bytes);

    if (NULL == p_list)
    {
        printf("TILE_TASKS vTaskList skipped: no %" PRIu32 " B buffer\n",
               bytes);
    }
    else
    {
        vTaskList(p_list);
        printf("TILE_TASKS vTaskList (name state prio stack num core), "
               "%" PRIu32 " tasks\n%s",
               tasks, p_list);
        vPortFree(p_list);
    }
}

/**
 * @brief  Cycle-counter wrap time in tenths of a second, rounded to the
 *         nearest tenth, in integers only: 2^32 x 10 / (f x 10^6).
 * @param  freq_mhz  CPU frequency, MHz (non-zero).
 * @return Wrap time, tenths of a second (268 at 160 MHz, 179 at 240 MHz).
 */
static uint32_t wrap_tenths (uint64_t freq_mhz)
{
    uint64_t const freq_hz = freq_mhz * HZ_PER_MHZ;

    /* At 1 MHz or more the quotient is at most 42,950, so it fits uint32_t. */
    return (uint32_t)(((CYCLE_RANGE * TENTHS_PER_SECOND) + (freq_hz / 2u)) /
                      freq_hz);
}

/*** end of file ***/
