#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "onewire_bus.h"
#include "ds18b20.h"

#include "hot_tub_globals.h"
#include "hot_tub_ds18b20.h"

static const char *TAG = "hot_tub_ds18b20";
static onewire_bus_handle_t s_onewire_bus = NULL;
static ds18b20_device_handle_t s_ds18b20 = NULL;

static void hot_tub_ds18b20_cleanup(void)
{
    if (s_ds18b20) {
        ds18b20_del_device(s_ds18b20);
        s_ds18b20 = NULL;
    }
    if (s_onewire_bus) {
        onewire_bus_del(s_onewire_bus);
        s_onewire_bus = NULL;
    }
}

esp_err_t hot_tub_ds18b20_init(void)
{
    if (s_ds18b20 != NULL) {
        return ESP_OK;
    }

    onewire_bus_config_t bus_config = {
        .bus_gpio_num = GPIO_DS18B20,
        .flags = {
            .en_pull_up = false, // External 4.7k resistor used
        },
    };

    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10,
    };

    esp_err_t err = onewire_new_bus_rmt(&bus_config, &rmt_config, &s_onewire_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "onewire_new_bus_rmt failed (%s)", esp_err_to_name(err));
        hot_tub_ds18b20_cleanup();
        return err;
    }

    ds18b20_config_t ds_cfg = {};
    err = ds18b20_new_device_from_bus(s_onewire_bus, &ds_cfg, &s_ds18b20);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ds18b20_new_device_from_bus failed (%s)", esp_err_to_name(err));
        hot_tub_ds18b20_cleanup();
        return err;
    }

    err = ds18b20_set_resolution(s_ds18b20, DS18B20_RESOLUTION_10B);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ds18b20_set_resolution failed (%s)", esp_err_to_name(err));
        hot_tub_ds18b20_cleanup();
        return err;
    }

    ESP_LOGI(TAG, "DS18B20 initialized on GPIO%d at 10-bit resolution", GPIO_DS18B20);
    return ESP_OK;
}

esp_err_t hot_tub_ds18b20_read_temperature(float *temperature)
{
    if (temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = ESP_OK;

    for (int attempt = 0; attempt < 2; attempt++) {
        if (s_ds18b20 == NULL) {
            err = hot_tub_ds18b20_init();
            if (err != ESP_OK) {
                // If init fails, wait briefly before retrying
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }
        }

        err = ds18b20_trigger_temperature_conversion(s_ds18b20);
        if (err == ESP_OK) {
            // Give sensor time to calculate temp (187.5ms at 10-bit resolution)
            vTaskDelay(pdMS_TO_TICKS(200));
            err = ds18b20_get_temperature(s_ds18b20, temperature);
        }

        if (err == ESP_OK) {
            return ESP_OK;
        }

        ESP_LOGW(TAG, "DS18B20 read attempt %d failed (%s)", attempt + 1, esp_err_to_name(err));
        
        // Clean up bus handles so we get a fresh state on next loop
        hot_tub_ds18b20_cleanup();
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return err;
}