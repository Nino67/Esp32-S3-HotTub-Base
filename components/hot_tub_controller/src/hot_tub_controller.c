#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_err.h"
#include "esp_log.h"
// #include "esp_ota_ops.h"
// #include "esp_heap_caps.h"
// #include "esp_psram.h"
// #include "esp_partition.h"
// #include "esp_system.h"
// #include "esp_timer.h"
// #include "driver/temperature_sensor.h"
#include "cJSON.h"
#
#include "json_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"

#include "nvs_flash.h"
#include "hot_tub_globals.h"
#include "hot_tub_callbacks.h"
#include "hot_tub_controller.h"
#include "hot_tub_struct_io.h"


static const char *TAG = "hot_tub_controller";
static const char *NVS_HOTTUB_SETTINGS_NAMESPACE = "hottub_settings";


#define TIME_BUFFER_SIZE 32
#define CORE_0 0
#define CORE_1 1
// GPIO pin definitions for pump control
#define GPIO_PUMP_LOW 25
#define GPIO_PUMP_HIGH 26
#define PUMP_DEAD_TIME_MS 2000

#define DEFAULT_HOTTUB_TIMING_LOOP_DELAY_MS 1000
#define DEFAULT_SETPOINT_TEMP 37.0
#define DEFAULT_HIGH_HYSTERESIS 2.0
#define DEFAULT_LOW_HYSTERESIS 1.0
#define DEFAULT_PUMP_PRE_RUN_TIME 4.0
#define DEFAULT_PUMP_POST_RUN_TIME 5.0
#define DEFAULT_TEMP_UNIT_CELSIUS true


// Function prototypes
// void hottub_ctl_init(void);

esp_err_t hot_tub_controller_init(void);
esp_err_t hot_tub_controller_snapshot(HotTubController_t *state);
bool json_service_register_command(const char *, json_cmd_callback_t, uint8_t );

// External function prototypes from "hot_tub_struct_io.c"
extern bool hot_tub_controller_is_heater_on(void);
extern esp_err_t hot_tub_controller_set_heater_on(bool on);
extern bool hot_tub_controller_is_auto_mode(void);
extern esp_err_t hot_tub_controller_set_auto_mode(bool on);
extern bool hot_tub_controller_is_temp_unit_celsius(void);
extern esp_err_t hot_tub_controller_set_temp_unit_celsius(bool on);
extern bool hot_tub_controller_is_pump_on_light(void);
extern esp_err_t hot_tub_controller_set_pump_on_light(bool on);
extern bool hot_tub_controller_is_heater_on_light(void);
extern esp_err_t hot_tub_controller_set_heater_on_light(bool on);
extern float hot_tub_controller_get_water_temp(void);
extern float hot_tub_controller_get_air_temp(void);
extern float hot_tub_controller_get_humidity(void);
extern float hot_tub_controller_get_setpoint_temp(void);
extern void hot_tub_controller_set_setpoint_temp(float temp);
extern float hot_tub_controller_get_high_hysteresis(void);
extern void hot_tub_controller_set_high_hysteresis(float temp);
extern float hot_tub_controller_get_low_hysteresis(void);
extern void hot_tub_controller_set_low_hysteresis(float temp);
extern pump_state_t hot_tub_controller_pump_state_get(pump_state_t *state);
extern void hot_tub_controller_set_pump(pump_state_t targetSpeed); 
extern float hot_tub_controller_get_pump_pre_run_time(void);
extern void hot_tub_controller_set_pump_pre_run_time(float time);
extern float hot_tub_controller_get_pump_post_run_time(void);
extern void hot_tub_controller_set_pump_post_run_time(float time);

esp_err_t hot_tub_controller_gpio_set_level(gpio_num_t gpio_num, bool level);
esp_err_t hot_tub_controller_settings_load_from_nvs(void);
esp_err_t hot_tub_controller_settings_save_to_nvs(void);
esp_err_t hot_tub_controller_publish_status(void);
esp_err_t hot_tub_controller_to_json(cJSON *json, const HotTubController_t *state);

