cmake_minimum_required(VERSION 3.16)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(IDF_TARGET "esp32s3")

# Generate version header from Git or fallback string.
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --dirty --always
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DESCRIBE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT GIT_DESCRIBE)
        set(GIT_DESCRIBE "0.0.0-dirty")
    endif()
else()
    set(GIT_DESCRIBE "0.0.0-unknown")
endif()

set(APP_VERSION "${GIT_DESCRIBE}")
set(APP_BUILD_DATE "${CMAKE_TIME}")
string(TIMESTAMP APP_BUILD_TIME "%H:%M:%S" UTC)

configure_file("${CMAKE_SOURCE_DIR}/version.h.in"
               "${CMAKE_BINARY_DIR}/version.h"
               @ONLY)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(hot_tub_controller)

# Load local Wi-Fi credentials from wifi_credentials.env when present.
set(WIFI_SSID "")
set(WIFI_PASSWORD "")
set(WIFI_ENV_FILE "${CMAKE_SOURCE_DIR}/wifi_credentials.env")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WIFI_ENV_FILE}")
if(EXISTS "${WIFI_ENV_FILE}")
    file(STRINGS "${WIFI_ENV_FILE}" WIFI_ENV_LINES REGEX "^[^#].*")
    foreach(WIFI_ENV_LINE IN LISTS WIFI_ENV_LINES)
        string(REGEX REPLACE "^[ \t]*([^= \t]+)[ \t]*=[ \t]*(.*)$" "\\1" WIFI_ENV_KEY "${WIFI_ENV_LINE}")
        string(REGEX REPLACE "^[ \t]*([^= \t]+)[ \t]*=[ \t]*(.*)$" "\\2" WIFI_ENV_VALUE "${WIFI_ENV_LINE}")
        string(STRIP "${WIFI_ENV_KEY}" WIFI_ENV_KEY)
        string(STRIP "${WIFI_ENV_VALUE}" WIFI_ENV_VALUE)
        if(WIFI_ENV_KEY STREQUAL "WIFI_SSID")
            set(WIFI_SSID "${WIFI_ENV_VALUE}")
        elseif(WIFI_ENV_KEY STREQUAL "WIFI_PASSWORD")
            set(WIFI_PASSWORD "${WIFI_ENV_VALUE}")
        endif()
    endforeach()
else()
    message(WARNING "wifi_credentials.env not found. STA credentials will be empty and the project may fall back to AP mode.")
endif()

configure_file("${CMAKE_SOURCE_DIR}/wifi_credentials.h.in"
               "${CMAKE_BINARY_DIR}/wifi_credentials.h"
               @ONLY)
include_directories(${CMAKE_BINARY_DIR})

