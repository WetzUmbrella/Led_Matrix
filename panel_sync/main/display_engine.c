/**
 * @file    display_engine.c
 * @brief   Display Engine Task (T-B5-01, T-B5-02, T-B5-03): gptimer one-shot
 *          tick, test pin, double-buffered frame path and RMT start on the
 *          tick.
 *
 * Owner: Jia Jun (Buddy 5). T-B5-01 minimal version by Ahsanul (B4) for
 * T-B4-03; T-B5-02 and T-B5-03 implemented by Ahsanul (B4) for Jia Jun,
 * written 29 Oct 2026 in Jia Jun's session. What is implemented and the
 * PROVISIONAL choices: see display_engine.h.
 *
 * Contexts: de_task (core 1, priority 20) owns both pixel buffers, the RMT
 * channel, the counters and the histogram; the gptimer ISR only writes the
 * test pin and notifies de_task. The buffers are never referenced outside
 * this file (T-B5-03 code review check).
 *
 * DEVIATION D-2 (module-wide): ESP-IDF and FreeRTOS names, types and macros
 * (gptimer_handle_t, rmt_channel_handle_t, rmt_encoder_handle_t,
 * TaskHandle_t, BaseType_t, portMUX_TYPE, pdTRUE, IRAM_ATTR, ESP_OK).
 */
#include "display_engine.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_partition.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"

#include "asset_store.h"
#include "display_frame.h"
#include "fault_word.h"
#include "orientation.h"
#include "playback_control.h"
#include "proto.h"
#include "tile_net.h"
#include "tile_tasks.h"
#include "timesync.h"

/** gptimer resolution: 1 MHz, so one count is 1 us. */
#define DE_TIMER_HZ 1000000u

/** Tick grid origin in Pico time: ticks fall on multiples of 10^6 / 60 us
 *  (T-B4-03 rig build); the playback origin (START_AT) picks the first
 *  frame's tick on this grid. */
#define DE_ORIGIN_US INT64_C(0)

/** Least lead for arming a tick, microseconds (plan choice: room for the
 *  arming calls themselves); a deadline closer than this is a missed tick. */
#define DE_MIN_LEAD_US INT64_C(200)

/** Marker every 64 ticks and the two pulse widths (RIG.md section 4b). */
#define DE_MARKER_PERIOD 64u
#define DE_PULSE_US      INT64_C(1000)
#define DE_MARKER_US     INT64_C(4000)

/** WS2812B data line (PostTri RMT row) and RMT resolution, 10 MHz. */
#define DE_LED_GPIO 14
#define DE_RMT_HZ   10000000u

/** WS2812B bit timing in 0.1 us RMT ticks: 0 = 0.3 us high, 0.9 us low;
 *  1 = 0.9 us high, 0.3 us low (WS2812B datasheet, as led_strip used). */
#define DE_T_SHORT 3u
#define DE_T_LONG  9u

/** RMT memory per channel without DMA, symbols (PROVISIONAL, OD-16). */
#define DE_RMT_SYMBOLS 64u

/** Poll period while waiting for SYNC_READY before the first tick (plan
 *  choice: 20 ms, about one beacon interval / 5). */
#define DE_WAIT_SYNC_MS 20u

/** Counter and histogram line every 600 ticks (10 s, plan choice). */
#define DE_LOG_EVERY 600u

/** Lateness histogram: 10 bins of 100 us, then one overflow bin (VC-8). */
#define DE_HIST_BINS   11u
#define DE_HIST_BIN_US INT64_C(100)

/** T-X-03 DE_PROFILE: preparation-time histogram, 16 bins of 250 us (the
 *  last also holds longer), and the GPIO toggled around preparation
 *  (PROVISIONAL, owner Jia Jun: GPIO5, next to the test pin). */
#define DE_PROF_BINS   16u
#define DE_PROF_BIN_US 250u
#define DE_PROF_PIN    5u

/** T-B5-07 (b) DE_TEST_SLOW_PREP: busy-wait added to every preparation,
 *  bringing it close to the about 14 ms slack (plan choice: 13 ms). */
