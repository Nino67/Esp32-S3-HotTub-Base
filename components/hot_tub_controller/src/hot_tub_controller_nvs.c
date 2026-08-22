#include "esp_log.h"
#include "esp_err.h"
#include "cJSON.h"
#include "nvs.h"

#include "hot_tub_globals.h"
#include "hot_tub_callbacks.h"
#include "hot_tub_controller.h"
#include "hot_tub_struct_io.h"
#include "hot_tub_controller_nvs.h"

static const char *TAG = "hot_tub_controller_nvs";
static const char *NVS_HOTTUB_SETTINGS_NAMESPACE = "hottub_settings";


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
    
    ESP_LOGI(TAG, "Hot tub settings saved to NVS successfully.");
    ESP_LOGI(TAG, "Current State: safetySwitch=%d, heaterOn=%d, autoMode=%d, tempUnitCelsius=%d, pumpOnLight=%d, heaterOnLight=%d, waterTemp=%.2f, filteredWaterTemp=%.2f, airTemp=%.2f, humidity=%.2f, setpointTemp=%.2f, lowPassFilterAlpha=%.2f, highHysteresis=%.2f, lowHysteresis=%.2f, pumpPreRunTime=%.2f, pumpPostRunTime=%.2f, pumpState=%d, initialStartTime=%s, lastUpdateTime=%s, simulationMode=%d, errorCode=%d",
        current_state.safetySwitch,
        current_state.heaterOn,
        current_state.autoMode,
        current_state.tempUnitCelsius,
        current_state.pumpOnLight,
        current_state.heaterOnLight,
        current_state.waterTemp,
        current_state.filteredWaterTemp,
        current_state.airTemp,
        current_state.humidity,
        current_state.setpointTemp,
        current_state.lowPassFilterAlpha,
        current_state.highHysteresis,
        current_state.lowHysteresis,
        current_state.pumpPreRunTime,
        current_state.pumpPostRunTime,
        current_state.pumpState,
        current_state.initialStartTime,
        current_state.lastUpdateTime,
        current_state.simulationMode,
        current_state.errorCode
    );
    return ESP_OK;

} // end of hot_tub_controller_settings_save_to_nvs()
//-----------------------------------------------------------------------------

esp_err_t hot_tub_struct_io_save_settings_to_nvs(void)
{
    return hot_tub_controller_settings_save_to_nvs();
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


