idf_component_register(SRCS "src/hot_tub_ota_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES storage 
                                app_update 
                                esp_common 
                                esp_http_client 
                                esp_https_ota 
                                mbedtls 
                                littlefs
                                rgb_led
                                device_state)
