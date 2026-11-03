/**
 * @file    display_engine.c
 * @brief   Display Engine Task (T-B5-01): gptimer one-shot tick, test pin and
 *          the Week 10 moving pixel.
 *
 * Minimal implementation by Buddy 4 for T-B4-03. Owner: Jia Jun, task
 * T-B5-01. Implemented and not yet implemented: see display_engine.h.
 * PROVISIONAL choices: see display_engine.h.
 *
 * DEVIATION D-2 (module-wide): ESP-IDF, FreeRTOS and led_strip names, types
 * and macros (gptimer_handle_t, TaskHandle_t, BaseType_t, pdTRUE, IRAM_ATTR,
 * led_strip_handle_t, ESP_OK).
 */
#include "display_engine.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "hal/gpio_ll.h"
#include "led_strip.h"
#include "soc/gpio_struct.h"

#include "tile_tasks.h"
#include "timesync.h"

/** gptimer resolution: 1 MHz, so one count is 1 us. */
#define DE_TIMER_HZ 1000000u

/** Timeline origin in Pico time for the rig build (PROVISIONAL). */
#define DE_ORIGIN_US INT64_C(0)

/** Least lead for arming a tick, microseconds (plan choice: room for the
 *  arming calls themselves); a deadline closer than this is a missed tick. */
#define DE_MIN_LEAD_US INT64_C(200)

/** Marker every 64 ticks and the two pulse widths (RIG.md section 4b). */
#define DE_MARKER_PERIOD 64u
#define DE_PULSE_US      INT64_C(1000)
#define DE_MARKER_US     INT64_C(4000)

/** LED matrix: 64 WS2812B on GPIO14, value 8 = the 8/255 cap (VC-6). */
#define DE_LED_GPIO  14
#define DE_LED_COUNT 64u
#define DE_LED_VALUE 8u
#define DE_RMT_HZ    10000000u

/** Poll period while waiting for SYNC_READY before the first tick (plan
 *  choice: 20 ms, about one beacon interval / 5). */
#define DE_WAIT_SYNC_MS 20u

/** Count line every 600 ticks (10 s, plan choice). */
#define DE_LOG_EVERY 600u

static TaskHandle_t g_p_de_task = NULL;

/** Level the next alarm writes to the test pin; set by de_task before
 *  arming, read by the ISR. */
static volatile bool g_b_pin_high_next = false;

/* Written and read only by de_task. */
static uint32_t g_ticks  = 0u;
static uint32_t g_missed = 0u;

static void     de_task (void * p_arg);
static uint64_t de_run_tick (gptimer_handle_t    p_timer,
                             led_strip_handle_t  p_strip,
                             timesync_reader_t * p_reader, uint64_t tick);
static bool     de_tick_isr (gptimer_handle_t                   p_timer,
                             gptimer_alarm_event_data_t const * p_event,
                             void *                             p_ctx);
static void     de_arm_next (gptimer_handle_t p_timer, int64_t delay_us,
                             bool b_pin_high);
static void     de_wait_alarm (void);
static void     de_show_tick (led_strip_handle_t p_strip, uint64_t tick);
static gptimer_handle_t   de_timer_create (void);
static led_strip_handle_t de_strip_create (void);

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
 * @brief  Display Engine Task, core 1. For each tick n: deadline =
 *         timesync_deadline_local(origin, n), computed from n every frame;
 *         arm a one-shot for the rising edge; on the alarm, arm the falling
 *         edge, show the pixel, notify Time Sync, wait for the falling edge,
 *         then n + 1. A deadline already too close counts as missed and the
 *         task moves to the next tick after now.
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

    gptimer_handle_t const   p_timer = de_timer_create();
    led_strip_handle_t const p_strip = de_strip_create();
    timesync_reader_t        reader  = {0};

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

    for (;;)
    {
        if (NULL == p_timer)
        {
            /* No tick source: stay idle (a FreeRTOS task never returns). */
            vTaskDelay(portMAX_DELAY);
        }
        else
        {
            tick = de_run_tick(p_timer, p_strip, &reader, tick);
        }
    }
}

