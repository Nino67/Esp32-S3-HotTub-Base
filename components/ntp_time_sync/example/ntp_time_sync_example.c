// #include "lvgl.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include <time.h>

#include "ntp_time_sync.h"


static const char *TAG = "ntp_time_sync_example";

// ============================================================================
// Time maintenance task: initial NTP sync, then 1s updates and daily resync.
#define NTP_RESYNC_INTERVAL_SEC   (24 * 60 * 60)   // once per day

static void time_maintenance_task(void *arg)
{
    const char *tz = "EST5EDT,M3.2.0,M11.1.0";   // America/Toronto
    const char *ntp_server = NULL;               // use default in utils
    time_t last_sync = 0;

    ESP_LOGI(TAG, "time_maintenance_task: starting initial NTP sync");
    esp_err_t err = utils_time_sync_blocking(ntp_server, tz, 15000);
    if (err == ESP_OK) {
        time(&last_sync);
        ESP_LOGI(TAG, "time_maintenance_task: initial NTP sync OK");
    } else {
        ESP_LOGW(TAG, "time_maintenance_task: initial NTP sync failed: %s", esp_err_to_name(err));
    }

    while (1) {
        // 1-second tick
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Read current local time without doing NTP again
        struct tm now_tm;
        if (utils_time_get_local(&now_tm) == ESP_OK) {
            char buf[32];
            if (strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &now_tm) > 0) {
                ESP_LOGI("time_task", "Tick time: %s", buf);
            } else {
                ESP_LOGW("time_task", "Failed to format current time");
            }
        }

        // Once per day, resync via NTP to correct drift
        time_t now = 0;
        time(&now);

        if (last_sync == 0 || (now - last_sync) >= NTP_RESYNC_INTERVAL_SEC) {
            ESP_LOGI(TAG, "time_maintenance_task: performing daily NTP resync");
            esp_err_t r = utils_time_sync_blocking(ntp_server, tz, 15000);
            if (r == ESP_OK) {
                last_sync = now;
                ESP_LOGI(TAG, "time_maintenance_task: daily NTP resync OK");
            } else {
                ESP_LOGW(TAG, "time_maintenance_task: daily NTP resync failed: %s", esp_err_to_name(r));
            }
        }
    }
}
// ============================================================================

