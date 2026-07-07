#pragma once

#include <stdint.h>

#include "esp_err.h"

esp_err_t nvs_storage_init(void);
esp_err_t nvs_storage_get_boot_failures(uint32_t *boot_failures);
esp_err_t nvs_storage_set_boot_failures(uint32_t boot_failures);
esp_err_t nvs_storage_increment_boot_failures(uint32_t *boot_failures);
esp_err_t nvs_storage_reset_boot_failures(void);
