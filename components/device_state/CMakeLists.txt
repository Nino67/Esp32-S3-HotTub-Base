idf_component_register(SRCS "src/hot_tub_device_state.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos esp_common ntp_time_sync)
