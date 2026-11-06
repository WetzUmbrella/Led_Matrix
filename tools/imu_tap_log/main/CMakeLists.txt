idf_component_register(SRCS "imu_tap_log.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_timer nvs_flash esp_driver_usb_serial_jtag esp_common)
