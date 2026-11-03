/**
 * @file    tile_stress.c
 * @brief   Stress test builds T-X-02 and T-X-05. See tile_stress.h.
 *
 * Owners: Cheston (T-X-02) and Ahsanul (T-X-05). Implemented by Ahsanul (B4)
 * for them.
 *
 * DEVIATION D-1 (module-wide): C11 stdatomic.h for the finished counter.
 * DEVIATION D-2 (module-wide): FreeRTOS names and types (TaskHandle_t,
 * BaseType_t, pdPASS).
 */
#include "tile_stress.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "fault_word.h"
#include "tile_tasks.h"
#include "timesync.h"

/** Stack of each stress task (plan choice). */
#define TS_STACK 4096u

/** Setters and readers (one per core). */
#define TS_PAIR 2u

#if defined(FAULT_STRESS) || defined(TS_STRESS)
/** One stress task's parameters and results (written only by that task). */
typedef struct
{
    char const * p_name;      /**< "A" or "B", or the bit's name. */
    uint32_t     bit;         /**< FAULT_STRESS: the setter's event bit. */
    uint32_t     done;        /**< Sets or reads made. */
    uint32_t     torn;        /**< TS_STRESS: torn snapshots. */
    int64_t      max_wait_us; /**< FAULT_STRESS: longest wait for a clear. */
} ts_job_t;

static ts_job_t         g_jobs[TS_PAIR];
static _Atomic uint32_t g_running = 0u;

static void ts_pause (uint32_t pass);
#endif

#ifdef FAULT_STRESS
/** Snapshots holding each setter's bit; written only by the reporter. */
static uint32_t g_seen[TS_PAIR];

static void ts_setter (void * p_arg);
static void ts_reporter (void * p_arg);
#endif

#ifdef TS_STRESS
static void ts_writer (void * p_arg);
static void ts_reader (void * p_arg);
#endif

/**
 * @brief  Start the stress tasks of the selected build.
 * @return true when a stress build is running.
 */
bool tile_stress_start (void)
{
    bool b_started = false;

#ifdef FAULT_STRESS
    g_jobs[0] = (ts_job_t){.p_name = "CRC_FAIL", .bit = FAULT_BIT_CRC_FAIL};
    g_jobs[1] = (ts_job_t){.p_name = "MISSED_DEADLINE",
                           .bit    = FAULT_BIT_MISSED_DEADLINE};
    atomic_store_explicit(&g_running, TS_PAIR, memory_order_release);
    b_started =
        (pdPASS == xTaskCreatePinnedToCore(ts_reporter, "f_reporter", TS_STACK,
                                           NULL, TILE_NET_PRIO, NULL,
                                           TILE_CORE_NET)) &&
        (pdPASS == xTaskCreatePinnedToCore(ts_setter, "f_set0", TS_STACK,
                                           &g_jobs[0], TILE_NET_PRIO, NULL,
                                           TILE_CORE_NET)) &&
        (pdPASS == xTaskCreatePinnedToCore(ts_setter, "f_set1", TS_STACK,
                                           &g_jobs[1], TILE_ORIENT_PRIO, NULL,
                                           TILE_CORE_DISPLAY));
#elif defined(TS_STRESS)
    g_jobs[0] = (ts_job_t){.p_name = "A"};
    g_jobs[1] = (ts_job_t){.p_name = "B"};
    atomic_store_explicit(&g_running, TS_PAIR, memory_order_release);
    b_started =
        (pdPASS == xTaskCreatePinnedToCore(ts_writer, "t_writer", TS_STACK,
                                           NULL, TILE_PLAYBACK_PRIO, NULL,
                                           TILE_CORE_NET)) &&
        (pdPASS == xTaskCreatePinnedToCore(ts_reader, "t_readA", TS_STACK,
                                           &g_jobs[0], TILE_PLAYBACK_PRIO, NULL,
                                           TILE_PLAYBACK_CORE)) &&
        (pdPASS == xTaskCreatePinnedToCore(ts_reader, "t_readB", TS_STACK,
                                           &g_jobs[1], TILE_DISPLAY_PRIO, NULL,
                                           TILE_DISPLAY_CORE));
#endif

    return b_started;
}

#if defined(FAULT_STRESS) || defined(TS_STRESS)
/**
 * @brief  Sleep one tick every TILE_STRESS_YIELD_EVERY passes (idle tasks
 *         and the task watchdog); otherwise yield.
 * @param  pass  Pass number.
 */
static void ts_pause (uint32_t pass)
{
    if (0u == (pass % TILE_STRESS_YIELD_EVERY))
    {
        vTaskDelay(1);
    }
    else
    {
        taskYIELD();
    }
}
#endif

