idf_component_register(SRCS "src/nvs_storage.c"
                       INCLUDE_DIRS "include"
                       REQUIRES nvs_flash esp_common system_status)
