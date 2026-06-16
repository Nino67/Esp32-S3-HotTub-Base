#pragma once

#include "esp_err.h"

esp_err_t hot_tub_ble_service_init(void);
esp_err_t hot_tub_ble_service_send_json(const char *json);