#define DE_SLOW_PREP_US 13000u

/** T-B5-07 (c) DE_TEST_FLASH_ERASE: one 4 KB sector at the end of the
 *  frames partition (the asset uses the first 64 KB), erased every 1 s by
 *  a core 0 task (test build only; the shipped firmware never writes flash
 *  while PLAYING, T-B3-04). */
#define DE_ERASE_OFFSET    0x1FF000u
#define DE_ERASE_BYTES     4096u
#define DE_ERASE_PERIOD_MS 1000u
#define DE_ERASE_PRIO      TILE_ASSET_PRIO

/** Number of pixel buffers (front and back, W6D-3b). */
#define DE_BUFFERS 2u

/** RMT and LED driver state, owned by de_task. */
typedef struct
{
    rmt_channel_handle_t p_chan;
    rmt_encoder_handle_t p_enc;
} de_rmt_t;

static TaskHandle_t g_p_de_task = NULL;

/** Level the next alarm writes to the test pin; set by de_task before
 *  arming, read by the ISR. */
static volatile bool g_b_pin_high_next = false;

/* Written and read only by de_task. */
static uint8_t      g_buf[DE_BUFFERS][DISPLAY_FRAME_WIRE_BYTES];
static uint8_t *    g_p_front   = g_buf[0];
static uint8_t *    g_p_back    = g_buf[1];
static portMUX_TYPE g_swap_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t     g_ticks     = 0u;
static uint32_t     g_missed    = 0u;
static uint32_t     g_rmt_busy  = 0u;
static uint32_t     g_hist[DE_HIST_BINS];
static int64_t      g_prep_max_us = 0;
static uint32_t     g_state       = PROTO_DISPLAY_COUNT; /* None reported. */
static uint8_t      g_orient      = PROTO_ORIENT_0;
static bool         g_b_rmt_fault = false;

static void     de_task (void * p_arg);
static void     de_report (proto_display_state_t state);
static uint64_t de_run_tick (gptimer_handle_t p_timer, de_rmt_t const * p_rmt,
                             timesync_reader_t * p_reader, uint64_t tick);
static void     de_present (de_rmt_t const * p_rmt, int64_t deadline);
static void     de_swap (void);
static void     de_prepare_frame (uint64_t tick);
static void     de_log (void);
static void     de_test_after_prepare (uint32_t frame, bool b_frame);
#ifdef DE_PROFILE
static uint32_t g_prof[DE_PROF_BINS];
#endif
#ifdef DE_DUMP_FRAMES
static uint32_t g_dump_next = 0u; /* Next frame to dump (T-X-01). */
#endif
#ifdef DE_TEST_FLASH_ERASE
static void de_erase_task (void * p_arg);
#endif
static bool             de_tick_isr (gptimer_handle_t                   p_timer,
                                     gptimer_alarm_event_data_t const * p_event,
                                     void *                             p_ctx);
static void             de_arm_next (gptimer_handle_t p_timer, int64_t delay_us,
                                     bool b_pin_high);
static void             de_wait_alarm (void);
static gptimer_handle_t de_timer_create (void);
static bool             de_start_rmt (de_rmt_t * p_rmt);

/**
 * @brief  Create the Display Engine Task.
 * @return true when the task was created.
 */
bool display_engine_start (void)
{
    return (pdPASS == xTaskCreatePinnedToCore(
                          de_task, TILE_DISPLAY_NAME, TILE_DISPLAY_STACK, NULL,
                          TILE_DISPLAY_PRIO, NULL, TILE_DISPLAY_CORE));
}

/**
 * @brief  Display Engine Task, core 1: create the test pin, the gptimer and
 *         the RMT channel on core 1, wait for SYNC_READY, prepare the first
 *         frame, then one de_run_tick() per tick.
 * @param  p_arg  Unused.
 */
