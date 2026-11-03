/**
 * @file    orientation.c
 * @brief   Orientation Task (T-B5-05). See orientation.h.
 *
 * Owner: Jia Jun (Buddy 5). Implemented by Ahsanul (B4) for Jia Jun, task
 * T-B5-05.
 *
 * Contexts: the Orientation Task owns the I2C bus, the device handle and
 * the filter, and is the only writer of the mailbox; any task may peek the
 * mailbox (orientation_current()).
 *
 * DEVIATION D-2 (module-wide): ESP-IDF and FreeRTOS names and types
 * (i2c_master_bus_handle_t, i2c_master_dev_handle_t, QueueHandle_t,
 * TickType_t, esp_err_t, the int timeout of i2c_master_*).
 */
#include "orientation.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"

#include "orient_filter.h"
#include "tile_tasks.h"

/** I2C pins (PostTri pin row, Waveshare ESP32-S3-Matrix). */
#define OR_SDA_GPIO 11
#define OR_SCL_GPIO 12

/** I2C clock, 400 kHz fast mode (plan choice). */
#define OR_I2C_HZ 400000u

/** QMI8658 addresses to probe (boardtest.c). */
#define OR_ADDR_FIRST 0x6Au
#define OR_ADDR_LAST  0x6Bu

/** Sample period 20 ms = 50 Hz (T-B5-05 row). */
#define OR_PERIOD_MS 20u

/** I2C timeout: no longer than one sample period (derived from 50 Hz). */
#define OR_I2C_TIMEOUT_MS 20

/** Re-initialise the IMU every 50 failed samples (1 s) while it fails. */
#define OR_REINIT_EVERY 50u

/** QMI8658 registers and values (PROVISIONAL, see orientation.h). */
#define OR_REG_WHO_AM_I 0x00u
#define OR_REG_CTRL1    0x02u
#define OR_REG_CTRL2    0x03u
#define OR_REG_CTRL7    0x08u
#define OR_REG_AX_L     0x35u
#define OR_WHO_AM_I     0x05u
#define OR_CTRL1_VALUE  0x40u
#define OR_CTRL2_VALUE  0x06u
#define OR_CTRL7_VALUE  0x01u

/** Accelerometer bytes read: X and Y, low byte first. */
#define OR_ACCEL_BYTES 4u

/** Bits per byte, to join the little-endian halves. */
#define OR_BYTE_BITS 8u

static QueueHandle_t g_p_mailbox = NULL; /* Length 1, overwrite only. */

/* Orientation Task only. */
static i2c_master_bus_handle_t g_p_bus = NULL;
static i2c_master_dev_handle_t g_p_dev = NULL;

static void orientation_task (void * p_arg);
static bool or_imu_init (void);
static bool or_write_reg (uint8_t reg, uint8_t value);
static bool qmi_read_accel (int16_t * p_x, int16_t * p_y);

/**
 * @brief  Create the mailbox and the Orientation Task.
 * @return true when both were created.
 */
bool orientation_start (void)
{
    bool b_ok = false;

    g_p_mailbox = xQueueCreate(1u, sizeof(uint8_t));

    if (NULL != g_p_mailbox)
    {
        b_ok = (pdPASS == xTaskCreatePinnedToCore(
                              orientation_task, TILE_ORIENT_NAME,
                              TILE_ORIENT_STACK, NULL, TILE_ORIENT_PRIO, NULL,
                              TILE_ORIENT_CORE));
    }

    return b_ok;
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
 * @brief  Orientation Task: 50 Hz samples through the filter; a change is
 *         written to the mailbox and logged.
 * @param  p_arg  Unused.
 */
static void orientation_task (void * p_arg)
{
    orient_filter_t filter;
    uint32_t        failures = 0u;

    (void)p_arg; /* The task takes no parameter. */

    orient_filter_init(&filter);

    bool       b_imu = or_imu_init();
    TickType_t wake  = xTaskGetTickCount();

    printf("ORIENT imu %s\n", b_imu ? "ready" : "not found");

    for (;;)
    {
        int16_t accel_x = 0;
        int16_t accel_y = 0;

        vTaskDelayUntil(&wake, pdMS_TO_TICKS(OR_PERIOD_MS));

        bool const b_read = (b_imu) && (qmi_read_accel(&accel_x, &accel_y));

        if (orient_filter_step(&filter, b_read, accel_x, accel_y))
        {
            (void)xQueueOverwrite(g_p_mailbox, &filter.stable); /* Always. */
            printf("ORIENT changed %" PRIu8 "\n", filter.stable);
        }

        failures = b_read ? 0u : (failures + 1u);

        if ((0u != failures) && (0u == (failures % OR_REINIT_EVERY)))
        {
            b_imu = or_imu_init(); /* Monitoring retries (T-B5-05 row). */
        }
    }
}

/**
 * @brief  Create the I2C bus once, probe 0x6A then 0x6B, check WHO_AM_I and
 *         configure the accelerometer.
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
        uint8_t         reg = OR_REG_WHO_AM_I;
        uint8_t         who = 0u;
        esp_err_t const err = i2c_master_transmit_receive(
            g_p_dev, &reg, 1u, &who, 1u, OR_I2C_TIMEOUT_MS);

        b_ok = (ESP_OK == err) && (OR_WHO_AM_I == who) &&
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
 * @brief  Read accelerometer X and Y (raw, 16-bit, kept 16-bit: VB-5c).
 * @param  p_x  Receives X.
 * @param  p_y  Receives Y.
 * @return true on success within the 20 ms timeout.
 */
static bool qmi_read_accel (int16_t * p_x, int16_t * p_y)
{
    uint8_t         reg = OR_REG_AX_L;
    uint8_t         raw[OR_ACCEL_BYTES];
    esp_err_t const err = i2c_master_transmit_receive(
        g_p_dev, &reg, 1u, raw, sizeof(raw), OR_I2C_TIMEOUT_MS);

    if (ESP_OK == err)
    {
        /* Two's complement 16-bit values, low byte first; the joined value
         * is below 65,536, so the narrowing keeps all 16 bits. */
        uint16_t const raw_x =
            (uint16_t)(((uint32_t)raw[1] << OR_BYTE_BITS) | (uint32_t)raw[0]);
        /* Same for Y. */
        uint16_t const raw_y =
            (uint16_t)(((uint32_t)raw[3] << OR_BYTE_BITS) | (uint32_t)raw[2]);

        *p_x = (int16_t)raw_x; /* Same 16 bits read as signed. */
        *p_y = (int16_t)raw_y; /* Same. */
    }

    return (ESP_OK == err);
}

/*** end of file ***/