#ifdef FAULT_STRESS
/**
 * @brief  Setter: raise its bit TILE_STRESS_SETS times, each time waiting
 *         until the reporter has cleared it.
 * @param  p_arg  Its ts_job_t.
 */
static void ts_setter (void * p_arg)
{
    ts_job_t * const p_job = (ts_job_t *)p_arg; /* Set in start(). */

    for (uint32_t set = 0u; set < TILE_STRESS_SETS; set++)
    {
        int64_t const start = esp_timer_get_time();
        uint32_t      pass  = 0u;

        while (0u != (fault_word_snapshot() & p_job->bit))
        {
            pass++;
            ts_pause(pass);
        }

        int64_t const wait = esp_timer_get_time() - start;

        if (wait > p_job->max_wait_us)
        {
            p_job->max_wait_us = wait;
        }

        fault_word_set(p_job->bit);
        p_job->done++;
        ts_pause(set + 1u);
    }

    /* The previous count is not needed. */
    (void)atomic_fetch_sub_explicit(&g_running, 1u, memory_order_acq_rel);
    vTaskDelete(NULL);
}

/**
 * @brief  Reporter: snapshot, count, clear on the simulated ACK; report
 *         when both setters are done.
 * @param  p_arg  Unused.
 */
static void ts_reporter (void * p_arg)
{
    uint32_t pass = 0u;

    (void)p_arg; /* No parameter. */

    while (0u != atomic_load_explicit(&g_running, memory_order_acquire))
    {
        uint32_t const snap = fault_word_snapshot();

        for (uint32_t idx = 0u; idx < TS_PAIR; idx++)
        {
            g_seen[idx] += (0u != (snap & g_jobs[idx].bit)) ? 1u : 0u;
        }

        fault_word_clear_events(snap);
        pass++;
        ts_pause(pass);
    }

    /* The last sets may still be in the word: report them once. */
    uint32_t const last = fault_word_snapshot();

    for (uint32_t idx = 0u; idx < TS_PAIR; idx++)
    {
        g_seen[idx] += (0u != (last & g_jobs[idx].bit)) ? 1u : 0u;
        printf("FAULT_STRESS bit=%s sets=%" PRIu32 " seen=%" PRIu32
               " lost=%" PRIu32 " max_wait_us=%" PRId64 "\n",
               g_jobs[idx].p_name, g_jobs[idx].done, g_seen[idx],
               g_jobs[idx].done - g_seen[idx], g_jobs[idx].max_wait_us);
    }

    vTaskDelete(NULL);
}
#endif

#ifdef TS_STRESS
/**
 * @brief  Writer: publish snapshots whose fields all come from one counter.
 * @param  p_arg  Unused.
 */
static void ts_writer (void * p_arg)
{
    uint32_t count = 0u;

    (void)p_arg; /* No parameter. */

    while (0u != atomic_load_explicit(&g_running, memory_order_acquire))
    {
        count++;

        timesync_snapshot_t const snap = {
            .offset_us = (int64_t)count,               /* Widened, unchanged. */
            .drift_ppb = (int32_t)(count & INT32_MAX), /* Masked: fits. */
            .ref_us    = (int64_t)count,               /* Widened, unchanged. */
            .flags     = TIMESYNC_SNAP_VALID,
        };

        timesync_publish(&snap);
        ts_pause(count);
    }

    vTaskDelete(NULL);
}

/**
 * @brief  Reader: TILE_STRESS_READS reads, each checked for a mix of writes.
 * @param  p_arg  Its ts_job_t.
 */
static void ts_reader (void * p_arg)
{
    ts_job_t * const  p_job  = (ts_job_t *)p_arg; /* Set in start(). */
    timesync_reader_t reader = {0};

    for (uint32_t read = 0u; read < TILE_STRESS_READS; read++)
    {
        if (timesync_read_snapshot(&reader))
        {
            timesync_snapshot_t const * const p_snap = &reader.snap;

            bool const b_whole = (p_snap->offset_us == p_snap->ref_us) &&
                                 (((int64_t)p_snap->drift_ppb) == /* Widened. */
                                  (p_snap->offset_us & INT32_MAX));

            p_job->torn += b_whole ? 0u : 1u;
        }

        p_job->done++;
        ts_pause(read + 1u);
    }

    printf("TS_STRESS reader=%s reads=%" PRIu32 " torn=%" PRIu32
           " stale=%" PRIu32 "\n",
           p_job->p_name, p_job->done, p_job->torn, reader.stale_count);
    /* The previous count is not needed. */
    (void)atomic_fetch_sub_explicit(&g_running, 1u, memory_order_acq_rel);
    vTaskDelete(NULL);
}
#endif

/*** end of file ***/