// JSON service callback functions from "hot_tub_callbacks.c" 
extern void hottub_status_get_callback(cJSON *root);
extern void hottub_auto_mode_get_callback(cJSON *root);
extern void hottub_auto_mode_set_callback(cJSON *root);
extern void hottub_heater_status_get_callback(cJSON *root);
extern void hottub_heater_status_set_callback(cJSON *root);
extern void hottub_temperature_unit_get_callback(cJSON *root);
extern void hottub_temperature_unit_set_callback(cJSON *root);
extern void hottub_water_temperature_get_callback(cJSON *root);
extern void hottub_water_temperature_set_callback(cJSON *root);
extern void hottub_setpoint_temperature_get_callback(cJSON *root);
extern void hottub_setpoint_temperature_set_callback(cJSON *root);
extern void hottub_high_hysteresis_get_callback(cJSON *root);
extern void hottub_high_hysteresis_set_callback(cJSON *root);
extern void hottub_low_hysteresis_get_callback(cJSON *root);
extern void hottub_low_hysteresis_set_callback(cJSON *root);
extern void hottub_pump_state_get_callback(cJSON *root);
extern void hottub_pump_state_set_callback(cJSON *root);
extern void hottub_pump_pre_run_time_get_callback(cJSON *root);
extern void hottub_pump_pre_run_time_set_callback(cJSON *root);
extern void hottub_pump_post_run_time_get_callback(cJSON *root);
extern void hottub_pump_post_run_time_set_callback(cJSON *root);


/**
 * @brief Initialize the hot tub controller.
 *
 * This function initializes the hot tub controller, including setting up
 * the mutex for thread safety, initializing the NVS storage, and loading
 * settings from NVS. If settings are not found in NVS, default values are
 * saved to NVS.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_init(void)
{
    if (!s_mutex) 
    {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) { return ESP_ERR_NO_MEM; }
    }

    lock_state();
    memset(&hottub_ctl, 0, sizeof(hottub_ctl));
    unlock_state();

    // check if NVS is initialized
    if (nvs_flash_init() != ESP_OK) {
        ESP_LOGW(TAG, "NVS not initialized, initializing now...");
        if (nvs_flash_init() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize NVS");
            return ESP_FAIL;
        }
    }

    // Check if settings exist in NVS, if not, save defaults
    if( hot_tub_controller_settings_load_from_nvs() != ESP_OK) {
        ESP_LOGW(TAG, "No settings found in NVS, saving defaults...");

        // Set default values
        hot_tub_controller_set_setpoint_temp(DEFAULT_SETPOINT_TEMP);
        hot_tub_controller_set_high_hysteresis(DEFAULT_HIGH_HYSTERESIS);
        hot_tub_controller_set_low_hysteresis(DEFAULT_LOW_HYSTERESIS);
        hot_tub_controller_set_pump_pre_run_time(DEFAULT_PUMP_PRE_RUN_TIME);
        hot_tub_controller_set_pump_post_run_time(DEFAULT_PUMP_POST_RUN_TIME);
        hot_tub_controller_set_temp_unit_celsius(DEFAULT_TEMP_UNIT_CELSIUS);

        if (hot_tub_controller_settings_save_to_nvs() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save default settings to NVS");
            return ESP_FAIL;
        }
    }

    // Register the "hot_tub_controller" command with the JSON service
    // json_service_register_command("hottub.status.get", hottub_status_get_callback, 0);
    
    hot_tub_controller_register_callbacks();

    return ESP_OK;

} // end of hot_tub_controller_init()
//-----------------------------------------------------------------------------



/**
 * @brief Register the callback functions for the,
 * hot tub controller commands with the JSON service. 
 */
