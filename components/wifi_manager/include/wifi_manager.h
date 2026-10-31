#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct
{
    char ssid[33];
    char password[65];
} wifi_credentials_t;

typedef struct
{
    bool sta_connected;
    bool ap_started;
    char sta_ip[16];
    char ap_ip[16];
} wifi_status_t;

typedef void (*wifi_manager_provision_result_cb_t)(uint32_t request_id, esp_err_t result);

esp_err_t wifi_manager_start(const wifi_credentials_t *sta_credentials);
esp_err_t wifi_manager_set_provision_result_callback(wifi_manager_provision_result_cb_t callback);
esp_err_t wifi_manager_provision(const wifi_credentials_t *credentials, uint32_t request_id);
esp_err_t wifi_manager_get_status(wifi_status_t *status);
bool wifi_manager_is_connected(void);