static void de_task (void * p_arg)
{
    (void)p_arg; /* The task takes no parameter. */

    g_p_de_task = xTaskGetCurrentTaskHandle();

    gpio_config_t const pin_cfg = {
        .pin_bit_mask = (UINT64_C(1) << DE_TEST_PIN),
        .mode         = GPIO_MODE_OUTPUT,
    };

    (void)gpio_config(&pin_cfg);           /* Fixed, valid configuration. */
    (void)gpio_set_level(DE_TEST_PIN, 0u); /* Valid pin and level. */
#ifdef DE_PROFILE
    gpio_config_t const prof_cfg = {
        .pin_bit_mask = (UINT64_C(1) << DE_PROF_PIN),
        .mode         = GPIO_MODE_OUTPUT,
    };

    (void)gpio_config(&prof_cfg); /* Fixed, valid configuration. */
#endif
#ifdef DE_TEST_FLASH_ERASE
    /* Core 0, so the erase competes with the network side as an upload
     * would; the result only matters for the test build. */
    (void)xTaskCreatePinnedToCore(de_erase_task, "de_erase", 4096u, NULL,
                                  DE_ERASE_PRIO, NULL, TILE_CORE_NET);
#endif

    de_report(PROTO_DISPLAY_INITIALISING);
    display_frame_status(DISPLAY_FRAME_STATUS_DARK, g_p_front);
    display_frame_status(DISPLAY_FRAME_STATUS_DARK, g_p_back);

    de_rmt_t               rmt     = {NULL, NULL};
    gptimer_handle_t const p_timer = de_timer_create();
    bool const             b_rmt   = de_start_rmt(&rmt);
    timesync_reader_t      reader  = {0};

    if ((NULL == p_timer) || (!b_rmt))
    {
        /* No tick source or no LED output: DISPLAY FAULT (condition bit,
         * owner display_engine). */
        fault_word_set(FAULT_BIT_DISPLAY);
        de_report(PROTO_DISPLAY_FAULT);
    }
    else
    {
        de_report(PROTO_DISPLAY_IDLE);
    }

    /* Start-up defect fix (HANDOVER step 11): the first tick is taken
     * from corrected Pico time only once the OD-10 rule holds, so a tile
     * that booted before the Pico does not arm a deadline seconds away. */
    while (!timesync_sync_ready())
    {
        vTaskDelay(pdMS_TO_TICKS(DE_WAIT_SYNC_MS));
    }

    uint64_t tick =
        timesync_next_tick(DE_ORIGIN_US, timesync_now_corrected(&reader));

    printf("display_engine: test pin GPIO%u, first tick %" PRIu64 "\n",
           DE_TEST_PIN, tick);
    de_prepare_frame(tick);

    for (;;)
    {
        if ((NULL == p_timer) || (!b_rmt))
        {
            /* No tick source: stay idle (a FreeRTOS task never returns). */
            vTaskDelay(portMAX_DELAY);
        }
        else
        {
            tick = de_run_tick(p_timer, &rmt, &reader, tick);
        }
    }
}

/**
 * @brief  Report a display state change to the Network Task for TELEMETRY
 *         (T-B3-06), with the orientation in use. Never blocks; only a
 *         change is posted.
 * @param  state  New display state.
 */
static void de_report (proto_display_state_t state)
{
    /* A display state is 0 to 6, so it fits uint32_t. */
    if ((uint32_t)state != g_state)
    {
        g_state = (uint32_t)state; /* Same range. */
        /* A full queue drops its oldest item and counts it (tile_net). The
         * state is 0 to 6, so it fits uint8_t. */
        (void)tile_net_post_status(g_orient, (uint8_t)state);
    }
}

/**
 * @brief  One tick (T-B5-03): arm the rising edge for tick n; on the alarm
 *         present the frame prepared for n, arm the falling edge, notify
 *         Time Sync, prepare n + 1 into the back buffer, wait for the
 *         falling edge. A deadline already too close counts as missed and
 *         the next tick after now is returned instead.
 * @param  p_timer   The tick timer.
 * @param  p_rmt     The LED output.
 * @param  p_reader  This task's timesync reader.
 * @param  tick      Tick number n to show.
 * @return The next tick to show.
 */
