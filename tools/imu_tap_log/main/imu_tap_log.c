/**
 * @file    imu_tap_log.c
 * @brief   Tap-feasibility logger for hardware test card H6
 *          (docs/bench/2026-11-06_hardware_testing.md, tools/imu_tap_log/
 *          README.md).
 *
 * QMI8658 accelerometer on the Waveshare ESP32-S3-Matrix: I2C SDA GPIO11,
 * SCL GPIO12, 400 kHz, address 0x6A then 0x6B, WHO_AM_I 0x05 (as
 * panel_sync/main/orientation.c). Accelerometer only, +/-16 g (2048 LSB/g,
 * the widest range, so a firm tap does not clip), ODR 1000 Hz.
 *
 * Sample source (ITL_SOURCE_FIFO):
 *   1 (default) the QMI8658 FIFO, 64 samples, stream mode. A periodic
 *     esp_timer wakes the sampling task every 10 ms; the task drains the FIFO
 *     (CTRL9 CTRL_CMD_REQ_FIFO, read the count, burst-read FIFO_DATA, leave
 *     read mode). One drain is one window. FIFO_STATUS overflow counts as an
 *     overrun.
 *   0 (fallback) data-ready poll: the esp_timer wakes the task every 500 us;
 *     the task reads STATUS0 and, when bit 0 is set, the sample counter,
 *     temperature and accelerometer in one burst (0x30 to 0x3A). A jump in
 *     the 24-bit sample counter counts the skipped samples as overruns.
 * Either way the task blocks between wake-ups, so the IDLE tasks and the
 * task watchdog are never starved, and the rate does not depend on the
 * FreeRTOS tick (100 Hz by default).
 *
 * Peak: per axis, a slow baseline b (exponential moving average, time
 * constant 500 ms, so each sample moves b by 1/500 of the difference) stands
 * for gravity. The baseline is held (not updated) while the current or the
 * previous window's peak is over ITL_HOLD_MG, so a tap does not drag it. The
 * per-sample deviation is |a - b| = sqrt(dx^2 + dy^2 + dz^2) in mg, and each
 * window's line carries its largest deviation.
 *
 * Output, one line each, nothing read from the port:
 *   H,board=<n or MAC>,range_g=16,odr_hz=1000,window_ms=10,src=<fifo|poll>,
 *     tau_ms=500,hold_mg=<n>          every second (a logger started late
 *                                     still sees it)
 *   S,<ms>,<samples in the last second>,<overruns in the last second>
 *   T,<ms>,<peak deviation in mg>     every 10 ms window
 *   E,imu_not_found                   once a second while the IMU is missing
 * ms is time since boot. Only the sampling task prints after start-up.
 *
 * PROVISIONAL: the FIFO and sample-counter registers are from memory of the
 * QMI8658A datasheet. The S line shows the real rate on the bench; if it is
 * well under 1000 with src=fifo, set ITL_SOURCE_FIFO to 0 and rebuild.
 *
 * DEVIATION D-1 (module-wide): ESP-IDF and FreeRTOS names and types
 * (i2c_master_*, esp_timer_*, nvs_*, TaskHandle_t, BaseType_t, esp_err_t).
 */
#include <inttypes.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#ifndef ITL_SOURCE_FIFO
/** 1 = QMI8658 FIFO (default), 0 = data-ready poll from an esp_timer. */
#define ITL_SOURCE_FIFO 1
#endif

/* I2C bus (QMI8658 on the Waveshare ESP32-S3-Matrix). */
#define ITL_SDA_GPIO       11
#define ITL_SCL_GPIO       12
#define ITL_I2C_HZ         400000u
#define ITL_I2C_TIMEOUT_MS 20
#define ITL_GLITCH_CNT     7u
#define ITL_ADDR_FIRST     0x6Au
#define ITL_ADDR_LAST      0x6Bu

