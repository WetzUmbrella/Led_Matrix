idf_component_register(SRCS "imu_tap_log.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_timer nvs_flash esp_hw_support)