static uint64_t de_run_tick (gptimer_handle_t p_timer, de_rmt_t const * p_rmt,
                             timesync_reader_t * p_reader, uint64_t tick)
{
    uint64_t next = tick + 1u;

    int64_t const deadline =
        timesync_deadline_local(p_reader, DE_ORIGIN_US, tick);
    int64_t const lead = deadline - esp_timer_get_time();

    if (lead < DE_MIN_LEAD_US)
    {
        g_missed++;
        fault_word_set(FAULT_BIT_MISSED_DEADLINE);
        next =
            timesync_next_tick(DE_ORIGIN_US, timesync_now_corrected(p_reader));
        de_prepare_frame(next);
    }
    else
    {
        int64_t const width =
            (0u == (tick % DE_MARKER_PERIOD)) ? DE_MARKER_US : DE_PULSE_US;

        de_arm_next(p_timer, lead, true);
        de_wait_alarm(); /* Rising edge: tick n. */
        de_present(p_rmt, deadline);
        de_arm_next(p_timer, width, false);
        timesync_notify_tick();

        int64_t const prep_start = esp_timer_get_time();
#ifdef DE_PROFILE
        uint32_t const cycles_start = esp_cpu_get_cycle_count();

        gpio_ll_set_level(&GPIO, DE_PROF_PIN, 1u);
#endif

        de_prepare_frame(next);

#ifdef DE_TEST_SLOW_PREP
        esp_rom_delay_us(DE_SLOW_PREP_US); /* T-B5-07 (b). */
#endif
#ifdef DE_PROFILE
        gpio_ll_set_level(&GPIO, DE_PROF_PIN, 0u);

        /* Unsigned 32-bit delta, taken within one frame: correct across one
         * wrap (26.8 s at 160 MHz, VC-8a). */
        uint32_t const cycles = esp_cpu_get_cycle_count() - cycles_start;
        uint32_t const prof_us =
            cycles / (uint32_t)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ; /* Fits. */
        uint32_t const prof_bin = (prof_us / DE_PROF_BIN_US < DE_PROF_BINS)
                                      ? (prof_us / DE_PROF_BIN_US)
                                      : (DE_PROF_BINS - 1u);

        g_prof[prof_bin]++;
#endif

        int64_t const prep_us = esp_timer_get_time() - prep_start;

        if (prep_us > g_prep_max_us)
        {
            g_prep_max_us = prep_us;
        }

        de_wait_alarm(); /* Falling edge. */
        g_ticks++;

        if (0u == (g_ticks % DE_LOG_EVERY))
        {
            de_log();
        }
    }

    return next;
}

/**
 * @brief  On the tick: if RMT is still sending, count a missed deadline and
 *         raise MISSED_DEADLINE; otherwise swap the buffers and start RMT on
 *         the front buffer. The start lateness goes into the histogram.
 * @param  p_rmt     The LED output.
 * @param  deadline  Local time of this tick.
 */
static void de_present (de_rmt_t const * p_rmt, int64_t deadline)
{
    if (ESP_OK != rmt_tx_wait_all_done(p_rmt->p_chan, 0))
    {
        g_rmt_busy++;
        g_missed++;
        fault_word_set(FAULT_BIT_MISSED_DEADLINE);
    }
    else
    {
        rmt_transmit_config_t const tx_cfg = {.loop_count = 0};

        de_swap();

        int64_t const   late = esp_timer_get_time() - deadline;
        esp_err_t const err =
            rmt_transmit(p_rmt->p_chan, p_rmt->p_enc, g_p_front,
                         DISPLAY_FRAME_WIRE_BYTES, &tx_cfg);

        /* late >= 0 after the alarm; the bin is below DE_HIST_BINS. */
        uint32_t const bin =
            (late >= (DE_HIST_BIN_US * (int64_t)(DE_HIST_BINS - 1u)))
                ? (DE_HIST_BINS - 1u)
                : (uint32_t)((late < 0) ? 0 : (late / DE_HIST_BIN_US));

        g_hist[bin]++;

        if ((ESP_OK != err) && (!g_b_rmt_fault))
        {
            g_b_rmt_fault = true;
            fault_word_set(FAULT_BIT_DISPLAY);
            de_report(PROTO_DISPLAY_FAULT);
        }
        else if ((ESP_OK == err) && (g_b_rmt_fault))
        {
            g_b_rmt_fault = false;
            fault_word_clear_condition(FAULT_BIT_DISPLAY); /* Owner. */
        }
        else
        {
            /* No change in the RMT condition. */
        }
    }
}

