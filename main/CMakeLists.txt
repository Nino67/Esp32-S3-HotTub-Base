idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES app_controller)

littlefs_create_partition_image(littlefs ../littlefs FLASH_IN_PROJECT)