/* QMI8658 registers and values. */
#define ITL_REG_WHO_AM_I   0x00u
#define ITL_WHO_AM_I       0x05u
#define ITL_REG_CTRL1      0x02u
#define ITL_CTRL1_VALUE    0x40u /* Address auto-increment, little-endian. */
#define ITL_REG_CTRL2      0x03u
#define ITL_CTRL2_VALUE    0x33u /* aFS 011 = +/-16 g, aODR 0011 = 1000 Hz. */
#define ITL_REG_CTRL7      0x08u
#define ITL_CTRL7_OFF      0x00u
#define ITL_CTRL7_ACC_ON   0x01u
#define ITL_REG_CTRL8      0x09u
#define ITL_CTRL8_VALUE    0x80u /* CTRL9 done flag in STATUSINT bit 7. */
#define ITL_REG_CTRL9      0x0Au
#define ITL_CMD_ACK        0x00u
#define ITL_CMD_RST_FIFO   0x04u
#define ITL_CMD_REQ_FIFO   0x05u
#define ITL_REG_FIFO_WTM   0x13u
#define ITL_FIFO_WTM_VALUE 0x08u
#define ITL_REG_FIFO_CTRL  0x14u
#define ITL_FIFO_CTRL_ON   0x0Au /* Size 64 (bits 3:2 = 10), stream (10). */
#define ITL_REG_FIFO_COUNT 0x15u /* FIFO_SMPL_CNT, then FIFO_STATUS. */
#define ITL_FIFO_MSB_MASK  0x03u
#define ITL_FIFO_OVERFLOW  0x20u
#define ITL_REG_FIFO_DATA  0x17u
#define ITL_REG_STATUSINT  0x2Du
#define ITL_CMD_DONE_BIT   0x80u
#define ITL_REG_STATUS0    0x2Eu
#define ITL_STATUS0_ADA    0x01u
#define ITL_REG_TIMESTAMP  0x30u /* 24-bit sample counter, then TEMP, AX.. */

/* Sizes. */
#define ITL_ACCEL_BYTES    6u
#define ITL_TS_BYTES       3u
#define ITL_TEMP_BYTES     2u
#define ITL_POLL_BYTES     (ITL_TS_BYTES + ITL_TEMP_BYTES + ITL_ACCEL_BYTES)
#define ITL_FIFO_SAMPLES   64u
#define ITL_FIFO_MAX_BYTES (ITL_FIFO_SAMPLES * ITL_ACCEL_BYTES)
#define ITL_FIFO_UNIT      2u /* The count is in 2-byte units. */
#define ITL_BYTE_BITS      8u
#define ITL_TS_MASK        0xFFFFFFu
#define ITL_AXES           3u

/* Scale and peak. */
#define ITL_LSB_PER_G 2048.0f
#define ITL_MG_PER_G  1000.0f
#define ITL_EMA_DIV   500   /* tau 500 ms x 1000 samples/s. */
#define ITL_Q_ONE     65536 /* 1 LSB in the Q16 baseline. */
#define ITL_HOLD_MG   100
#define ITL_TAU_MS    500u

/* Timing. */
#define ITL_US_PER_MS 1000u
#define ITL_WINDOW_MS 10u
#define ITL_WINDOW_US (ITL_WINDOW_MS * ITL_US_PER_MS)
#define ITL_POLL_US   500u
#define ITL_STATS_US  1000000u
#define ITL_RETRY_MS  1000u
#define ITL_WAIT_MS   100u
#define ITL_CMD_POLLS 20u
#define ITL_RANGE_G   16u
#define ITL_ODR_HZ    1000u

/* Board identity and the task. */
#define ITL_NVS_NAMESPACE "board"
#define ITL_NVS_KEY       "number"
#define ITL_ID_SIZE       24u
#define ITL_MAC_BYTES     6u
#define ITL_STACK_BYTES   4096u
#define ITL_TASK_PRIO     5u
#define ITL_TASK_CORE     1

#if ITL_SOURCE_FIFO
#define ITL_TICK_US  ITL_WINDOW_US
#define ITL_SRC_NAME "fifo"
#else
#define ITL_TICK_US  ITL_POLL_US
#define ITL_SRC_NAME "poll"
#endif

static i2c_master_bus_handle_t g_p_bus  = NULL;
static i2c_master_dev_handle_t g_p_dev  = NULL;
static TaskHandle_t            g_p_task = NULL;
static char                    g_board[ITL_ID_SIZE];

