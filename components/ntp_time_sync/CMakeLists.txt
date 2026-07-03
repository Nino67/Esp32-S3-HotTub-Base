idf_component_register(SRCS "src/ntp_time_sync.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_timer lwip app_watchdog system_status)
