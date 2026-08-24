#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "hot_tub_globals.h"
#include "hot_tub_callbacks.h"
#include "hot_tub_controller.h"
#include "hot_tub_struct_io.h"
#include "hot_tub_controller_nvs.h"

static const char *TAG = "hot_tub_controller_nvs";
static const char *NVS_HOTTUB_SETTINGS_NAMESPACE = "hottub_settings";

#define PERSISTENCE_FLUSH_INTERVAL_MS 5000
#define PERSISTENCE_TASK_STACK_SIZE 4096
#define PERSISTENCE_TASK_PRIORITY 3

static QueueHandle_t s_persistence_queue;

static void persistence_task(void *arg)
{
    uint8_t dirty_notification;
    bool dirty = false;

    while (true)
    {
        if (xQueueReceive(s_persistence_queue,
                          &dirty_notification,
                          pdMS_TO_TICKS(PERSISTENCE_FLUSH_INTERVAL_MS)) == pdTRUE)
        {
            dirty = true;
            continue;
        }

        if (!dirty)
        {
            continue;
        }

        esp_err_t err = hot_tub_controller_settings_save_to_nvs();
        if (err == ESP_OK)
        {
            dirty = false;
        }
        else
        {
            ESP_LOGE(TAG, "Deferred settings save failed: %s", esp_err_to_name(err));
        }
    }
}

esp_err_t hot_tub_controller_persistence_init(void)
{
    if (s_persistence_queue)
    {
        return ESP_OK;
    }

    s_persistence_queue = xQueueCreate(1, sizeof(uint8_t));
    if (!s_persistence_queue)
    {
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreatePinnedToCore(persistence_task,
                                                 "hottub_persist",
                                                 PERSISTENCE_TASK_STACK_SIZE,
                                                 NULL,
                                                 PERSISTENCE_TASK_PRIORITY,
                                                 NULL,
                                                 CORE_0);
    if (created != pdPASS)
    {
        vQueueDelete(s_persistence_queue);
        s_persistence_queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t hot_tub_controller_persistence_mark_dirty(void)
{
    if (!s_persistence_queue)
    {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t dirty_notification = 1;
    if (xQueueSend(s_persistence_queue, &dirty_notification, 0) != pdTRUE)
    {
        // A queued notification already represents pending state changes.
        return ESP_OK;
    }

    return ESP_OK;
}


/**
 * @brief Save the current hot tub settings to NVS.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_settings_save_to_nvs(void)
{
    // Implement NVS save logic here
    const char *TAG = "hot_tub_controller_nvs";
    const char *nvs_namespace = NVS_HOTTUB_SETTINGS_NAMESPACE;
    const char *nvs_key = "settings";

    HotTubController_t current_state = {0};

    esp_err_t err = hot_tub_controller_snapshot_get(&current_state);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to take snapshot of current state");
        return err;
    }

    nvs_handle_t nvs_handle;
    err = nvs_open(nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle!");
        return err;
    }
    
    err = nvs_set_blob(nvs_handle, nvs_key, &current_state, sizeof(current_state));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write settings to NVS!");
        nvs_close(nvs_handle);
        return err;
    }
    
    err = nvs_commit(nvs_handle);
    nvs_close(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit settings to NVS!");
        return err;
    }
    
    ESP_LOGD(TAG, "Hot tub settings saved to NVS successfully.");
    return ESP_OK;

} // end of hot_tub_controller_settings_save_to_nvs()
//-----------------------------------------------------------------------------

esp_err_t hot_tub_struct_io_save_settings_to_nvs(void)
{
    return hot_tub_controller_persistence_mark_dirty();
}



//-----------------------------------------------------------------------------



/**
 * @brief Load the hot tub settings from NVS.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_settings_load_from_nvs(void)
{
    ESP_LOGI(TAG, "Loading hot tub settings from NVS...");
    const char *nvs_namespace = NVS_HOTTUB_SETTINGS_NAMESPACE;
    const char *nvs_key = "settings";
    HotTubController_t nvs_data;

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(nvs_namespace, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle!");
        return err;
    }
    size_t required_size = sizeof(nvs_data);
    err = nvs_get_blob(nvs_handle, nvs_key, &nvs_data, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read settings from NVS!");
        nvs_close(nvs_handle);
        return err;
    }
    nvs_close(nvs_handle);

    // Apply loaded settings to the controller
    err = hot_tub_controller_snapshot_set(&nvs_data);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to apply loaded settings to controller: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;

} // end of hot_tub_controller_settings_load_from_nvs()
//-----------------------------------------------------------------------------


