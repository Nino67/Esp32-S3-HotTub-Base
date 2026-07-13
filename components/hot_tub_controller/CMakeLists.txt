idf_component_register(SRCS "src/hot_tub_controller.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos 
                                esp_common
                                esp_timer
                                esp_system)


