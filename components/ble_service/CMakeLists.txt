idf_component_register(SRCS "src/ble_service.c"
                       INCLUDE_DIRS "include"
                       REQUIRES bt esp_common system_status espressif__cjson ota_manager wifi_manager)
