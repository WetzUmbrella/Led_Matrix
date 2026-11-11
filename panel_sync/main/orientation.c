/**
 * @file    orientation.c
 * @brief   Orientation Task (T-B5-05) and tap sampling (build step 2). See
 *          orientation.h.
 *
 * Owner: Jia Jun (Buddy 5). Implemented by Ahsanul (B4) for Jia Jun, task
 * T-B5-05; the 1 kHz sampling and software taps added 11 Nov 2026 (DS-02).
 *
 * Contexts: the Orientation Task owns the I2C bus, the device handle, the
 * filter and the detector, and is the only writer of the mailbox and the tap
 * queue; any task may peek the mailbox (orientation_current()); one consumer
 * takes taps (orientation_tap_take()). The esp_timer callback only notifies
 * the task.
 *
 * DEVIATION D-2 (module-wide): ESP-IDF and FreeRTOS names and types
 * (i2c_master_bus_handle_t, i2c_master_dev_handle_t, QueueHandle_t,
 * TaskHandle_t, esp_timer_handle_t, esp_err_t, the int timeout of
 * i2c_master_*).
 */
#include "orientation.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"

#include "orient_filter.h"
#include "tap_detect.h"
#include "tile_net.h"
#include "tile_tasks.h"

/** I2C pins (PostTri pin row, Waveshare ESP32-S3-Matrix). */
#define OR_SDA_GPIO 11
#define OR_SCL_GPIO 12

/** I2C clock, 400 kHz fast mode (plan choice). */
#define OR_I2C_HZ 400000u

/** QMI8658 addresses to probe (boardtest.c). */
#define OR_ADDR_FIRST 0x6Au
#define OR_ADDR_LAST  0x6Bu

/** Sample period, microseconds: 1 kHz from an esp_timer (DS-02). */
#define OR_PERIOD_US 1000u

/** Every 20th sample goes to orient_filter, keeping the T-B5-05 50 Hz. */
#define OR_ORIENT_DIVIDER 20u

/** I2C timeout: no longer than one 50 Hz orientation period (T-B5-05). */
#define OR_I2C_TIMEOUT_MS 20

/** Re-initialise the IMU every 1,000 failed samples (1 s) while it fails. */
#define OR_REINIT_EVERY 1000u

/** Samples ignored by the tap detector after the IMU is (re)configured
 *  (200 ms): the first readings are not settled, and a baseline seeded from
 *  them gave two false taps at every boot (bench 11 Nov 2026). */
#define OR_SETTLE_SAMPLES 200u

/** QMI8658 registers and values (see orientation.h). */
#define OR_REG_WHO_AM_I 0x00u
#define OR_REG_CTRL1    0x02u
#define OR_REG_CTRL2    0x03u
#define OR_REG_CTRL7    0x08u
#define OR_REG_AX_L     0x35u
#define OR_WHO_AM_I     0x05u
#define OR_CTRL1_VALUE  0x40u /* Address auto-increment, little-endian. */
#define OR_CTRL2_VALUE  0x33u /* +/-16 g, 1,000 Hz. */
#define OR_CTRL7_OFF    0x00u
#define OR_CTRL7_VALUE  0x01u /* Accelerometer on. */

/** Accelerometer bytes read: X, Y and Z, low byte first. */
#define OR_ACCEL_BYTES 6u

/** Offsets of Y and Z in those bytes. */
#define OR_ACCEL_Y 2u
#define OR_ACCEL_Z 4u

/** Bits per byte, to join the little-endian halves. */
#define OR_BYTE_BITS 8u

/** Accepted taps held for the consumer; the oldest goes when full. */
#define OR_TAP_QUEUE_LEN 4u

static QueueHandle_t g_p_mailbox   = NULL; /* Length 1, overwrite only. */
static QueueHandle_t g_p_tap_queue = NULL; /* Accepted tap events. */
static TaskHandle_t  g_p_task      = NULL; /* Woken by the sample timer. */

/* Orientation Task only. */
static i2c_master_bus_handle_t g_p_bus = NULL;
static i2c_master_dev_handle_t g_p_dev = NULL;

static void    orientation_task (void * p_arg);
static void    or_tick (void * p_arg);
static void    or_tap_publish (tap_detect_event_t const * p_event);
static bool    or_imu_init (void);
static bool    or_write_reg (uint8_t reg, uint8_t value);
static bool    or_read_reg (uint8_t reg, uint8_t * p_value);
static bool    qmi_read_accel (int16_t * p_x, int16_t * p_y, int16_t * p_z);
static int16_t or_join (uint8_t const * p_raw);

/**
 * @brief  Create the queues and the Orientation Task; the task starts its
 *         own 1 kHz sample timer.
 * @return true when the queues and the task were created.
 */
