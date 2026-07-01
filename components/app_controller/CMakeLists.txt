idf_component_register(SRCS "src/hot_tub_app.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "${CMAKE_BINARY_DIR}"
                       REQUIRES 
                                storage 
                                ota_manager
                                app_watchdog
                                wifi_manager
                                web_server
                                ble_service
                                device_state
                                esp_common
                                rgb_led
                                ntp_time_sync
                                json_service)
