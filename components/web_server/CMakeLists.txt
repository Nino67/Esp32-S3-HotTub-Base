idf_component_register(SRCS "src/hot_tub_web_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server littlefs device_state esp_common json)
