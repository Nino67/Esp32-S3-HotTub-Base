#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"

#define MAX_SSID_LEN        32
#define MAX_PASSWORD_LEN    64
#define MAX_CLIENTS_COUNT   8

// --- Wireless & Network States ---
typedef enum {
    WIFI_STATE_DISCONNECTED,
    WIFI_STATE_STA_CONNECTING,
    WIFI_STATE_STA_CONNECTED,
    WIFI_STATE_AP_FALLBACK
} wifi_mode_state_t;

typedef struct {
    uint8_t mac_address[6];
    char ip_address[16];
    int8_t rssi;
} wifi_client_info_t;

typedef struct {
    wifi_mode_state_t current_mode;
    char active_ssid[MAX_SSID_LEN];
    bool internet_connected;
    uint32_t bytes_received;
    uint32_t bytes_transmitted;
    
    // AP Mode Stats
    uint8_t connected_client_count;
    wifi_client_info_t clients[MAX_CLIENTS_COUNT];
} wireless_status_t;

// --- Bluetooth (BLE) Config & Security State ---
typedef struct {
    bool is_advertising;
    bool is_paired;
    uint16_t mtu_size;
    uint8_t bonded_devices_count;
    uint32_t last_credential_update_time;
} ble_config_status_t;

// --- Storage & Filesystem (LittleFS) ---
typedef struct {
    bool is_mounted;
    uint32_t total_space_bytes;
    uint32_t used_space_bytes;
    uint16_t open_file_handles;
    bool write_error_flag;
} filesystem_status_t;

// --- Web Server Diagnostics ---
typedef struct {
    bool is_running;
    uint16_t active_connections;
    uint32_t total_requests_served;
    uint32_t internal_500_errors;
    uint32_t file_not_found_404s;
} webserver_status_t;

// --- OTA & Firmware Partition State ---
typedef enum {
    OTA_READY,
    OTA_DOWNLOADING,
    OTA_VERIFYING,
    OTA_FLASHING,
    OTA_FAILED,
    OTA_PENDING_REBOOT
} ota_state_t;

typedef struct {
    char current_version[32];
    char compile_date[16];
    char compile_time[16];
    uint8_t active_partition_slot;
    uint8_t running_partition_slot;
    ota_state_t current_ota_state;
    uint32_t ota_bytes_written;
    uint32_t ota_total_expected_bytes;
} firmware_status_t;

// --- Master System Status Structure ---
typedef struct {
    bool initialized;
    uint64_t uptime_us;
    uint64_t last_update_us;
    size_t core_0_free_heap_bytes;
    size_t core_1_free_heap_bytes;
    size_t psram_free_bytes;
    float internal_temperature;
    uint32_t crash_counter;
    uint32_t reset_reason;
    uint32_t core_0_task_stack_high_water_bytes;

    wireless_status_t   network;
    ble_config_status_t bluetooth;
    filesystem_status_t storage;
    webserver_status_t  web_server;
    firmware_status_t   firmware;

    struct {
        float loop_frequency_hz;
        bool telemetry_enabled;
        uint8_t debug_level;
    } parameters;
} SystemStatus_t;

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t system_status_init(TaskHandle_t core0_task);
esp_err_t system_status_snapshot(void);
esp_err_t system_status_update(void);
SystemStatus_t *system_status_get(void);

char *system_status_get_json();

esp_err_t system_status_set_wireless_status(const wireless_status_t *status);
esp_err_t system_status_set_network_status(const wireless_status_t *status);
esp_err_t system_status_set_firmware_status(const firmware_status_t *status);
esp_err_t system_status_set_filesystem_status(const filesystem_status_t *status);
esp_err_t system_status_set_webserver_status(const webserver_status_t *status);

#ifdef __cplusplus
}
#endif