/* Sampling state: only the sampling task touches it. */
static int64_t  g_base_q[ITL_AXES]; /* Baseline per axis, Q16 LSB. */
static bool     g_b_seeded  = false;
static int32_t  g_win_peak  = 0;  /* Peak deviation this window, mg. */
static int32_t  g_prev_peak = 0;  /* Peak deviation last window, mg. */
static uint32_t g_samples   = 0u; /* Samples since the last S line. */
static uint32_t g_overruns  = 0u; /* Overruns since the last S line. */
static uint64_t g_stats_us  = 0u; /* Time of the last S line. */

static bool    itl_write_reg (uint8_t reg, uint8_t value);
static bool    itl_read (uint8_t reg, uint8_t * p_buf, size_t len);
static bool    itl_imu_init (void);
static void    itl_board_identity (void);
static void    itl_sample (uint8_t const * p_raw);
static int16_t itl_s16 (uint8_t const * p_raw);
static void    itl_window_end (uint64_t now_us);
static void    itl_stats (uint64_t now_us);
static void    itl_tick (void * p_arg);
static void    itl_task (void * p_arg);
#if ITL_SOURCE_FIFO
static bool itl_ctrl9 (uint8_t cmd);
static bool itl_ctrl9_wait (bool b_done);
static void itl_fifo_drain (uint64_t now_us);
#else
static void itl_poll (uint64_t now_us);
#endif

/**
 * @brief  Entry point: board identity, IMU, then the timer and the sampling
 *         task. While the IMU is missing, E lines once a second.
 */
void app_main (void)
{
    esp_err_t const err = nvs_flash_init();

    if ((ESP_ERR_NVS_NO_FREE_PAGES == err) ||
        (ESP_ERR_NVS_NEW_VERSION_FOUND == err))
    {
        (void)nvs_flash_erase(); /* A failure shows in the init below. */
        (void)nvs_flash_init();  /* The MAC is the fallback if this fails. */
    }

    itl_board_identity();

    while (!itl_imu_init())
    {
        printf("E,imu_not_found\n");
        vTaskDelay(pdMS_TO_TICKS(ITL_RETRY_MS));
    }

    if (pdPASS == xTaskCreatePinnedToCore(itl_task, "itl_sample",
                                          ITL_STACK_BYTES, NULL, ITL_TASK_PRIO,
                                          &g_p_task, ITL_TASK_CORE))
    {
        esp_timer_create_args_t const args  = {.callback = itl_tick,
                                               .name     = "itl_tick"};
        esp_timer_handle_t            timer = NULL;

        if ((ESP_OK == esp_timer_create(&args, &timer)) &&
            (ESP_OK == esp_timer_start_periodic(timer, ITL_TICK_US)))
        {
            g_stats_us = (uint64_t)esp_timer_get_time(); /* Never negative. */
        }
        else
        {
            printf("E,timer_failed\n");
        }
    }
    else
    {
        printf("E,task_failed\n");
    }
}

/**
 * @brief  esp_timer callback (esp_timer task): wake the sampling task.
 * @param  p_arg  Unused.
 */
static void itl_tick (void * p_arg)
{
    (void)p_arg;                     /* No argument. */
    (void)xTaskNotifyGive(g_p_task); /* Always pdPASS. */
}

/**
 * @brief  Sampling task: wait for the timer, take the samples, and print
 *         the T, S and H lines. Blocks between wake-ups.
 * @param  p_arg  Unused.
 */
static void itl_task (void * p_arg)
{
    (void)p_arg; /* No argument. */

    for (;;)
    {
        if (0u != ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(ITL_WAIT_MS)))
        {
            uint64_t const now_us = (uint64_t)esp_timer_get_time(); /* >= 0 */

#if ITL_SOURCE_FIFO
            itl_fifo_drain(now_us);
#else
            itl_poll(now_us);
#endif
            itl_stats(now_us);
        }
    }
}

#if ITL_SOURCE_FIFO
/**
 * @brief  Drain the FIFO into the current window, then end the window. One
 *         drain every 10 ms is one window.
 * @param  now_us  Time of this drain.
 */
