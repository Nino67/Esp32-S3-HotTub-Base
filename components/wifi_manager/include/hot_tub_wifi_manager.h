#pragma once

#include <stdbool.h>

#include "esp_err.h"

typedef struct
{
    char ssid[33];
    char password[65];
} hot_tub_wifi_credentials_t;

typedef struct
{
    bool sta_connected;
    bool ap_started;
    char sta_ip[16];
    char ap_ip[16];
} hot_tub_wifi_status_t;

esp_err_t hot_tub_wifi_manager_start(const hot_tub_wifi_credentials_t *sta_credentials);
esp_err_t hot_tub_wifi_manager_get_status(hot_tub_wifi_status_t *status);
bool hot_tub_wifi_manager_is_connected(void);
