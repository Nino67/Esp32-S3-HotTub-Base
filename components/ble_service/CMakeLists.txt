idf_component_register(SRCS "src/ble_service.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt esp_common system_status espressif__cjson wifi_manager)
