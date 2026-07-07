idf_component_register(SRCS "src/web_server.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_http_server littlefs esp_common json app_update ota_manager json_service)
