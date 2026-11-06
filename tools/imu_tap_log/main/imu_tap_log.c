/**
 * @file    imu_tap_log.c
 * @brief   Tap-feasibility logger for hardware test card H6.
 *
 * QMI8658 accelerometer logger for the Waveshare ESP32-S3-Matrix (target
 * esp32s3). Logs how hard taps on the tile are so tap thresholds can be
 * chosen. See docs/bench/2026-11-06_hardware_testing.md section "H6".
 *
 * IMU setup: CTRL7=0x00 (sensors off), CTRL1=0x40 (address auto-increment,
 * little-endian), CTRL2=0x33 (accelerometer +/-16 g, ODR 1000 Hz: aFS bits
 * 6:4 = 011, aODR bits 3:0 = 0011), CTRL7=0x01 (accelerometer on).
 * +/-16 g is the widest range, so a firm finger tap does not saturate;
 * at +/-16 g the scale is 2048 LSB per g.
 *
 * The sampling task polls STATUS0 bit 0 (accelerometer data ready), reads
 * the 6 bytes from AX_L (AX, AY, AZ, little-endian int16), computes
 * |a| = sqrt(ax^2+ay^2+az^2) in mg (mg = LSB * 1000 / 2048) and keeps the
 * peak of the current 10 ms window (windows aligned to
 * esp_timer_get_time() / 10000). When a window ends it prints exactly one
 * line: T,<ms since boot>,<peak |a| in mg as an integer> (ms = window start).
 *
 * At boot one header line is printed before any T line, for example:
 * H,board=<n or MAC aa:bb:cc:dd:ee:ff>,range_g=16,odr_hz=1000,window_ms=10
 *
 * The mark task reads characters typed in the serial monitor (usb_serial_jtag
 * driver, usb_serial_jtag_read_bytes with a short timeout; \r and \n ignored)
 * and prints MARK,<ms since boot>,<char> for each character.
 *
 * If the IMU is not found, E,imu_not_found is printed once a second and the
 * init is retried.
 *
 * One mutex guards every printf so T and MARK lines never interleave mid-line.
 *
 * Contexts: app_main installs the USB-Serial-JTAG driver, reads the board
 * number from NVS (namespace "board", key "number", u8; the MAC is the
 * fallback), brings up the IMU, prints the header, then starts the sampling
 * and mark tasks. Both tasks take g_print_mutex around each line.
 *
 * DEVIATION D-1 (module-wide): ESP-IDF and FreeRTOS names and types
 * (i2c_master_*, usb_serial_jtag_*, nvs_*, esp_timer_get_time, TickType_t,
 * UBaseType_t, BaseType_t, esp_err_t).
 */

#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"
#include "nvs_flash.h"

/* I2C bus (QMI8658 on Waveshare ESP32-S3-Matrix: SDA GPIO11, SCL GPIO12). */
#define ITL_I2C_PORT  I2C_NUM_0
#define ITL_SDA_GPIO  11u
#define ITL_SCL_GPIO  12u
#define ITL_I2C_HZ    400000u                 /* 400 kHz fast mode */
#define ITL_I2C_TIMEOUT_MS 20u                /* 20 ms I2C timeouts */

/* QMI8658 probe addresses: 0x6A then 0x6B. */
#define ITL_ADDR_FIRST 0x6Au
#define ITL_ADDR_LAST  0x6Bu

/* QMI8658 registers and values. */
#define ITL_REG_WHO_AM_I 0x00u
#define ITL_WHO_AM_I     0x05u                /* must read 0x05 */
#define ITL_REG_CTRL1    0x02u
#define ITL_CTRL1_VALUE  0x40u                /* address auto-increment, little-endian */
#define ITL_REG_CTRL2    0x03u
#define ITL_CTRL2_VALUE  0x33u                /* aFS=011 (+/-16 g), aODR=0011 (1000 Hz) */
#define ITL_REG_CTRL7    0x08u
#define ITL_CTRL7_OFF    0x00u                /* sensors off */
#define ITL_CTRL7_ON     0x01u                /* accelerometer on */
#define ITL_REG_STATUS0  0x2Eu
#define ITL_STATUS0_DRDY_BIT 0x01u            /* accelerometer data ready */
#define ITL_REG_AX_L     0x35u                /* 6 bytes: AX, AY, AZ, little-endian int16 */

/* I2C frame sizes. */
#define ITL_REG_ADDR_BYTES 1u                 /* one-byte register address */
#define ITL_REG_WRITE_BYTES 2u                /* register + value */
#define ITL_ACCEL_BYTES    6u                 /* AX_L..AZ_H */
#define ITL_BYTE_BITS      8u                 /* bits per byte, to join halves */