bool orientation_start (void)
{
    bool b_ok = false;

    g_p_mailbox   = xQueueCreate(1u, sizeof(uint8_t));
    g_p_tap_queue = xQueueCreate(OR_TAP_QUEUE_LEN, sizeof(tap_detect_event_t));

    if ((NULL != g_p_mailbox) && (NULL != g_p_tap_queue))
    {
        b_ok = (pdPASS == xTaskCreatePinnedToCore(
                              orientation_task, TILE_ORIENT_NAME,
                              TILE_ORIENT_STACK, NULL, TILE_ORIENT_PRIO,
                              &g_p_task, TILE_ORIENT_CORE));
    }

    return b_ok;
}

/**
 * @brief  Take the oldest accepted tap, without blocking.
 * @param  p_event  Receives the tap.
 * @return true when a tap was taken.
 */
bool orientation_tap_take (tap_detect_event_t * p_event)
{
    bool b_taken = false;

    if (NULL != g_p_tap_queue)
    {
        b_taken = (pdTRUE == xQueueReceive(g_p_tap_queue, p_event, 0));
    }

    return b_taken;
}

/**
 * @brief  The stable orientation.
 * @return 0 to 3; 0 before the first change.
 */
uint8_t orientation_current (void)
{
    uint8_t value = 0u;

    if (NULL != g_p_mailbox)
    {
        /* Empty until the first ORIENTATION_CHANGED: value stays 0. */
        (void)xQueuePeek(g_p_mailbox, &value, 0);
    }

    return value;
}

/**
 * @brief  Orientation Task: one sample per 1 ms timer tick through the tap
 *         detector; every 20th through the orientation filter; a change is
 *         written to the mailbox and logged. Ticks missed while a sample was
 *         read are not caught up (the next tick reads the newest sample).
 * @param  p_arg  Unused.
 */
