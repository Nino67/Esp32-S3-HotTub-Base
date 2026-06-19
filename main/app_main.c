#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "hot_tub_app.h"

static const char *TAG = "app_main";

static void hot_tub_app_task(void *arg)
{
    ESP_LOGI(TAG, "Starting Hot Tub Controller on core %d", xPortGetCoreID());
    esp_err_t err = hot_tub_app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hot_tub_app_start failed: %s", esp_err_to_name(err));
    }
    vTaskDelete(NULL);
}

void app_main(void)
{
    xTaskCreatePinnedToCore(hot_tub_app_task,
                            "hot_tub_app",
                            8192,
                            NULL,
                            5,
                            NULL,
                            0);
}
