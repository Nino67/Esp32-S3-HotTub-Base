#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "esp_err.h"


typedef struct
{
    int id;
    char time_stamp[32];
    bool wifi_connected;
    bool ble_connected;
    bool ota_pending;
    char ota_status[32];
    int ota_progress;
    char wifi_ip[16];
    size_t last_command_length;
} hot_tub_device_state_t;

esp_err_t hot_tub_device_state_init(void);
esp_err_t hot_tub_device_state_get_snapshot(hot_tub_device_state_t *state);
esp_err_t hot_tub_device_state_format_json(char *buffer, size_t buffer_size);
void hot_tub_device_state_set_wifi_connected(bool connected, const char *ip_address);
void hot_tub_device_state_set_ble_connected(bool connected);
void hot_tub_device_state_set_ota_pending(bool pending);
void hot_tub_device_state_set_ota_status(const char *status);
void hot_tub_device_state_set_ota_progress(int progress);
void hot_tub_device_state_set_last_command(const char *command);
void hot_tub_device_state_set_time_stamp(const struct tm *time_stamp);  
void hot_tub_device_state_set_id(int id);