#pragma once

#include <stddef.h>
#include "esp_err.h"
#include "esp_http_server.h"
#include "cJSON.h"
// RPC envelope types and error codes are defined once, in json_service.h.
#include "json_service.h"

esp_err_t web_server_start(void);
esp_err_t web_server_broadcast_json(const char *json);
cJSON *system_status_get_json(void);