static void itl_fifo_drain (uint64_t now_us)
{
    static uint8_t buf[ITL_FIFO_MAX_BYTES];
    uint8_t        count[2] = {0u, 0u};

    if ((itl_ctrl9(ITL_CMD_REQ_FIFO)) &&
        (itl_read(ITL_REG_FIFO_COUNT, count, sizeof(count))))
    {
        /* Widen the count bytes to uint32_t before shifting. */
        uint32_t const msb = (uint32_t)count[1] & ITL_FIFO_MSB_MASK; /* u8 */
        uint32_t bytes = ((msb << ITL_BYTE_BITS) | count[0]) * ITL_FIFO_UNIT;

        if (0u != (count[1] & ITL_FIFO_OVERFLOW))
        {
            g_overruns++;
        }

        if (bytes > ITL_FIFO_MAX_BYTES)
        {
            bytes = ITL_FIFO_MAX_BYTES;
        }

        bytes -= bytes % ITL_ACCEL_BYTES;

        if ((bytes > 0u) && (itl_read(ITL_REG_FIFO_DATA, buf, bytes)))
        {
            for (uint32_t off = 0u; off < bytes; off += ITL_ACCEL_BYTES)
            {
                itl_sample(&buf[off]);
            }
        }
    }

    /* Leave FIFO read mode (CTRL_CMD_REQ_FIFO set it). */
    (void)itl_write_reg(ITL_REG_FIFO_CTRL, ITL_FIFO_CTRL_ON); /* Retried. */
    itl_window_end(now_us);
}

/**
 * @brief  Run one CTRL9 command: send, wait for done, acknowledge, wait for
 *         the flag to clear (as panel_sync/main/orientation.c).
 * @param  cmd  Command code.
 * @return true when the IMU completed it.
 */
static bool itl_ctrl9 (uint8_t cmd)
{
    return (itl_write_reg(ITL_REG_CTRL9, cmd)) && (itl_ctrl9_wait(true)) &&
           (itl_write_reg(ITL_REG_CTRL9, ITL_CMD_ACK)) &&
           (itl_ctrl9_wait(false));
}

/**
 * @brief  Poll the CTRL9 done flag in STATUSINT.
 * @param  b_done  Wait for the flag to be set (true) or clear (false).
 * @return true when it reached that value within ITL_CMD_POLLS reads.
 */
static bool itl_ctrl9_wait (bool b_done)
{
    bool b_ok = false;

    for (uint32_t idx = 0u; (!b_ok) && (idx < ITL_CMD_POLLS); idx++)
    {
        uint8_t status = 0u;

        if (itl_read(ITL_REG_STATUSINT, &status, 1u))
        {
            b_ok = (b_done == (0u != (status & ITL_CMD_DONE_BIT)));
        }
    }

    return b_ok;
}
#else
/**
 * @brief  Data-ready poll: read one sample if STATUS0 says one is ready,
 *         count skipped samples from the sample counter, and end the window
 *         when 10 ms have passed.
 * @param  now_us  Time of this poll.
 */
static void itl_poll (uint64_t now_us)
{
    static uint32_t last_ts   = 0u;
    static bool     b_have_ts = false;
    static uint64_t win_start = 0u;
    uint8_t         status    = 0u;
    uint8_t         raw[ITL_POLL_BYTES];

    if ((itl_read(ITL_REG_STATUS0, &status, 1u)) &&
        (0u != (status & ITL_STATUS0_ADA)) &&
        (itl_read(ITL_REG_TIMESTAMP, raw, sizeof(raw))))
    {
        uint32_t const high = (uint32_t)raw[2]; /* u8 widened to shift. */
        uint32_t const mid  = (uint32_t)raw[1]; /* u8 widened to shift. */
        uint32_t const stamp =
            (high << (2u * ITL_BYTE_BITS)) | (mid << ITL_BYTE_BITS) | raw[0];
        uint32_t const step = (stamp - last_ts) & ITL_TS_MASK;

        if ((b_have_ts) && (step > 1u))
        {
            g_overruns += step - 1u;
        }

        last_ts   = stamp;
        b_have_ts = true;
        itl_sample(&raw[ITL_TS_BYTES + ITL_TEMP_BYTES]);
    }

    if ((now_us - win_start) >= ITL_WINDOW_US)
    {
        itl_window_end(now_us);
        win_start = now_us;
    }
}
#endif

