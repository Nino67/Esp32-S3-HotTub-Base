idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES 
                            app_controller 
                            web_server 
                            device_state 
                            esp_common
                            rgb_led 
                            json
                            json_service)
    
littlefs_create_partition_image(littlefs ../littlefs FLASH_IN_PROJECT)

