#include "hot_tub_app.h"

#include "esp_check.h"
#include "esp_log.h"

#include <string.h>

#include "hot_tub_ble_service.h"
#include "hot_tub_device_state.h"
#include "hot_tub_ota_manager.h"
#include "hot_tub_storage.h"
#include "hot_tub_web_server.h"
#include "hot_tub_wifi_manager.h"
#include "wifi_credentials.h"

static const char *TAG = "hot_tub_app";

esp_err_t hot_tub_app_start(void)
{
    ESP_RETURN_ON_ERROR(hot_tub_storage_init(), TAG, "storage init failed");
    ESP_RETURN_ON_ERROR(hot_tub_device_state_init(), TAG, "device state init failed");
    ESP_RETURN_ON_ERROR(hot_tub_ota_manager_note_boot(), TAG, "ota boot check failed");

    hot_tub_wifi_credentials_t credentials = {0};
    if (strlen(WIFI_CREDENTIALS_SSID) > 0)
    {
        strlcpy(credentials.ssid, WIFI_CREDENTIALS_SSID, sizeof(credentials.ssid));
        strlcpy(credentials.password, WIFI_CREDENTIALS_PASSWORD, sizeof(credentials.password));
    }

    ESP_RETURN_ON_ERROR(hot_tub_wifi_manager_start(&credentials), TAG, "wifi start failed");
    ESP_RETURN_ON_ERROR(hot_tub_web_server_start(), TAG, "web server start failed");
    ESP_RETURN_ON_ERROR(hot_tub_ble_service_init(), TAG, "ble start failed");

    ESP_RETURN_ON_ERROR(hot_tub_ota_manager_mark_app_ready(), TAG, "ota validation failed");

    ESP_LOGI(TAG, "Hot Tub Controller started successfully");
    return ESP_OK;
}
