#pragma once

#include "esp_err.h"

esp_err_t web_server_start(void);
esp_err_t web_server_broadcast_json(const char *json);
