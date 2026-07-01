idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES 
                            app_controller 
                            web_server 
                            device_state 
                            esp_common
                            rgb_led 
                            esp_psram
                            ntp_time_sync
                            json
                            json_service)
    
littlefs_create_partition_image(storage_0 ../littlefs FLASH_IN_PROJECT)
littlefs_create_partition_image(storage_1 ../littlefs FLASH_IN_PROJECT)

