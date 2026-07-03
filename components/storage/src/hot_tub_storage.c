#include "hot_tub_storage.h"
#include "system_status.h"

#include <stdbool.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "hot_tub_storage";
static const char *NVS_NAMESPACE = "hot_tub";
static const char *BOOT_FAILURES_KEY = "boot_failures";

static nvs_handle_t s_nvs_handle;
static bool s_initialized;

static esp_err_t open_storage(void)
{
    if (s_initialized)
    {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
    {
        return err;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK)
    {
        return err;
    }

    s_initialized = true;
    return ESP_OK;
}


esp_err_t hot_tub_storage_init(void)
{
    esp_err_t err = open_storage();
    if (err == ESP_OK) {
        filesystem_status_t fs_status = {
            .is_mounted = true,
            .total_space_bytes = 0,
            .used_space_bytes = 0,
            .open_file_handles = 0,
            .write_error_flag = false,
        };
        system_status_set_filesystem_status(&fs_status);
    }
    return err;
}


esp_err_t hot_tub_storage_get_boot_failures(uint32_t *boot_failures)
{
    if (!boot_failures)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(open_storage(), TAG, "storage not ready");

    esp_err_t err = nvs_get_u32(s_nvs_handle, BOOT_FAILURES_KEY, boot_failures);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        *boot_failures = 0;
        return ESP_OK;
    }

    return err;
}

esp_err_t hot_tub_storage_set_boot_failures(uint32_t boot_failures)
{
    ESP_RETURN_ON_ERROR(open_storage(), TAG, "storage not ready");
    ESP_RETURN_ON_ERROR(nvs_set_u32(s_nvs_handle, BOOT_FAILURES_KEY, boot_failures), TAG, "nvs set failed");
    return nvs_commit(s_nvs_handle);
}

esp_err_t hot_tub_storage_increment_boot_failures(uint32_t *boot_failures)
{
    uint32_t value = 0;
    ESP_RETURN_ON_ERROR(hot_tub_storage_get_boot_failures(&value), TAG, "read boot failures failed");
    value += 1;
    ESP_RETURN_ON_ERROR(hot_tub_storage_set_boot_failures(value), TAG, "write boot failures failed");
    if (boot_failures)
    {
        *boot_failures = value;
    }
    return ESP_OK;
}

esp_err_t hot_tub_storage_reset_boot_failures(void)
{
    return hot_tub_storage_set_boot_failures(0);
}
