idf_component_register(SRCS "src/hot_tub_controller.c" 
                            "src/hot_tub_callbacks.c"
                            "src/hot_tub_struct_io.c"
                            "src/hot_tub_controller_nvs.c"
                            "src/hot_tub_ds18b20.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos 
                                esp_common
                                esp_timer
                                esp_system
                                app_watchdog
                                nvs_flash
                                nvs_storage
                                ntp_time_sync
                                espressif__cjson
                                json_service
                                espressif__onewire_bus
                                espressif__ds18b20)


