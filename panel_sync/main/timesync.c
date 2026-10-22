// Time Sync Task (T-B4-02): FreeRTOS wrapper around timesync_core.c.
// Drains the Time Sync Queue into the filter and runs one slew step per wake.
#include "timesync.h"

#include <stdatomic.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"

// (plan choice) 100 ms: one beacon period at 10 Hz, so samples are still
// drained at the beacon rate when no tick notification arrives.
#define TS_WAIT_MS 100

static _Atomic(TaskHandle_t) s_task = NULL;

int64_t ts_platform_now_us(void)
{
    return esp_timer_get_time();
}

void timesync_notify_tick(void)
{
    TaskHandle_t t = atomic_load_explicit(&s_task, memory_order_acquire);
    if (t != NULL) xTaskNotifyGive(t);
}

void timesync_task(void *q_timesync)
{
    QueueHandle_t q = (QueueHandle_t)q_timesync;
    ts_core_init();
    atomic_store_explicit(&s_task, xTaskGetCurrentTaskHandle(), memory_order_release);

    for (;;) {
        // Wakes once per Display Engine tick, or after TS_WAIT_MS without one.
        // Several pending ticks still give one step, so no published update
        // moves the offset by more than 100 us.
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TS_WAIT_MS));

        ts_pair_t p;
        while (xQueueReceive(q, &p, 0) == pdTRUE)
            ts_add_sample(p.t_p, p.t_l);

        ts_tick(esp_timer_get_time());
    }
}