/**
 * @brief  One pass of the tick loop: arm, pulse, show, notify. A deadline
 *         already too close counts as missed and the next tick after now is
 *         returned instead.
 * @param  p_timer   The tick timer.
 * @param  p_strip   LED driver, or NULL.
 * @param  p_reader  This task's timesync reader.
 * @param  tick      Tick number n to show.
 * @return The next tick to show.
 */
static uint64_t de_run_tick (gptimer_handle_t    p_timer,
                             led_strip_handle_t  p_strip,
                             timesync_reader_t * p_reader, uint64_t tick)
{
    uint64_t next = tick + 1u;

    int64_t const deadline =
        timesync_deadline_local(p_reader, DE_ORIGIN_US, tick);
    int64_t const lead = deadline - esp_timer_get_time();

    if (lead < DE_MIN_LEAD_US)
    {
        g_missed++;
        next =
            timesync_next_tick(DE_ORIGIN_US, timesync_now_corrected(p_reader));
    }
    else
    {
        int64_t const width =
            (0u == (tick % DE_MARKER_PERIOD)) ? DE_MARKER_US : DE_PULSE_US;

        de_arm_next(p_timer, lead, true);
        de_wait_alarm(); /* Rising edge: tick n. */
        de_arm_next(p_timer, width, false);
        de_show_tick(p_strip, tick);
        timesync_notify_tick();
        de_wait_alarm(); /* Falling edge. */
        g_ticks++;

        if (0u == (g_ticks % DE_LOG_EVERY))
        {
            printf("DE ticks=%" PRIu32 " missed=%" PRIu32 "\n", g_ticks,
                   g_missed);
        }
    }

    return next;
}

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
 * @brief  DE_W10_TEST_PATTERN: light only LED (n mod 64) at value 8 on all
 *         three channels; the previous LED goes dark.
 * @param  p_strip  LED driver, or NULL if it failed to start.
 * @param  tick     Tick number n.
 */
static void de_show_tick (led_strip_handle_t p_strip, uint64_t tick)
{
#if DE_W10_TEST_PATTERN
    if (NULL != p_strip)
    {
        /* tick mod 64 is below 64, so it fits uint32_t. */
        uint32_t const led  = (uint32_t)(tick % DE_LED_COUNT);
        uint32_t const prev = (led + DE_LED_COUNT - 1u) % DE_LED_COUNT;

        /* Both indices are below DE_LED_COUNT, so the calls cannot fail on
         * range; their results are not needed. */
        (void)led_strip_set_pixel(p_strip, prev, 0u, 0u, 0u);

        /* led is below DE_LED_COUNT: the result is not needed. */
        (void)led_strip_set_pixel(p_strip, led, DE_LED_VALUE, DE_LED_VALUE,
                                  DE_LED_VALUE);
        (void)led_strip_refresh(p_strip); /* A failed frame is not retried. */
    }
#else
    (void)p_strip; /* No LED content outside the test pattern build. */
    (void)tick;    /* Same. */
#endif
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
 * @brief  Create the WS2812B driver on GPIO14 (RMT, created on core 1).
 * @return The driver, or NULL if it could not start.
 */
static led_strip_handle_t de_strip_create (void)
{
    led_strip_handle_t p_strip = NULL;

    led_strip_config_t const strip_cfg = {
        .strip_gpio_num         = DE_LED_GPIO,
        .max_leds               = DE_LED_COUNT,
        .led_model              = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_RGB,
    };
    led_strip_rmt_config_t const rmt_cfg = {
        .clk_src       = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = DE_RMT_HZ,
    };

    if (ESP_OK != led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &p_strip))
    {
        printf("display_engine: LED driver failed, pulses only\n");
        p_strip = NULL;
    }

    return p_strip;
}

/*** end of file ***/