/* Accelerometer scale: +/-16 g => 2048 LSB per g. */
#define ITL_LSB_PER_G 2048u
#define ITL_MG_PER_G  1000u                   /* 1 g = 1000 mg */

/* Sampling window: 10 ms = 10000 us, aligned to esp_timer_get_time(). */
#define ITL_WINDOW_US 10000u
#define ITL_WINDOW_MS 10u                     /* window start in ms */
#define ITL_POLL_MS   1u                      /* poll period (1000 Hz ODR) */

/* Mark task: USB-Serial-JTAG read timeout. */
#define ITL_MARK_TIMEOUT_MS 10u
#define ITL_US_PER_MS   1000u                 /* us -> ms */

/* E line retry period. */
#define ITL_RETRY_MS 1000u

/* NVS board number: namespace "board", key "number", u8. */
#define ITL_NVS_NAMESPACE "board"
#define ITL_NVS_KEY "number"

/* USB-Serial-JTAG driver buffer sizes. */
#define ITL_USB_RX_SIZE 1024u
#define ITL_USB_TX_SIZE 1024u

/* Tasks. */
#define ITL_SAMPLE_STACK 4096u                /* words: I2C + math + printf */
#define ITL_MARK_STACK   2048u                /* words: read + printf */
#define ITL_PRIO_SAMPLE  5u
#define ITL_PRIO_MARK    4u
#define ITL_AFFINITY_CORE 0u

/* NVS errors handled by erasing the namespace once. */
#define ITL_NVS_ERR_NO_FREE_PAGES  ESP_ERR_NVS_NO_FREE_PAGES
#define ITL_NVS_ERR_NEW_VERSION    ESP_ERR_NVS_NEW_VERSION_FOUND

/* Printed board identity buffer (MAC is 17 chars + NUL). */
#define ITL_BOARD_ID_SIZE 32u

/* One mutex serialises all printf output. */
static SemaphoreHandle_t g_print_mutex = NULL;

/* I2C bus and device, created once by itl_imu_init. */
static i2c_master_bus_handle_t g_p_bus = NULL;
static i2c_master_dev_handle_t g_p_dev = NULL;

/* Board identity read from NVS; the MAC is the fallback. */
static uint8_t  g_board_number = 0u;
static bool     g_board_known  = false;

/**
 * @brief  Write one QMI8658 register (20 ms timeout).
 * @param  reg    Register.
 * @param  value  Value.
 * @return true on success.
 */
static bool itl_write_reg (uint8_t reg, uint8_t value)
{
    uint8_t const bytes[ITL_REG_WRITE_BYTES] = {reg, value};

    return (ESP_OK == i2c_master_transmit(g_p_dev, bytes, sizeof(bytes),
                                          ITL_I2C_TIMEOUT_MS));
}

/**
 * @brief  Poll STATUS0 bit 0 (accelerometer data ready).
 * @param  p_ready  Receives true when a new accelerometer sample is ready.
 * @return true when the STATUS0 register was read successfully.
 */
static bool itl_status_ready (bool * p_ready)
{
    uint8_t        reg  = ITL_REG_STATUS0;
    uint8_t        status = 0u;
    esp_err_t const err   = i2c_master_transmit_receive(
        g_p_dev, &reg, ITL_REG_ADDR_BYTES, &status, 1u, ITL_I2C_TIMEOUT_MS);

    *p_ready = (ESP_OK == err) && (0u != (status & ITL_STATUS0_DRDY_BIT));
    return (ESP_OK == err);
}

/**
 * @brief  Read AX_L..AZ_H (6 bytes, little-endian int16) and return |a| in mg.
 * @param  p_mg  Receives the magnitude in mg.
 * @return true on success.
 */
