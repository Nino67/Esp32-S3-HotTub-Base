idf_component_register(SRCS "src/json_service.c"
                       INCLUDE_DIRS "include"
                       REQUIRES json
                                esp_common
                                web_server)