/**
 * @brief  One accelerometer sample: update the baseline (unless held) and
 *         the window's peak deviation.
 * @param  p_raw  6 bytes AX, AY, AZ, little-endian int16.
 */
static void itl_sample (uint8_t const * p_raw)
{
    bool const b_hold =
        (g_win_peak > ITL_HOLD_MG) || (g_prev_peak > ITL_HOLD_MG);
    float sum_sq = 0.0f;

    for (uint32_t axis = 0u; axis < ITL_AXES; axis++)
    {
        /* int16 in Q16, in int64 so the difference below cannot overflow. */
        int64_t const x_q = (int64_t)itl_s16(&p_raw[2u * axis]) * ITL_Q_ONE;

        if (!g_b_seeded)
        {
            g_base_q[axis] = x_q;
        }
        else if (!b_hold)
        {
            g_base_q[axis] += (x_q - g_base_q[axis]) / ITL_EMA_DIV;
        }
        else
        {
            /* Held: a tap is in this or the last window. */
        }

        /* Q16 LSB to LSB as float for the magnitude (|d| < 2^17). */
        float const d_lsb = (float)(x_q - g_base_q[axis]) / (float)ITL_Q_ONE;

        sum_sq += d_lsb * d_lsb;
    }

    g_b_seeded = true;
    g_samples++;

    /* LSB to mg, rounded to the nearest mg; at most about 55,000 mg. */
    int32_t const dev_mg =
        (int32_t)((sqrtf(sum_sq) * ITL_MG_PER_G / ITL_LSB_PER_G) + 0.5f);

    if (dev_mg > g_win_peak)
    {
        g_win_peak = dev_mg;
    }
}

/**
 * @brief  Print the window's T line and start the next window.
 * @param  now_us  End of the window.
 */
static void itl_window_end (uint64_t now_us)
{
    uint64_t const start_ms = (now_us - ITL_WINDOW_US) / ITL_US_PER_MS;

    printf("T,%" PRIu64 ",%" PRId32 "\n", start_ms, g_win_peak);
    g_prev_peak = g_win_peak;
    g_win_peak  = 0;
}

/**
 * @brief  Once a second: the H line, then the S line with the samples and
 *         overruns since the last one.
 * @param  now_us  Now.
 */
static void itl_stats (uint64_t now_us)
{
    if ((now_us - g_stats_us) >= ITL_STATS_US)
    {
        printf("H,board=%s,range_g=%u,odr_hz=%u,window_ms=%u,src=%s,"
               "tau_ms=%u,hold_mg=%d\n",
               g_board, ITL_RANGE_G, ITL_ODR_HZ, ITL_WINDOW_MS, ITL_SRC_NAME,
               ITL_TAU_MS, ITL_HOLD_MG);
        printf("S,%" PRIu64 ",%" PRIu32 ",%" PRIu32 "\n",
               now_us / ITL_US_PER_MS, g_samples, g_overruns);
        g_samples  = 0u;
        g_overruns = 0u;
        g_stats_us = now_us;
    }
}

/**
 * @brief  Little-endian int16 from two bytes.
 * @param  p_raw  Low byte, then high byte.
 * @return The value.
 */
static int16_t itl_s16 (uint8_t const * p_raw)
{
    uint32_t const high = (uint32_t)p_raw[1]; /* u8 widened to shift. */
    uint16_t const bits =
        (uint16_t)((high << ITL_BYTE_BITS) | p_raw[0]); /* 16 bits. */

    return (int16_t)bits; /* Two's complement reinterpretation. */
}

/**
 * @brief  Board identity for the H line: the NVS board number (namespace
 *         "board", key "number", as panel_sync/main/tile_net.c), or the MAC.
 */
