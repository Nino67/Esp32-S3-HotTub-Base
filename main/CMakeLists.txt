idf_component_register(SRCS "app_main.c"
                       INCLUDE_DIRS "."
                       REQUIRES 
                            app_controller 
                            web_server 
                            esp_common
                            rgb_led 
                            esp_psram
                            ntp_time_sync
                            json
                            json_service
                            psram_allocator
                            system_status)

target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_BINARY_DIR})

# Web assets are gzip-compressed into a staging directory (see tools/stage_www.py) before the
# LittleFS images are built; the web server serves the ".gz" files with Content-Encoding: gzip.
set(WWW_SOURCE_DIR ${CMAKE_SOURCE_DIR}/littlefs)
set(WWW_STAGING_DIR ${CMAKE_BINARY_DIR}/www_staging)
set(WWW_STAGING_STAMP ${CMAKE_BINARY_DIR}/www_staging.stamp)
file(GLOB_RECURSE WWW_SOURCE_FILES CONFIGURE_DEPENDS ${WWW_SOURCE_DIR}/*)
add_custom_command(
    OUTPUT ${WWW_STAGING_STAMP}
    COMMAND ${PYTHON} ${CMAKE_SOURCE_DIR}/tools/stage_www.py ${WWW_SOURCE_DIR} ${WWW_STAGING_DIR} ${WWW_STAGING_STAMP}
    DEPENDS ${WWW_SOURCE_FILES} ${CMAKE_SOURCE_DIR}/tools/stage_www.py
    COMMENT "Staging compressed web assets"
    VERBATIM)

littlefs_create_partition_image(storage_0 ${WWW_STAGING_DIR} FLASH_IN_PROJECT DEPENDS ${WWW_STAGING_STAMP})
littlefs_create_partition_image(storage_1 ${WWW_STAGING_DIR} FLASH_IN_PROJECT DEPENDS ${WWW_STAGING_STAMP})

