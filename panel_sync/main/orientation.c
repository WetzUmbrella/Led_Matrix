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
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"

#include "orient_filter.h"
#include "tap_detect.h"
#include "tile_tasks.h"

/** I2C pins (PostTri pin row, Waveshare ESP32-S3-Matrix). */
#define OR_SDA_GPIO 11
#define OR_SCL_GPIO 12

/** QMI8658 INT1 pin. PROVISIONAL, owner Jia Jun: confirm against the board
 *  schematic, see orientation.h. */
#define OR_TAP_INT_GPIO GPIO_NUM_10

/** I2C clock, 400 kHz fast mode (plan choice). */
#define OR_I2C_HZ 400000u

/** QMI8658 addresses to probe (boardtest.c). */
#define OR_ADDR_FIRST 0x6Au
#define OR_ADDR_LAST  0x6Bu

/** Sample period 10 ms = 100 Hz, the fastest whole-tick period at
 *  CONFIG_FREERTOS_HZ=100; the tap peak needs more than 50 Hz. */
#define OR_PERIOD_MS 10u

/** Every second sample goes to orient_filter, keeping the T-B5-05 50 Hz. */
#define OR_ORIENT_DIVIDER 2u

/** I2C timeout: no longer than one 50 Hz orientation period (T-B5-05). */
#define OR_I2C_TIMEOUT_MS 20

/** Re-initialise the IMU every 100 failed samples (1 s) while it fails. */
#define OR_REINIT_EVERY 100u

/** QMI8658 registers and values (PROVISIONAL, see orientation.h). */
#define OR_REG_WHO_AM_I  0x00u
#define OR_REG_CTRL1     0x02u
#define OR_REG_CTRL2     0x03u
#define OR_REG_CTRL7     0x08u
#define OR_REG_CTRL8     0x09u
#define OR_REG_CTRL9     0x0Au
#define OR_REG_CAL1_L    0x0Bu
#define OR_REG_STATUSINT 0x2Du
#define OR_REG_STATUS1   0x2Fu
#define OR_REG_AX_L      0x35u
#define OR_REG_TAP       0x59u
#define OR_WHO_AM_I      0x05u
#define OR_CTRL1_VALUE   0x48u /* Auto-increment, little-endian, INT1 on. */
#define OR_CTRL2_VALUE   0x24u /* +/-8 g, 500 Hz. */
#define OR_CTRL7_OFF     0x00u
#define OR_CTRL7_VALUE   0x01u
#define OR_CTRL8_VALUE   0xC0u /* CTRL9 done in STATUSINT, motion on INT1. */
#define OR_CTRL8_TAP     0xC1u /* The same, tap engine on. */
#define OR_CMD_ACK       0x00u
#define OR_CMD_TAP       0x0Cu /* CTRL_CMD_CONFIGURE_TAP. */
#define OR_CMD_DONE_BIT  0x80u /* STATUSINT: CTRL9 command done. */
#define OR_STATUS1_TAP   0x02u /* STATUS1: tap flagged, cleared by reading. */

/** CTRL9 completion: 10 polls of one tick each, 100 ms at 100 Hz. */
#define OR_CMD_POLLS      10u
#define OR_CMD_POLL_TICKS 1u

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
static QueueHandle_t g_p_tap_irq   = NULL; /* Length 1: interrupt time. */
static QueueHandle_t g_p_tap_queue = NULL; /* Accepted tap events. */

/* Orientation Task only. */
static i2c_master_bus_handle_t g_p_bus = NULL;
static i2c_master_dev_handle_t g_p_dev = NULL;

static void orientation_task (void * p_arg);
static void or_tap_isr (void * p_arg);
static bool or_tap_pin_init (void);
static void or_tap_service (tap_detect_t * p_detect);
static void or_tap_publish (tap_detect_event_t const * p_event);
static bool or_imu_init (void);
static bool or_tap_config (void);
static bool or_ctrl9_cmd (uint8_t cmd);
static bool or_ctrl9_wait (bool b_done);
static bool or_write_reg (uint8_t reg, uint8_t value);
static bool or_write_cal (uint8_t const * p_page);
static bool or_read_reg (uint8_t reg, uint8_t * p_value);
static bool qmi_read_accel (int16_t * p_x, int16_t * p_y, int16_t * p_z);
static int16_t or_join (uint8_t const * p_raw);

/**
 * @brief  Create the queues, the tap interrupt and the Orientation Task. A
 *         tap interrupt failure is logged and leaves orientation running.
 *         Called from app_main, so the interrupt lands on core 0, away from
 *         the Display Engine tick on core 1.
 * @return true when the queues and the task were created.
 */
