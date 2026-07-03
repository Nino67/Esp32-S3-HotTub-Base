#pragma once

#include <stdint.h>
#include <stdbool.h>
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
    uint8_t active_partition_slot;   // 0 or 1 for the 2-partition scheme
    uint8_t running_partition_slot;  // Can check if boot matched expected active slot
    ota_state_t current_ota_state;
    uint32_t ota_bytes_written;
    uint32_t ota_total_expected_bytes;
} firmware_status_t;

// --- Master System Status Structure ---
typedef struct {
    // System Identification & Vital Signs
    uint64_t uptime_us;
    float core_0_free_heap;          // Checked frequently on the comms core
    float core_1_free_heap;
    float internal_temperature;       // Built-in ESP32-S3 TSENS
    uint32_t crash_counter;          // Retained via RTC memory if you want to track resets
    
    // Subsystem Structs
    wireless_status_t   network;
    ble_config_status_t bluetooth;
    filesystem_status_t storage;
    webserver_status_t  web_server;
    firmware_status_t   firmware;
    
    // Dynamic Parameter Mirror (For BLE/Web configuration syncing)
    // Put a placeholder here for your custom runtime params
    struct {
        float loop_frequency_hz;
        bool telemetry_enabled;
        uint8_t debug_level;
    } parameters;
} SystemStatus_t;