esp_err_t hot_tub_controller_register_callbacks()
{
    // Register the callback for the "hottub.status.get" command
    if (!json_service_register_command("hottub.status.get", hottub_status_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    // Register the callback for the "hottub.automode.get" command
    if (!json_service_register_command("hottub.auto.mode.get", hottub_auto_mode_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    // Register the callback for the "hottub.automode.set" command
    if (!json_service_register_command("hottub.auto.mode.set", hottub_auto_mode_set_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.heater.status.get", hottub_heater_status_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.heater.status.set", hottub_heater_status_set_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.temperature.unit.get", hottub_temperature_unit_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.temperature.unit.set", hottub_temperature_unit_set_callback, CORE_0)) {
        return ESP_FAIL;
    }  
    
    if (!json_service_register_command("hottub.water.temperature.get", hottub_water_temperature_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.setpoint.temperature.get", hottub_setpoint_temperature_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.setpoint.temperature.set", hottub_setpoint_temperature_set_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.high.hysteresis.get", hottub_high_hysteresis_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.high.hysteresis.set", hottub_high_hysteresis_set_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.low.hysteresis.get", hottub_low_hysteresis_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.low.hysteresis.set", hottub_low_hysteresis_set_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.pump.state.get", hottub_pump_state_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.pump.state.set", hottub_pump_state_set_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.pump.pre.run.time.get", hottub_pump_pre_run_time_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.pump.pre.run.time.set", hottub_pump_pre_run_time_set_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.pump.post.run.time.get", hottub_pump_post_run_time_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.pump.post.run.time.set", hottub_pump_post_run_time_set_callback, CORE_0)) {
        return ESP_FAIL;
    }
    return ESP_OK;
} // end of hot_tub_controller_register_callbacks()
//-----------------------------------------------------------------------------


/**
 * @brief Take a snapshot of the current hot tub controller state.
 *
 * @param state Pointer to a HotTubController_t structure to store the snapshot.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_snapshot(HotTubController_t *state)
{
    if (!state) {return ESP_ERR_INVALID_ARG;}
    lock_state();
    *state = hottub_ctl;
    unlock_state();
    // Figure out what to do with *state
    
    return ESP_OK;
} // end of hot_tub_controller_snapshot()
//-----------------------------------------------------------------------------


/**
 * @brief Publish the current hot tub controller status as a JSON object.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_publish_status(void)
{
    HotTubController_t *snapshot = malloc(sizeof(HotTubController_t));

    if (!snapshot) { return ESP_ERR_NO_MEM; }

    lock_state();
    *snapshot = hottub_ctl;
    unlock_state();


    cJSON *json = cJSON_CreateObject();
    esp_err_t err = hot_tub_controller_to_json(json, snapshot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert snapshot to JSON: %s", esp_err_to_name(err));
        cJSON_Delete(json);
        return err;
    }

    if (!json) {
        ESP_LOGE(TAG, "Failed to convert snapshot to JSON");
        return ESP_ERR_NO_MEM;
    }

    char * json_str = cJSON_PrintUnformatted(json);
    if (!json_str) {
        ESP_LOGE(TAG, "Failed to print JSON string");
        cJSON_Delete(json);
        return ESP_ERR_NO_MEM;
    }   
    ESP_LOGW(TAG, "Hot Tub Status JSON: %s", json_str);
    // Free the JSON string after use
    free(json_str);

    // Free the allocated snapshot structure
    free(snapshot);
  
    // Free the allocated JSON object
    cJSON_Delete(json);

    return ESP_OK;
} // end of hot_tub_controller_publish_status()
//-----------------------------------------------------------------------------


/**
 * @brief Convert the hot tub controller state to a JSON object.
 *
 * @param json Pointer to a cJSON object where the state will be stored.
 * @param state Pointer to the hot tub controller state.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_to_json(cJSON *json, const HotTubController_t *state)
{
    if (!json || !state) return ESP_ERR_INVALID_ARG;

    cJSON_AddBoolToObject(json, "heaterOn", state->heaterOn);
    cJSON_AddBoolToObject(json, "autoMode", state->autoMode);
    cJSON_AddBoolToObject(json, "tempUnitCelsius", state->tempUnitCelsius);
    cJSON_AddBoolToObject(json, "pumpOnLight", state->pumpOnLight);
    cJSON_AddBoolToObject(json, "heaterOnLight", state->heaterOnLight);
    
    cJSON_AddNumberToObject(json, "waterTemp", state->waterTemp);
    cJSON_AddNumberToObject(json, "airTemp", state->airTemp);
    cJSON_AddNumberToObject(json, "humidity", state->humidity);
    cJSON_AddNumberToObject(json, "setpointTemp", state->setpointTemp);
    cJSON_AddNumberToObject(json, "highHysteresis", state->highHysteresis);
    cJSON_AddNumberToObject(json, "lowHysteresis", state->lowHysteresis);
    cJSON_AddNumberToObject(json, "pumpPreRunTime", state->pumpPreRunTime);
    cJSON_AddNumberToObject(json, "pumpPostRunTime", state->pumpPostRunTime);
    cJSON_AddStringToObject(json, "lastUpdateTime", asctime(localtime(&state->lastUpdateTime)));
    return ESP_OK;
} // end of hot_tub_controller_to_json()
//-----------------------------------------------------------------------------


/**
 * @brief Get the current pump state.
 *
 * @param state Pointer to a variable where the current pump state will be stored.
 * @return The current pump state (PUMP_OFF, PUMP_LOW, PUMP_HIGH).
 */
pump_state_t hot_tub_controller_pump_state_get(pump_state_t *state) {
    if (!state) return PUMP_OFF; // Return a default value if the pointer is NULL
    lock_state();
    *state = hottub_ctl.pumpState;
    unlock_state();
    return *state;  
}// End of hot_tub_controller_pump_state_get
//-----------------------------------------------------------------------------


/**
 * @brief Set the GPIO level for the specified pin.
 *
 * @param gpio_num The GPIO pin number.
 * @param level The desired level (true for high, false for low).
 * @return ESP_OK on success, or an appropriate error code.
 */
esp_err_t hot_tub_controller_gpio_set_level(gpio_num_t gpio_num, bool level) {
    // Implement GPIO control logic here
    // For example, using the ESP-IDF GPIO API:
    // gpio_set_level(gpio_num, level ? 1 : 0);
    return ESP_OK; // Return appropriate error code if needed
} // End of hot_tub_controller_gpio_set_level
//-----------------------------------------------------------------------------


/**
 * @brief Set the pump hardware to the target speed.
 *
 * @param targetSpeed The desired pump speed (PUMP_OFF, PUMP_LOW, PUMP_HIGH).
 *
 * @note This function ensures safe operation by first turning off both relays,
 * waiting for a dead-time delay, and then engaging the desired speed.
 * ONLY this function should be used to control the pump hardware to avoid damage.
 */
void hot_tub_controller_set_pump(pump_state_t targetSpeed) {
    static pump_state_t currentSpeed = PUMP_OFF;
    
    // If already there, do nothing
    if (targetSpeed == currentSpeed) return;
 
    // ALWAYS kill both relays first (Safe State)
    hot_tub_controller_gpio_set_level(GPIO_PUMP_LOW, 0);
    hot_tub_controller_gpio_set_level(GPIO_PUMP_HIGH, 0);
    
    // Mandatory dead-time delay to let the motor arcs quench
    // (Crucial when switching directly between Low and High)
    if (currentSpeed != PUMP_OFF && targetSpeed != PUMP_OFF) {
        vTaskDelay(pdMS_TO_TICKS(PUMP_DEAD_TIME_MS)); // 1.5 second pause
    }

    // Safely engage the new target
    switch (targetSpeed) {
        case PUMP_LOW:
            hot_tub_controller_gpio_set_level(GPIO_PUMP_LOW, 1);
            break;
        case PUMP_HIGH:
            hot_tub_controller_gpio_set_level(GPIO_PUMP_HIGH, 1);
            break;
        case PUMP_OFF:
        default:
            // Already handled earlier 
            break;
    }
    // Update the current speed state
    currentSpeed = targetSpeed;

} // end of hot_tub_controller_set_pump()
//-----------------------------------------------------------------------------



// /**
//  * @brief Structure to hold the hot tub settings for NVS storage.
//  */
// typedef struct {
//     bool tempUnitCelsius;
//     float setpointTemp;
//     float highHysteresis;
//     float lowHysteresis;
//     float pumpPreRunTime;
//     float pumpPostRunTime;
// } hotTub_nvs_settings_t;



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
    const hotTub_nvs_settings_t nvs_data = {
        .tempUnitCelsius = hot_tub_controller_is_temp_unit_celsius(),
        .setpointTemp = hot_tub_controller_get_setpoint_temp(),
        .highHysteresis = hot_tub_controller_get_high_hysteresis(),
        .lowHysteresis = hot_tub_controller_get_low_hysteresis(),
        .pumpPreRunTime = hot_tub_controller_get_pump_pre_run_time(),
        .pumpPostRunTime = hot_tub_controller_get_pump_post_run_time(),
    };

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(nvs_namespace, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS handle!");
        return err;
    }
    
    err = nvs_set_blob(nvs_handle, nvs_key, &nvs_data, sizeof(nvs_data));
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
  
    return ESP_OK;

} // end of hot_tub_controller_settings_save_to_nvs()
//-----------------------------------------------------------------------------


/**
 * @brief Load the hot tub settings from NVS.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_settings_load_from_nvs(void)
{
    const char *TAG = "hot_tub_controller_nvs";
    ESP_LOGI(TAG, "Loading hot tub settings from NVS...");
    const char *nvs_namespace = NVS_HOTTUB_SETTINGS_NAMESPACE;
    const char *nvs_key = "settings";
    hotTub_nvs_settings_t nvs_data;

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
    hot_tub_controller_set_temp_unit_celsius(nvs_data.tempUnitCelsius);
    hot_tub_controller_set_setpoint_temp(nvs_data.setpointTemp);
    hot_tub_controller_set_high_hysteresis(nvs_data.highHysteresis);
    hot_tub_controller_set_low_hysteresis(nvs_data.lowHysteresis);
    hot_tub_controller_set_pump_pre_run_time(nvs_data.pumpPreRunTime);
    hot_tub_controller_set_pump_post_run_time(nvs_data.pumpPostRunTime);

    return ESP_OK;
} // end of hot_tub_controller_settings_load_from_nvs()
























// typedef struct {
//     char *key_name;
//     cJSON *key_value;
// } param_tupple_t;


// typedef struct {
//     bool heaterOn;
//     bool autoMode;
//     bool tempUnitCelsius;
//     bool pumpOnLight;
//     bool heaterOnLight;
    
//     float waterTemp;
//     float airTemp;
//     float humidity;
//     float setpointTemp;
//     float highHysteresis;
//     float lowHysteresis;
//     float pumpPreRunTime;
//     float pumpPostRunTime;
//     pump_state_t pumpState;
//     time_t lastUpdateTime;
//     sim_mode_t simulationMode;
// } HotTubController_t;
