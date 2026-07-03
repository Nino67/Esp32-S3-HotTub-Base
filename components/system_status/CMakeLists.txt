idf_component_register(SRCS "src/system_status.c"
                       INCLUDE_DIRS "include"
                       REQUIRES 
                            esp_common
                            esp_psram 
                            psram_allocator
                            esp_timer
                            esp_system
                            app_update
                            driver
                            espressif__cjson)
