#include "app_controller.h"

#include "esp_check.h"
#include "esp_log.h"

#include <string.h>

#include "ble_service.h"
#include "app_watchdog.h"
// #include "hot_tub_device_state.h"
#include "ota_manager.h"
#include "nvs_storage.h"
#include "web_server.h"
#include "wifi_manager.h"
#include "wifi_credentials.h"
#include "rgb_led.h"
#include "ntp_time_sync.h"


static const char *TAG = "app_controller";


// Function prototypes
void time_maintenance_task(void *arg);
esp_err_t ntp_time_sync_init(void);



/**
 * @brief Start the Hot Tub application.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t app_start(void)
{
    app_watchdog_config_t watchdog_config;
    app_watchdog_get_default_config(&watchdog_config);
    ESP_RETURN_ON_ERROR(app_watchdog_init(&watchdog_config), TAG, "watchdog init failed");
    ESP_RETURN_ON_ERROR(app_watchdog_register_current_task("hot_tub_app"), TAG, "watchdog task register failed");

    ESP_RETURN_ON_ERROR(nvs_storage_init(), TAG, "storage init failed");
    ESP_RETURN_ON_ERROR(app_watchdog_feed_current_task(), TAG, "watchdog feed failed after storage init");
    // ESP_RETURN_ON_ERROR(hot_tub_device_state_init(), TAG, "device state init failed");
    // ESP_RETURN_ON_ERROR(app_watchdog_feed_current_task(), TAG, "watchdog feed failed after device state init");
    ESP_RETURN_ON_ERROR(ota_manager_note_boot(), TAG, "ota boot check failed");
    ESP_RETURN_ON_ERROR(app_watchdog_feed_current_task(), TAG, "watchdog feed failed after ota boot check");

    wifi_credentials_t credentials = {0};
    if (strlen(WIFI_CREDENTIALS_SSID) > 0)
    {
        strlcpy(credentials.ssid, WIFI_CREDENTIALS_SSID, sizeof(credentials.ssid));
        strlcpy(credentials.password, WIFI_CREDENTIALS_PASSWORD, sizeof(credentials.password));
    }

    ESP_RETURN_ON_ERROR(wifi_manager_start(&credentials), TAG, "wifi start failed");
    ESP_RETURN_ON_ERROR(app_watchdog_feed_current_task(), TAG, "watchdog feed failed after wifi start");
    ESP_RETURN_ON_ERROR(web_server_start(), TAG, "web server start failed");
    ESP_RETURN_ON_ERROR(app_watchdog_feed_current_task(), TAG, "watchdog feed failed after web server start");
    ESP_RETURN_ON_ERROR(ble_service_init(), TAG, "ble start failed");
    ESP_RETURN_ON_ERROR(app_watchdog_feed_current_task(), TAG, "watchdog feed failed after ble init");
    ESP_RETURN_ON_ERROR(ota_manager_mark_app_ready(), TAG, "ota validation failed");
    ESP_RETURN_ON_ERROR(app_watchdog_feed_current_task(), TAG, "watchdog feed failed after app ready");
    ESP_RETURN_ON_ERROR(rgb_led_heartbeat(), TAG, "rgb led heartbeat start failed");
    ESP_RETURN_ON_ERROR(app_watchdog_feed_current_task(), TAG, "watchdog feed failed after rgb heartbeat start");

    ESP_LOGI(TAG, "Hot Tub Controller started successfully");
    return ESP_OK;
}

