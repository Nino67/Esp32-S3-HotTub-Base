#pragma once

#include "esp_err.h"

esp_err_t ota_manager_note_boot(void);
esp_err_t ota_manager_mark_app_ready(void);
esp_err_t ota_manager_trigger_github_ota(const char *url);
