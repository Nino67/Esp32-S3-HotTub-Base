idf_component_register(SRCS "src/app_controller.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "${CMAKE_BINARY_DIR}"
                       REQUIRES 
                                nvs_storage 
                                ota_manager
                                app_watchdog
                                wifi_manager
                                web_server
                                ble_service
                                esp_common
                                rgb_led
                                ntp_time_sync
                                json_service)