/**
 * @brief  Swap the front and back pointers inside a critical section
 *         (T-B5-03, W6D-3b).
 */
static void de_swap (void)
{
    taskENTER_CRITICAL(&g_swap_lock);

    uint8_t * const p_old_front = g_p_front;

    g_p_front = g_p_back;
    g_p_back  = p_old_front;
    taskEXIT_CRITICAL(&g_swap_lock);
}

/**
 * @brief  Prepare tick n into the back buffer (T-B5-02): the frame of the
 *         running or held timeline from the RAM asset, unpacked with the
 *         stable orientation; otherwise a status pattern. Reports the state.
 * @param  tick  Tick number the buffer is for.
 */
static void de_prepare_frame (uint64_t tick)
{
    playback_control_timeline_t timeline;
    uint8_t const *             p_payload   = NULL;
    uint32_t                    len         = 0u;
    uint16_t                    frame_count = 0u;
    uint16_t                    frame       = 0u;
    bool                        b_frame     = false;

    g_orient = orientation_current();

    bool const b_timeline = playback_control_read_timeline(&timeline);
    bool const b_asset    = asset_store_get(&p_payload, &len, &frame_count);

    if ((b_timeline) && (b_asset) && (timeline.b_held))
    {
        frame = (timeline.hold_frame < frame_count) ? timeline.hold_frame : 0u;
        b_frame = true;
        de_report(PROTO_DISPLAY_PAUSED);
    }
    else if ((b_timeline) && (b_asset) && (timeline.b_running))
    {
        /* First tick at or after the origin: strictly after origin - 1. */
        uint64_t const first =
            timesync_next_tick(DE_ORIGIN_US, timeline.origin_us - 1);

        b_frame = display_frame_index(tick, first, frame_count, &frame);
        de_report(PROTO_DISPLAY_WAITING_FOR_NEXT_TICK);
    }
    else
    {
        de_report(PROTO_DISPLAY_IDLE);
    }

    if (b_frame)
    {
        uint32_t const        frame_bytes = len / frame_count;
        uint32_t const        offset  = frame * frame_bytes; /* Below len. */
        uint8_t const * const p_frame = &p_payload[offset];

        if (DISPLAY_FRAME_PACKED_BYTES == frame_bytes)
        {
            display_frame_unpack(p_frame, display_frame_mounted(g_orient),
                                 g_p_back);
        }
        else
        {
            /* DE_PROFILE 8-bit asset (OD-15): copied, not unpacked. */
            display_frame_copy_8bit(p_frame, display_frame_mounted(g_orient),
                                    g_p_back);
        }
    }
    else if ((b_timeline) && (b_asset) && (timeline.b_running))
    {
        /* Before the first frame's tick: keep the LEDs dark. */
        display_frame_status(DISPLAY_FRAME_STATUS_DARK, g_p_back);
    }
    else
    {
        display_frame_status(b_asset ? DISPLAY_FRAME_STATUS_DARK
                                     : DISPLAY_FRAME_STATUS_NO_ASSET,
                             g_p_back);
    }

    de_test_after_prepare(frame, b_frame);
}

/**
 * @brief  Test builds only. DE_DUMP_FRAMES (T-X-01): print the back buffer
 *         of each frame once, in frame order, as "DE_DUMP,<frame>,<hex>";
 *         a frame missed while printing is dumped on a later lap. The
 *         shipped build does nothing here.
 * @param  frame    Frame just prepared.
 * @param  b_frame  true when a frame (not a status pattern) was prepared.
 */
