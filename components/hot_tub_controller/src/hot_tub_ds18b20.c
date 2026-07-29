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

/**
 * @brief Initialize the DS18B20 temperature sensor.
 *
 * This function initializes the 1-Wire bus and the DS18B20 sensor.
 * It should be called before attempting to read the temperature.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_ds18b20_init(void)
{
    if (s_ds18b20 != NULL) {
        return ESP_OK;
    }

    if (s_onewire_bus == NULL) {
        onewire_bus_config_t bus_config = {
            .bus_gpio_num = GPIO_DS18B20,
            .flags = {
                .en_pull_up = 0,
            },
        };

        onewire_bus_rmt_config_t rmt_config = {
            .max_rx_bytes = 10,
        };

        esp_err_t err = onewire_new_bus_rmt(&bus_config, &rmt_config, &s_onewire_bus);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "onewire_new_bus_rmt failed (%s)", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "1-Wire bus initialized on GPIO%d", GPIO_DS18B20);
    }

    ds18b20_config_t ds_cfg = {};
    esp_err_t err = ds18b20_new_device_from_bus(s_onewire_bus, &ds_cfg, &s_ds18b20);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ds18b20_new_device_from_bus failed (%s)", esp_err_to_name(err));
        hot_tub_ds18b20_cleanup();
        return err;
    }

    ESP_LOGI(TAG, "DS18B20 initialized");
    return ESP_OK;
} // End of hot_tub_ds18b20_init()
//-----------------------------------------------------------------------------


/**
 * @brief Read the current temperature from the DS18B20 sensor.
 *
 * This function triggers a temperature conversion and reads the result.
 * The temperature is returned in degrees Celsius.
 *
 * If the bus or sensor fails, it will reset the bus and retry once.
 *
 * @param temperature Pointer to a float where the temperature will be stored.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_ds18b20_read_temperature(float *temperature)
{
    if (temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_ds18b20 == NULL) {
        esp_err_t err = hot_tub_ds18b20_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    esp_err_t err = ESP_OK;
    for (int attempt = 0; attempt < 2; attempt++) {
        err = ds18b20_trigger_temperature_conversion(s_ds18b20);
        if (err == ESP_OK) {
            err = ds18b20_get_temperature(s_ds18b20, temperature);
        }

        if (err == ESP_OK) {
            return ESP_OK;
        }

        ESP_LOGW(TAG, "DS18B20 read attempt %d failed (%s)", attempt + 1, esp_err_to_name(err));
        hot_tub_ds18b20_cleanup();
        err = hot_tub_ds18b20_init();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "DS18B20 re-init failed (%s)", esp_err_to_name(err));
            return err;
        }
    }

    return err;
}
