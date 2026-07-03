idf_component_register(SRCS "src/hot_tub_wifi_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_wifi esp_event esp_netif lwip device_state esp_common system_status)
