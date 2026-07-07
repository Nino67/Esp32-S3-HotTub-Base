#pragma once

#include "esp_err.h"

esp_err_t ble_service_init(void);
esp_err_t ble_service_send_json(const char *json);