static void itl_board_identity (void)
{
    nvs_handle_t handle;
    uint8_t      number = 0u;
    bool         b_ok   = false;

    if (ESP_OK == nvs_open(ITL_NVS_NAMESPACE, NVS_READONLY, &handle))
    {
        b_ok = (ESP_OK == nvs_get_u8(handle, ITL_NVS_KEY, &number));
        nvs_close(handle);
    }

    if (b_ok)
    {
        (void)snprintf(g_board, sizeof(g_board), "%u", number); /* Fits. */
    }
    else
    {
        uint8_t mac[ITL_MAC_BYTES] = {0u};

        (void)esp_read_mac(mac, ESP_MAC_WIFI_STA); /* Zeros if it fails. */
        (void)snprintf(g_board, sizeof(g_board),   /* 17 characters, fits. */
                       "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2],
                       mac[3], mac[4], mac[5]);
    }
}

/**
 * @brief  Create the I2C bus once, probe 0x6A then 0x6B, check WHO_AM_I and
 *         configure the accelerometer (and the FIFO when it is the source).
 * @return true when the IMU answered and was configured.
 */
static bool itl_imu_init (void)
{
    bool b_ok = (NULL != g_p_bus);

    if (!b_ok)
    {
        i2c_master_bus_config_t const bus_cfg = {
            .i2c_port                     = I2C_NUM_0,
            .sda_io_num                   = ITL_SDA_GPIO,
            .scl_io_num                   = ITL_SCL_GPIO,
            .clk_source                   = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt            = ITL_GLITCH_CNT,
            .flags.enable_internal_pullup = true,
        };

        b_ok = (ESP_OK == i2c_new_master_bus(&bus_cfg, &g_p_bus));
    }

    for (uint16_t addr = ITL_ADDR_FIRST;
         (b_ok) && (NULL == g_p_dev) && (addr <= ITL_ADDR_LAST); addr++)
    {
        if (ESP_OK == i2c_master_probe(g_p_bus, addr, ITL_I2C_TIMEOUT_MS))
        {
            i2c_device_config_t const dev_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address  = addr,
                .scl_speed_hz    = ITL_I2C_HZ,
            };

            b_ok = (ESP_OK ==
                    i2c_master_bus_add_device(g_p_bus, &dev_cfg, &g_p_dev));
        }
    }

    uint8_t who = 0u;

    b_ok = (b_ok) && (NULL != g_p_dev) &&
           (itl_read(ITL_REG_WHO_AM_I, &who, 1u)) && (ITL_WHO_AM_I == who) &&
           (itl_write_reg(ITL_REG_CTRL7, ITL_CTRL7_OFF)) &&
           (itl_write_reg(ITL_REG_CTRL1, ITL_CTRL1_VALUE)) &&
           (itl_write_reg(ITL_REG_CTRL2, ITL_CTRL2_VALUE)) &&
           (itl_write_reg(ITL_REG_CTRL8, ITL_CTRL8_VALUE));
#if ITL_SOURCE_FIFO
    b_ok = (b_ok) && (itl_write_reg(ITL_REG_FIFO_WTM, ITL_FIFO_WTM_VALUE)) &&
           (itl_write_reg(ITL_REG_FIFO_CTRL, ITL_FIFO_CTRL_ON)) &&
           (itl_ctrl9(ITL_CMD_RST_FIFO));
#endif

    return (b_ok) && (itl_write_reg(ITL_REG_CTRL7, ITL_CTRL7_ACC_ON));
}

/**
 * @brief  Write one register.
 * @param  reg    Register.
 * @param  value  Value.
 * @return true on success.
 */
static bool itl_write_reg (uint8_t reg, uint8_t value)
{
    uint8_t const bytes[2] = {reg, value};

    return (ESP_OK == i2c_master_transmit(g_p_dev, bytes, sizeof(bytes),
                                          ITL_I2C_TIMEOUT_MS));
}

/**
 * @brief  Read len bytes from reg onwards (auto-increment is on; FIFO_DATA
 *         does not increment).
 * @param  reg    First register.
 * @param  p_buf  Receives the bytes.
 * @param  len    Bytes.
 * @return true on success.
 */
static bool itl_read (uint8_t reg, uint8_t * p_buf, size_t len)
{
    return (ESP_OK == i2c_master_transmit_receive(g_p_dev, &reg, 1u, p_buf, len,
                                                  ITL_I2C_TIMEOUT_MS));
}

/*** end of file ***/