bool orientation_start (void)
{
    bool b_ok = false;

    g_p_mailbox   = xQueueCreate(1u, sizeof(uint8_t));
    g_p_tap_irq   = xQueueCreate(1u, sizeof(int64_t));
    g_p_tap_queue = xQueueCreate(OR_TAP_QUEUE_LEN, sizeof(tap_detect_event_t));

    if ((NULL != g_p_mailbox) && (NULL != g_p_tap_irq) &&
        (NULL != g_p_tap_queue))
    {
        if (!or_tap_pin_init())
        {
            printf("TAP interrupt not available\n");
        }

        b_ok = (pdPASS == xTaskCreatePinnedToCore(
                              orientation_task, TILE_ORIENT_NAME,
                              TILE_ORIENT_STACK, NULL, TILE_ORIENT_PRIO, NULL,
                              TILE_ORIENT_CORE));
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
 * @brief  Orientation Task: 100 Hz samples for the tap peak, every second one
 *         through the orientation filter; a change is written to the mailbox
 *         and logged. A pending tap interrupt is handled after the sample,
 *         so the peak includes it.
 * @param  p_arg  Unused.
 */
static void orientation_task (void * p_arg)
{
    orient_filter_t filter;
    tap_detect_t    detect;
    uint32_t        failures = 0u;
    uint32_t        samples  = 0u;

    (void)p_arg; /* The task takes no parameter. */

    orient_filter_init(&filter);
    tap_detect_init(&detect);

    bool       b_imu = or_imu_init();
    TickType_t wake  = xTaskGetTickCount();

    printf("ORIENT imu %s\n", b_imu ? "ready" : "not found");

    for (;;)
    {
        int16_t accel_x = 0;
        int16_t accel_y = 0;
        int16_t accel_z = 0;

        vTaskDelayUntil(&wake, pdMS_TO_TICKS(OR_PERIOD_MS));

        bool const b_read =
            (b_imu) && (qmi_read_accel(&accel_x, &accel_y, &accel_z));

        if (b_read)
        {
            tap_detect_sample(&detect, accel_x, accel_y, accel_z);
        }

        samples++;

        if ((0u == (samples % OR_ORIENT_DIVIDER)) &&
            (orient_filter_step(&filter, b_read, accel_x, accel_y)))
        {
            (void)xQueueOverwrite(g_p_mailbox, &filter.stable); /* Always. */
            printf("ORIENT changed %" PRIu8 "\n", filter.stable);
        }

        or_tap_service(&detect);

        failures = b_read ? 0u : (failures + 1u);

        if ((0u != failures) && (0u == (failures % OR_REINIT_EVERY)))
        {
            b_imu = or_imu_init(); /* Monitoring retries (T-B5-05 row). */
        }
    }
}

/**
 * @brief  Tap interrupt: record the time for the Orientation Task. No I2C
 *         here; a later interrupt overwrites an unread time.
 * @param  p_arg  Unused.
 */
static void or_tap_isr (void * p_arg)
{
    int64_t const t_us = esp_timer_get_time();

    (void)p_arg; /* The handler takes no parameter. */

    /* Nothing blocks on this queue, so no task is woken: no yield needed. */
    (void)xQueueOverwriteFromISR(g_p_tap_irq, &t_us, NULL);
}

/**
 * @brief  Set up the INT1 pin: input, pulled low, interrupt on the rising
 *         edge. An ISR service already installed by another module is fine.
 * @return true when the handler is attached.
 */
static bool or_tap_pin_init (void)
{
    gpio_config_t const pin_cfg = {
        .pin_bit_mask = (UINT64_C(1) << OR_TAP_INT_GPIO),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type    = GPIO_INTR_POSEDGE,
    };

    bool b_ok = (ESP_OK == gpio_config(&pin_cfg));

    if (b_ok)
    {
        esp_err_t const err = gpio_install_isr_service(0);

        b_ok = (ESP_OK == err) || (ESP_ERR_INVALID_STATE == err);
    }

    return (b_ok) &&
           (ESP_OK == gpio_isr_handler_add(OR_TAP_INT_GPIO, or_tap_isr, NULL));
}

/**
 * @brief  Handle a pending tap interrupt: read the IMU status over I2C, run
 *         the detector, and log the result (the log is the tuning data).
 * @param  p_detect  The detector.
 */
static void or_tap_service (tap_detect_t * p_detect)
{
    int64_t t_us = 0;

    if (pdTRUE == xQueueReceive(g_p_tap_irq, &t_us, 0))
    {
        uint8_t status1 = 0u;
        uint8_t tap     = 0u; /* 0 is refused by tap_detect_on_irq(). */

        /* Reading STATUS1 releases the interrupt line. No device handle
         * means the IMU never answered, and the zeros are refused below. */
        if ((NULL != g_p_dev) && (or_read_reg(OR_REG_STATUS1, &status1)) &&
            (0u != (status1 & OR_STATUS1_TAP)))
        {
            /* A failed read leaves 0, which is refused. */
            (void)or_read_reg(OR_REG_TAP, &tap);
        }

        uint16_t const     peak = tap_detect_peak(p_detect);
        tap_detect_event_t event;

        tap_detect_result_t const result =
            tap_detect_on_irq(p_detect, t_us, tap, &event);

        if (TAP_DETECT_ACCEPT == result)
        {
            or_tap_publish(&event);
        }

        /* The enum has four values, so it fits uint32_t. */
        printf("TAP result=%" PRIu32 " t_us=%" PRId64 " peak=%" PRIu16
               " status1=0x%02" PRIX8 " tap=0x%02" PRIX8 "\n",
               (uint32_t)result, t_us, peak, status1, tap);
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

        /* This task is the only writer, so the second send has room. */
        (void)xQueueReceive(g_p_tap_queue, &oldest, 0);
        (void)xQueueSend(g_p_tap_queue, p_event, 0);
    }
}

/**
 * @brief  Create the I2C bus once, probe 0x6A then 0x6B, check WHO_AM_I and
 *         configure the accelerometer and the tap engine, sensors off while
 *         configuring. A tap engine failure is logged and leaves orientation
 *         running.
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
               (or_write_reg(OR_REG_CTRL2, OR_CTRL2_VALUE));

        if ((b_ok) && (!or_tap_config()))
        {
            printf("TAP engine not configured\n");
        }

        b_ok = (b_ok) && (or_write_reg(OR_REG_CTRL7, OR_CTRL7_VALUE));
    }

    return b_ok;
}

/**
 * @brief  Load both tap pages into the IMU and turn the tap engine on.
 *         Sensors must be off.
 * @return true on success.
 */
static bool or_tap_config (void)
{
    uint8_t first[TAP_DETECT_CONFIG_BYTES];
    uint8_t second[TAP_DETECT_CONFIG_BYTES];

    tap_detect_encode_config(first, second);

    return (or_write_reg(OR_REG_CTRL8, OR_CTRL8_VALUE)) &&
           (or_write_cal(first)) && (or_ctrl9_cmd(OR_CMD_TAP)) &&
           (or_write_cal(second)) && (or_ctrl9_cmd(OR_CMD_TAP)) &&
           (or_write_reg(OR_REG_CTRL8, OR_CTRL8_TAP));
}

/**
 * @brief  Run one CTRL9 command: send, wait for done, acknowledge, wait for
 *         done to clear.
 * @param  cmd  The command.
 * @return true on success.
 */
static bool or_ctrl9_cmd (uint8_t cmd)
{
    return (or_write_reg(OR_REG_CTRL9, cmd)) && (or_ctrl9_wait(true)) &&
           (or_write_reg(OR_REG_CTRL9, OR_CMD_ACK)) && (or_ctrl9_wait(false));
}

/**
 * @brief  Poll the CTRL9 done bit in STATUSINT.
 * @param  b_done  The bit state to wait for.
 * @return true when the bit reached that state in time.
 */
static bool or_ctrl9_wait (bool b_done)
{
    bool b_reached = false;

    for (uint32_t poll = 0u; (!b_reached) && (poll < OR_CMD_POLLS); poll++)
    {
        uint8_t status = 0u;

        if (or_read_reg(OR_REG_STATUSINT, &status))
        {
            b_reached = (b_done == (0u != (status & OR_CMD_DONE_BIT)));
        }

        if (!b_reached)
        {
            vTaskDelay(OR_CMD_POLL_TICKS);
        }
    }

    return b_reached;
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
 * @brief  Write one tap page to CAL1_L onwards (auto-increment is on).
 * @param  p_page  TAP_DETECT_CONFIG_BYTES bytes.
 * @return true on success.
 */
static bool or_write_cal (uint8_t const * p_page)
{
    uint8_t bytes[TAP_DETECT_CONFIG_BYTES + 1u];

    bytes[0] = OR_REG_CAL1_L;

    for (uint32_t idx = 0u; idx < TAP_DETECT_CONFIG_BYTES; idx++)
    {
        bytes[idx + 1u] = p_page[idx];
    }

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