static void de_test_after_prepare (uint32_t frame, bool b_frame)
{
#ifdef DE_DUMP_FRAMES
    if ((b_frame) && (frame == g_dump_next))
    {
        printf("DE_DUMP,%" PRIu32 ",", frame);

        for (uint32_t idx = 0u; idx < DISPLAY_FRAME_WIRE_BYTES; idx++)
        {
            printf("%02x", g_p_back[idx]);
        }

        printf("\n");
        g_dump_next++;
    }
#else
    (void)frame;   /* Used only by the dump build. */
    (void)b_frame; /* Same. */
#endif
}

/**
 * @brief  One log line every DE_LOG_EVERY ticks: counters, the worst
 *         preparation time and the tick-to-RMT-start lateness histogram in
 *         100 us bins (VC-8: a histogram, not per-frame prints).
 */
static void de_log (void)
{
    printf("DE ticks=%" PRIu32 " missed=%" PRIu32 " rmt_busy=%" PRIu32
           " prep_max_us=%" PRId64 " orient=%" PRIu8 "\n",
           g_ticks, g_missed, g_rmt_busy, g_prep_max_us, g_orient);
    printf("DE_HIST");

    for (uint32_t bin = 0u; bin < DE_HIST_BINS; bin++)
    {
        printf(",%" PRIu32, g_hist[bin]);
    }

    printf("\n");
#ifdef DE_PROFILE
    printf("DE_PROF cpu_mhz=%u bin_us=%u", CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
           DE_PROF_BIN_US);

    for (uint32_t bin = 0u; bin < DE_PROF_BINS; bin++)
    {
        printf(",%" PRIu32, g_prof[bin]);
    }

    printf("\n");
#endif
}

#ifdef DE_TEST_FLASH_ERASE
/**
 * @brief  T-B5-07 (c) test build: erase one spare sector every second while
 *         PLAYING and log how long each erase stalled this task.
 * @param  p_arg  Unused.
 */
static void de_erase_task (void * p_arg)
{
    esp_partition_t const * const p_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "frames");
    uint32_t erases = 0u;

    (void)p_arg; /* No parameter. */

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(DE_ERASE_PERIOD_MS));

        if ((NULL != p_part) && (PLAYBACK_PLAYING == playback_control_state()))
        {
            int64_t const   start = esp_timer_get_time();
            esp_err_t const err   = esp_partition_erase_range(
                p_part, DE_ERASE_OFFSET, DE_ERASE_BYTES);

            erases++;
            printf("DE_ERASE n=%" PRIu32 " us=%" PRId64 " err=%d\n", erases,
                   esp_timer_get_time() - start, err);
        }
    }
}
#endif

/**
 * @brief  gptimer alarm callback, in IRAM so it runs during flash operations
 *         (CONFIG_GPTIMER_ISR_IRAM_SAFE): set the test pin to the level
 *         de_task chose and wake de_task.
 * @param  p_timer  Unused.
 * @param  p_event  Unused.
 * @param  p_ctx    Unused.
 * @return true when a higher-priority task was woken.
 */
static bool IRAM_ATTR de_tick_isr (gptimer_handle_t                   p_timer,
                                   gptimer_alarm_event_data_t const * p_event,
                                   void *                             p_ctx)
{
    BaseType_t b_woken = pdFALSE;

    (void)p_timer; /* One timer only. */
    (void)p_event; /* The count is not needed. */
    (void)p_ctx;   /* No context. */

    gpio_ll_set_level(&GPIO, DE_TEST_PIN, g_b_pin_high_next ? 1u : 0u);
    vTaskNotifyGiveFromISR(g_p_de_task, &b_woken);

    return (pdTRUE == b_woken);
}

/**
 * @brief  Arm the one-shot alarm delay_us from now.
 * @param  p_timer     The tick timer.
 * @param  delay_us    Delay, microseconds, > 0.
 * @param  b_pin_high  Test pin level the alarm writes.
 */
