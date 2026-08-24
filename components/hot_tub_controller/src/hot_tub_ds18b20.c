#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "app_watchdog.h"

#include "hot_tub_globals.h"
#include "hot_tub_ds18b20.h"

static const char *TAG = "hot_tub_ds18b20";
static onewire_bus_handle_t s_onewire_bus = NULL;
static ds18b20_device_handle_t s_ds18b20 = NULL;


/**
 * @brief Clean up the DS18B20 and OneWire bus handles.
 */
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
//-----------------------------------------------------------------------------


/**
 * @brief Initialize the DS18B20 temperature sensor.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
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



    TaskHandle_t task_handle = NULL;
    BaseType_t result = xTaskCreatePinnedToCore(
                            water_temperature_monitoring_task,
                            "water_temperature_monitoring_task",
                            HOT_TUB_TEMPERATURE_TASK_STACK_SIZE,
                            NULL,
                            HOT_TUB_TEMPERATURE_TASK_PRIORITY,
                            &task_handle,
                            HOT_TUB_TEMPERATURE_TASK_CORE);

    if (result != pdPASS) 
    {
        ESP_LOGE(TAG, "Failed to create water temperature monitoring task");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
//-----------------------------------------------------------------------------


/**
 * @brief Task to monitor the water temperature.
 *
 * This task reads the water temperature from the DS18B20 sensor at regular intervals
 * and updates the hot tub controller state. It also feeds the watchdog to ensure
 * the system remains responsive.
 *
 * @param arg Task argument (not used).
 */
void water_temperature_monitoring_task(void *arg)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = pdMS_TO_TICKS(1000); // 1 second

    if (app_watchdog_register_current_task("water_temp") != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register temperature task with watchdog");
        vTaskDelete(NULL);
        return;
    }

    while (1) 
    {
        float water_temp = 0.0f;
        esp_err_t err = hot_tub_ds18b20_read_temperature(&water_temp);
        
        if (err == ESP_OK) 
        {
            lock_state();
            hottub_ctl.waterTemp = water_temp;
            unlock_state();
        } 
        else 
        {
            ESP_LOGW(TAG, "DS18B20 read failed: %s", esp_err_to_name(err));
        }

        if (app_watchdog_feed_current_task() != ESP_OK)
        {
            ESP_LOGW(TAG, "water temperature monitoring task failed to feed watchdog");
        }

        // Wait for the next cycle
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
} 
//-----------------------------------------------------------------------------



/**
 * @brief Read the temperature from the DS18B20 sensor.
 *
 * @param temperature Pointer to a float where the temperature will be stored.
 * @return ESP_OK on success, or an error code on failure.
 */
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
//-----------------------------------------------------------------------------
