idf_component_register(SRCS "src/hot_tub_controller.c" "src/hot_tub_callbacks.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos 
                                esp_common
                                esp_timer
                                esp_system
                                nvs_flash
                                espressif__cjson
                                json_service)


