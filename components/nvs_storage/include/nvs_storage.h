#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t nvs_storage_init(void);
esp_err_t nvs_storage_get_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size);
esp_err_t nvs_storage_set_wifi_credentials(const char *ssid, const char *password);
esp_err_t nvs_storage_get_boot_failures(uint32_t *boot_failures);
esp_err_t nvs_storage_set_boot_failures(uint32_t boot_failures);
esp_err_t nvs_storage_increment_boot_failures(uint32_t *boot_failures);
esp_err_t nvs_storage_reset_boot_failures(void);
