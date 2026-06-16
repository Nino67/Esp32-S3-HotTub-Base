#include "esp_log.h"

#include "hot_tub_app.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Hot Tub Controller");
    ESP_ERROR_CHECK(hot_tub_app_start());
}
