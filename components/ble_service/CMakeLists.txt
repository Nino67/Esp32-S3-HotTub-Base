idf_component_register(SRCS "src/hot_tub_ble_service.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt device_state esp_common)