static void de_arm_next (gptimer_handle_t p_timer, int64_t delay_us,
                         bool b_pin_high)
{
    uint64_t count = 0u;

    g_b_pin_high_next = b_pin_high;
    (void)gptimer_get_raw_count(p_timer, &count); /* Valid running timer. */

    /* One count is 1 us. delay_us > 0 here (lead >= DE_MIN_LEAD_US or a
     * fixed width), so the cast to uint64_t keeps the value. */
    uint64_t const delay = (uint64_t)delay_us;

    gptimer_alarm_config_t const alarm = {
        .alarm_count = count + delay,
    };

    (void)gptimer_set_alarm_action(p_timer, &alarm); /* Valid config. */
}

/**
 * @brief  Block until the armed alarm has fired.
 */
static void de_wait_alarm (void)
{
    /* The count is not needed: one alarm is armed at a time. */
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
}

/**
 * @brief  Create, enable and start the 1 MHz tick timer with de_tick_isr()
 *         registered, from de_task on core 1, so the interrupt is allocated
 *         on core 1 (T-B5-01).
 * @return The timer, or NULL if it could not start.
 */
static gptimer_handle_t de_timer_create (void)
{
    gptimer_handle_t p_timer = NULL;

    gptimer_config_t const timer_cfg = {
        .clk_src       = GPTIMER_CLK_SRC_DEFAULT,
        .direction     = GPTIMER_COUNT_UP,
        .resolution_hz = DE_TIMER_HZ,
    };
    gptimer_event_callbacks_t const callbacks = {
        .on_alarm = de_tick_isr,
    };

    esp_err_t err = gptimer_new_timer(&timer_cfg, &p_timer);

    if (ESP_OK == err)
    {
        err = gptimer_register_event_callbacks(p_timer, &callbacks, NULL);
    }

    if (ESP_OK == err)
    {
        err = gptimer_enable(p_timer);
    }

    if (ESP_OK == err)
    {
        err = gptimer_start(p_timer);
    }

    if (ESP_OK != err)
    {
        printf("display_engine: gptimer failed (%d)\n", err);
        p_timer = NULL;
    }

    return p_timer;
}

/**
 * @brief  Create the RMT TX channel on GPIO14 and a bytes encoder with the
 *         WS2812B bit timing, from de_task on core 1 (T-B5-03). No DMA: the
 *         RMT refill interrupt is IRAM-safe (CONFIG_RMT_ISR_IRAM_SAFE),
 *         PROVISIONAL (OD-16, owner Jia Jun).
 * @param  p_rmt  Receives the channel and encoder.
 * @return true when the channel is enabled.
 */
static bool de_start_rmt (de_rmt_t * p_rmt)
{
    rmt_tx_channel_config_t const chan_cfg = {
        .gpio_num          = DE_LED_GPIO,
        .clk_src           = RMT_CLK_SRC_DEFAULT,
        .resolution_hz     = DE_RMT_HZ,
        .mem_block_symbols = DE_RMT_SYMBOLS,
        .trans_queue_depth = 1u,
    };
    rmt_bytes_encoder_config_t const enc_cfg = {
        .bit0  = {.level0    = 1u,
                  .duration0 = DE_T_SHORT,
                  .level1    = 0u,
                  .duration1 = DE_T_LONG},
        .bit1  = {.level0    = 1u,
                  .duration0 = DE_T_LONG,
                  .level1    = 0u,
                  .duration1 = DE_T_SHORT},
        .flags = {.msb_first = 1u},
    };

    esp_err_t err = rmt_new_tx_channel(&chan_cfg, &p_rmt->p_chan);

    if (ESP_OK == err)
    {
        err = rmt_new_bytes_encoder(&enc_cfg, &p_rmt->p_enc);
    }

    if (ESP_OK == err)
    {
        err = rmt_enable(p_rmt->p_chan);
    }

    if (ESP_OK != err)
    {
        printf("display_engine: RMT failed (%d)\n", err);
    }

    return (ESP_OK == err);
}

/*** end of file ***/
