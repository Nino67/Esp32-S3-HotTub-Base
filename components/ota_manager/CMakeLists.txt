idf_component_register(SRCS "src/ota_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_storage 
                                app_update 
                                esp_common 
                                esp_http_client 
                                esp_https_ota 
                                mbedtls 
                                littlefs
                                rgb_led
                                json_service)
                                