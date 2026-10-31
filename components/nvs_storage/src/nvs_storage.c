#include "nvs_storage.h"
#include "system_status.h"

#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "nvs_storage";
static const char *NVS_NAMESPACE = "nvs_storage";
static const char *BOOT_FAILURES_KEY = "boot_failures";
static const char *WIFI_CREDENTIALS_KEY = "wifi_cred";

#define WIFI_SSID_STORAGE_SIZE 33
#define WIFI_PASSWORD_STORAGE_SIZE 65
#define WIFI_CREDENTIALS_VERSION 1

typedef struct
{
    uint8_t version;
    char ssid[WIFI_SSID_STORAGE_SIZE];
    char password[WIFI_PASSWORD_STORAGE_SIZE];
} wifi_credentials_record_t;

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


esp_err_t nvs_storage_init(void)
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

esp_err_t nvs_storage_get_wifi_credentials(char *ssid, size_t ssid_size, char *password, size_t password_size)
{
    if (!ssid || !password || ssid_size == 0 || password_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(open_storage(), TAG, "storage not ready");

    size_t record_size = 0;
    esp_err_t err = nvs_get_blob(s_nvs_handle, WIFI_CREDENTIALS_KEY, NULL, &record_size);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        return ESP_ERR_NOT_FOUND;
    }
    if (err != ESP_OK)
    {
        return err;
    }
    if (record_size != sizeof(wifi_credentials_record_t))
    {
        return ESP_ERR_INVALID_SIZE;
    }

    wifi_credentials_record_t record = {0};
    err = nvs_get_blob(s_nvs_handle, WIFI_CREDENTIALS_KEY, &record, &record_size);
    if (err != ESP_OK)
    {
        return err;
    }

    if (record.version != WIFI_CREDENTIALS_VERSION ||
        !memchr(record.ssid, '\0', sizeof(record.ssid)) ||
        !memchr(record.password, '\0', sizeof(record.password)) ||
        record.ssid[0] == '\0')
    {
        return ESP_ERR_INVALID_RESPONSE;
    }

    size_t required_ssid_size = strlen(record.ssid) + 1;
    size_t required_password_size = strlen(record.password) + 1;
    if (ssid_size < required_ssid_size || password_size < required_password_size)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    memcpy(ssid, record.ssid, required_ssid_size);
    memcpy(password, record.password, required_password_size);
    return ESP_OK;
}

esp_err_t nvs_storage_set_wifi_credentials(const char *ssid, const char *password)
{
    if (!ssid || !password)
    {
        return ESP_ERR_INVALID_ARG;
    }

    size_t ssid_length = strnlen(ssid, WIFI_SSID_STORAGE_SIZE);
    size_t password_length = strnlen(password, WIFI_PASSWORD_STORAGE_SIZE);
    if (ssid_length == 0 || ssid_length >= WIFI_SSID_STORAGE_SIZE ||
        password_length >= WIFI_PASSWORD_STORAGE_SIZE)
    {
        return ESP_ERR_INVALID_SIZE;
    }

    wifi_credentials_record_t record = {
        .version = WIFI_CREDENTIALS_VERSION,
    };
    memcpy(record.ssid, ssid, ssid_length + 1);
    memcpy(record.password, password, password_length + 1);

    ESP_RETURN_ON_ERROR(open_storage(), TAG, "storage not ready");
    ESP_RETURN_ON_ERROR(nvs_set_blob(s_nvs_handle, WIFI_CREDENTIALS_KEY, &record, sizeof(record)), TAG, "wifi credentials write failed");
    return nvs_commit(s_nvs_handle);
}


esp_err_t nvs_storage_get_boot_failures(uint32_t *boot_failures)
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

esp_err_t nvs_storage_set_boot_failures(uint32_t boot_failures)
{
    ESP_RETURN_ON_ERROR(open_storage(), TAG, "storage not ready");
    ESP_RETURN_ON_ERROR(nvs_set_u32(s_nvs_handle, BOOT_FAILURES_KEY, boot_failures), TAG, "nvs set failed");
    return nvs_commit(s_nvs_handle);
}

esp_err_t nvs_storage_increment_boot_failures(uint32_t *boot_failures)
{
    uint32_t value = 0;
    ESP_RETURN_ON_ERROR(nvs_storage_get_boot_failures(&value), TAG, "read boot failures failed");
    value += 1;
    ESP_RETURN_ON_ERROR(nvs_storage_set_boot_failures(value), TAG, "write boot failures failed");
    if (boot_failures)
    {
        *boot_failures = value;
    }
    return ESP_OK;
}

esp_err_t nvs_storage_reset_boot_failures(void)
{
    return nvs_storage_set_boot_failures(0);
}