static bool itl_read_accel_mg (int32_t * p_mg)
{
    uint8_t        reg  = ITL_REG_AX_L;
    uint8_t        raw[ITL_ACCEL_BYTES];
    esp_err_t const err = i2c_master_transmit_receive(
        g_p_dev, &reg, ITL_REG_ADDR_BYTES, raw, sizeof(raw),
        ITL_I2C_TIMEOUT_MS);

    bool b_ok = false;

    if (ESP_OK == err)
    {
        /* Join high:low bytes into a uint16. */
        uint16_t const raw_x =
            (uint16_t)(((uint32_t)raw[1] << ITL_BYTE_BITS) | (uint32_t)raw[0]);
        /* Same 16 bits read as signed. */
        int16_t const ax = (int16_t)raw_x;
        uint16_t const raw_y =
            (uint16_t)(((uint32_t)raw[3] << ITL_BYTE_BITS) | (uint32_t)raw[2]);
        int16_t const ay = (int16_t)raw_y;
        uint16_t const raw_z =
            (uint16_t)(((uint32_t)raw[5] << ITL_BYTE_BITS) | (uint32_t)raw[4]);
        int16_t const az = (int16_t)raw_z;

        /* LSB -> mg at 2048 LSB/g. */
        int32_t const ax_mg = ((int32_t)ax * ITL_MG_PER_G) / ITL_LSB_PER_G;
        int32_t const ay_mg = ((int32_t)ay * ITL_MG_PER_G) / ITL_LSB_PER_G;
        int32_t const az_mg = ((int32_t)az * ITL_MG_PER_G) / ITL_LSB_PER_G;

        /* |a| = sqrt(ax^2+ay^2+az^2) in mg; int64 avoids overflow on the sum. */
        int64_t const sum_sq =
            ((int64_t)ax_mg * (int64_t)ax_mg) +
            ((int64_t)ay_mg * (int64_t)ay_mg) +
            ((int64_t)az_mg * (int64_t)az_mg);
        /* double -> int32: the peak is below 30 000 mg. */
        int32_t const mag_mg = (int32_t)sqrt((double)sum_sq);

        *p_mg = mag_mg;
        b_ok  = true;
    }

    return b_ok;
}

/**
 * @brief  Create the I2C bus once, probe 0x6A then 0x6B, check WHO_AM_I and
 *         configure the accelerometer (off, CTRL1, CTRL2, on).
 * @return true when the IMU answered and was configured.
 */
static bool itl_imu_init (void)
{
    bool b_ok = (NULL != g_p_bus);

    if (!b_ok)
    {
        i2c_master_bus_config_t const bus_cfg = {
            .i2c_port                     = ITL_I2C_PORT,
            .sda_io_num                   = ITL_SDA_GPIO,
            .scl_io_num                   = ITL_SCL_GPIO,
            .clk_source                   = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt            = 7u,
            .flags.enable_internal_pullup = true,
        };

        b_ok = (ESP_OK == i2c_new_master_bus(&bus_cfg, &g_p_bus));
    }

    if ((b_ok) && (NULL == g_p_dev))
    {
        b_ok = false;

        for (uint16_t addr = ITL_ADDR_FIRST; (!b_ok) && (addr <= ITL_ADDR_LAST);
             addr++)
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
    }

    if (b_ok)
    {
        uint8_t         reg = ITL_REG_WHO_AM_I;
        uint8_t         who = 0u;
        esp_err_t const err = i2c_master_transmit_receive(
            g_p_dev, &reg, ITL_REG_ADDR_BYTES, &who, 1u, ITL_I2C_TIMEOUT_MS);

        b_ok = (ESP_OK == err) && (ITL_WHO_AM_I == who) &&
               (itl_write_reg(ITL_REG_CTRL7, ITL_CTRL7_OFF)) &&
               (itl_write_reg(ITL_REG_CTRL1, ITL_CTRL1_VALUE)) &&
               (itl_write_reg(ITL_REG_CTRL2, ITL_CTRL2_VALUE)) &&
               (itl_write_reg(ITL_REG_CTRL7, ITL_CTRL7_ON));
    }

    return b_ok;
}

/**
 * @brief  Read the board number from NVS namespace "board", key "number".
 * @param  p_number  Receives the number.
 * @return true when the key was read successfully.
 */
static bool itl_board_number_read (uint8_t * p_number)
{
    nvs_handle_t handle;
    bool         b_ok = false;

    if (ESP_OK == nvs_open(ITL_NVS_NAMESPACE, NVS_READONLY, &handle))
    {
        esp_err_t const err = nvs_get_u8(handle, ITL_NVS_KEY, p_number);

        nvs_close(handle); /* done with the namespace */
        b_ok = (ESP_OK == err);
    }

    return b_ok;
}

/**
 * @brief  Write the board identity (the NVS number, or the MAC fallback)
 *         into a buffer.
 * @param  p_buf  Destination buffer.
 * @param  size   Buffer size.
 * @return Number of characters written (excluding the NUL terminator).
 */