static void orientation_task (void * p_arg)
{
    orient_filter_t    filter;
    tap_detect_t       detect;
    esp_timer_handle_t timer    = NULL;
    uint32_t           failures = 0u;
    uint32_t           samples  = 0u;
    uint32_t           settle   = OR_SETTLE_SAMPLES;

    (void)p_arg; /* The task takes no parameter. */

    orient_filter_init(&filter);
    tap_detect_init(&detect);

    bool b_imu = or_imu_init();

    printf("ORIENT imu %s\n", b_imu ? "ready" : "not found");

    esp_timer_create_args_t const args = {.callback = or_tick,
                                          .name     = "or_tick"};

    if ((ESP_OK != esp_timer_create(&args, &timer)) ||
        (ESP_OK != esp_timer_start_periodic(timer, OR_PERIOD_US)))
    {
        printf("ORIENT sample timer failed\n");
    }

    for (;;)
    {
        int16_t            accel_x = 0;
        int16_t            accel_y = 0;
        int16_t            accel_z = 0;
        tap_detect_event_t event;

        /* Woken by or_tick(); the count is cleared, so a late pass does not
         * run twice. The returned count is not needed. */
        (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        int64_t const t_us = esp_timer_get_time();
        bool const    b_read =
            (b_imu) && (qmi_read_accel(&accel_x, &accel_y, &accel_z));

        if ((b_read) && (0u != settle))
        {
            settle--; /* The IMU is still settling: no taps yet. */
        }
        else if ((b_read) && (tap_detect_sample(&detect, t_us, accel_x,
                                                accel_y, accel_z, &event)))
        {
            or_tap_publish(&event);
            tile_net_tap_ready();
            printf("TAP t_us=%" PRId64 " peak_mg=%" PRIu16 " count=%" PRIu32
                   "\n",
                   event.t_us, event.peak, event.count);
        }
        else
        {
            /* No sample, or a sample with no completed tap. */
        }

        samples++;

        if ((0u == (samples % OR_ORIENT_DIVIDER)) &&
            (orient_filter_step(&filter, b_read, accel_x, accel_y)))
        {
            (void)xQueueOverwrite(g_p_mailbox, &filter.stable); /* Always. */
            printf("ORIENT changed %" PRIu8 "\n", filter.stable);
        }

        failures = b_read ? 0u : (failures + 1u);

        if ((0u != failures) && (0u == (failures % OR_REINIT_EVERY)))
        {
            b_imu = or_imu_init(); /* Monitoring retries (T-B5-05 row). */
            settle = OR_SETTLE_SAMPLES;
            tap_detect_init(&detect); /* A fresh baseline after settling. */
        }
    }
}

/**
 * @brief  Sample timer (esp_timer task context): wake the Orientation Task.
 * @param  p_arg  Unused.
 */
static void or_tick (void * p_arg)
{
    (void)p_arg; /* The callback takes no parameter. */

    if (NULL != g_p_task)
    {
        xTaskNotifyGive(g_p_task);
    }
}

/**
 * @brief  Queue an accepted tap, dropping the oldest when full, so a slow
 *         or absent consumer never gets stale taps ahead of new ones.
 * @param  p_event  The tap.
 */
static void or_tap_publish (tap_detect_event_t const * p_event)
{
    if (errQUEUE_FULL == xQueueSend(g_p_tap_queue, p_event, 0))
    {
        tap_detect_event_t oldest;

        /* Drop the oldest tap; the queue is full, so this cannot fail. */
        (void)xQueueReceive(g_p_tap_queue, &oldest, 0);
        /* This task is the only writer, so the second send has room. */
        (void)xQueueSend(g_p_tap_queue, p_event, 0);
    }
}

/**
 * @brief  Create the I2C bus once, probe 0x6A then 0x6B, check WHO_AM_I and
 *         configure the accelerometer, sensors off while configuring.
 * @return true when the IMU answered and was configured.
 */
static bool or_imu_init (void)
{
    bool b_ok = (NULL != g_p_bus);

    if (!b_ok)
    {
        i2c_master_bus_config_t const bus_cfg = {
            .i2c_port                     = I2C_NUM_0,
            .sda_io_num                   = OR_SDA_GPIO,
            .scl_io_num                   = OR_SCL_GPIO,
            .clk_source                   = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt            = 7u,
            .flags.enable_internal_pullup = true,
        };

        b_ok = (ESP_OK == i2c_new_master_bus(&bus_cfg, &g_p_bus));
    }

    if ((b_ok) && (NULL == g_p_dev))
    {
        b_ok = false;

        for (uint16_t addr = OR_ADDR_FIRST; (!b_ok) && (addr <= OR_ADDR_LAST);
             addr++)
        {
            if (ESP_OK == i2c_master_probe(g_p_bus, addr, OR_I2C_TIMEOUT_MS))
            {
                i2c_device_config_t const dev_cfg = {
                    .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                    .device_address  = addr,
                    .scl_speed_hz    = OR_I2C_HZ,
                };

                b_ok = (ESP_OK ==
                        i2c_master_bus_add_device(g_p_bus, &dev_cfg, &g_p_dev));
            }
        }
    }

    if (b_ok)
    {
        uint8_t who = 0u;

        b_ok = (or_read_reg(OR_REG_WHO_AM_I, &who)) && (OR_WHO_AM_I == who) &&
               (or_write_reg(OR_REG_CTRL7, OR_CTRL7_OFF)) &&
               (or_write_reg(OR_REG_CTRL1, OR_CTRL1_VALUE)) &&
               (or_write_reg(OR_REG_CTRL2, OR_CTRL2_VALUE)) &&
               (or_write_reg(OR_REG_CTRL7, OR_CTRL7_VALUE));
    }

    return b_ok;
}

/**
 * @brief  Write one QMI8658 register (20 ms timeout).
 * @param  reg    Register.
 * @param  value  Value.
 * @return true on success.
 */
static bool or_write_reg (uint8_t reg, uint8_t value)
{
    uint8_t const bytes[2] = {reg, value};

    return (ESP_OK == i2c_master_transmit(g_p_dev, bytes, sizeof(bytes),
                                          OR_I2C_TIMEOUT_MS));
}

/**
 * @brief  Read one QMI8658 register (20 ms timeout).
 * @param  reg      Register.
 * @param  p_value  Receives the value.
 * @return true on success.
 */
static bool or_read_reg (uint8_t reg, uint8_t * p_value)
{
    return (ESP_OK == i2c_master_transmit_receive(g_p_dev, &reg, 1u, p_value,
                                                  1u, OR_I2C_TIMEOUT_MS));
}

/**
 * @brief  Read accelerometer X, Y and Z (raw, 16-bit, kept 16-bit: VB-5c).
 * @param  p_x  Receives X.
 * @param  p_y  Receives Y.
 * @param  p_z  Receives Z.
 * @return true on success within the 20 ms timeout.
 */
static bool qmi_read_accel (int16_t * p_x, int16_t * p_y, int16_t * p_z)
{
    uint8_t         reg = OR_REG_AX_L;
    uint8_t         raw[OR_ACCEL_BYTES];
    esp_err_t const err = i2c_master_transmit_receive(
        g_p_dev, &reg, 1u, raw, sizeof(raw), OR_I2C_TIMEOUT_MS);

    if (ESP_OK == err)
    {
        *p_x = or_join(&raw[0]);
        *p_y = or_join(&raw[OR_ACCEL_Y]);
        *p_z = or_join(&raw[OR_ACCEL_Z]);
    }

    return (ESP_OK == err);
}

/**
 * @brief  Join a two's complement 16-bit value, low byte first.
 * @param  p_raw  The two bytes.
 * @return The value.
 */
static int16_t or_join (uint8_t const * p_raw)
{
    /* The joined value is below 65,536, so the narrowing keeps all 16 bits. */
    uint16_t const joined =
        (uint16_t)(((uint32_t)p_raw[1] << OR_BYTE_BITS) | (uint32_t)p_raw[0]);

    return (int16_t)joined; /* Same 16 bits read as signed. */
}

/*** end of file ***/