static int itl_board_identity (char * p_buf, size_t size)
{
    int n = 0;

    if (g_board_known)
    {
        n = snprintf(p_buf, size, "%" PRIu8, g_board_number);
    }
    else
    {
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        n = snprintf(p_buf, size, "%02x:%02x:%02x:%02x:%02x:%02x",
                     mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    return n;
}

/**
 * @brief  Sampling task: poll STATUS0, keep the peak |a| of each 10 ms window
 *         and print one T line when the window ends.
 * @param  p_arg  Unused.
 */
static void itl_sampling_task (void * p_arg)
{
    uint64_t last_win = UINT64_MAX; /* unset until the first sample */
    int32_t  peak_mg  = 0;

    (void)p_arg; /* The task takes no parameter. */

    for (;;)
    {
        uint64_t const now_us = esp_timer_get_time();
        uint64_t const win    = now_us / ITL_WINDOW_US; /* current 10 ms window */

        if (win != last_win)
        {
            if (UINT64_MAX != last_win) /* the previous window ended */
            {
                uint64_t const ms = last_win * ITL_WINDOW_MS; /* window start */

                if (pdTRUE == xSemaphoreTake(g_print_mutex, portMAX_DELAY))
                {
                    /* One complete T line. */
                    printf("T,%" PRIu64 ",%" PRIi32 "\n", ms, peak_mg);
                    xSemaphoreGive(g_print_mutex); /* line complete */
                }
            }

            last_win = win;
            peak_mg  = 0;
        }

        bool ready = false;

        (void)itl_status_ready(&ready); /* best effort: a failed poll skips one sample */

        if (ready)
        {
            int32_t mag_mg = 0;

            if (itl_read_accel_mg(&mag_mg) && (mag_mg > peak_mg))
            {
                peak_mg = mag_mg;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(ITL_POLL_MS));
    }
}

/**
 * @brief  Mark task: read typed characters from USB-Serial-JTAG and print one
 *         MARK line per character (\r and \n ignored).
 * @param  p_arg  Unused.
 */
static void itl_mark_task (void * p_arg)
{
    uint8_t buf[1];

    (void)p_arg; /* The task takes no parameter. */

    for (;;)
    {
        int const len = usb_serial_jtag_read_bytes(buf, sizeof(buf),
                                                   pdMS_TO_TICKS(ITL_MARK_TIMEOUT_MS));

        if (len > 0)
        {
            char const c = (char)buf[0]; /* byte -> char */

            if (('\r' != c) && ('\n' != c))
            {
                uint64_t const ms = esp_timer_get_time() / ITL_US_PER_MS; /* us -> ms */

                if (pdTRUE == xSemaphoreTake(g_print_mutex, portMAX_DELAY))
                {
                    /* One complete MARK line. */
                    printf("MARK,%" PRIu64 ",%c\n", ms, c);
                    xSemaphoreGive(g_print_mutex); /* line complete */
                }
            }
        }
    }
}

/**
 * @brief  Application entry point: console, board identity, IMU, header,
 *         then the sampling and mark tasks.
 */
void app_main (void)
{
    esp_err_t const nvs_err = nvs_flash_init();

    if ((ITL_NVS_ERR_NO_FREE_PAGES == nvs_err) ||
        (ITL_NVS_ERR_NEW_VERSION == nvs_err))
    {
        (void)nvs_flash_erase(); /* Failure shows in the init below. */
        (void)nvs_flash_init();
    }

    /* USB-Serial-JTAG is the board's native console port: install the driver
     * before any output, so E lines and the header are not lost. */
    {
    usb_serial_jtag_driver_config_t usb_cfg = {
        .rx_buffer_size = (int)ITL_USB_RX_SIZE,  /* u32 -> int for the config struct */
        .tx_buffer_size = (int)ITL_USB_TX_SIZE,  /* u32 -> int for the config struct */
    };

    (void)usb_serial_jtag_driver_install(&usb_cfg); /* OK if already installed */
    }

    g_print_mutex = xSemaphoreCreateMutex();

    /* Board number from NVS; the MAC is the fallback. */
    g_board_known = itl_board_number_read(&g_board_number);

    /* Bring up the IMU, printing E,imu_not_found once a second while missing. */
    while (!itl_imu_init())
    {
        if (pdTRUE == xSemaphoreTake(g_print_mutex, portMAX_DELAY))
        {
            printf("E,imu_not_found\n");
            xSemaphoreGive(g_print_mutex); /* line complete */
        }

        vTaskDelay(pdMS_TO_TICKS(ITL_RETRY_MS));
    }

    /* Header before any T line. */
    {
        char board[ITL_BOARD_ID_SIZE];

        itl_board_identity(board, sizeof(board));
        printf("H,board=%s,range_g=16,odr_hz=1000,window_ms=10\n", board);
    }

    (void)xTaskCreatePinnedToCore(itl_sampling_task, "itl_sample",
                                  (uint16_t)ITL_SAMPLE_STACK, /* words -> uint16_t */
                                  NULL,
                                  (UBaseType_t)ITL_PRIO_SAMPLE,
                                  NULL,
                                  (BaseType_t)ITL_AFFINITY_CORE);
    (void)xTaskCreatePinnedToCore(itl_mark_task, "itl_mark",
                                  (uint16_t)ITL_MARK_STACK,   /* words -> uint16_t */
                                  NULL,
                                  (UBaseType_t)ITL_PRIO_MARK,
                                  NULL,
                                  (BaseType_t)ITL_AFFINITY_CORE);
}

/*** end of file ***/
