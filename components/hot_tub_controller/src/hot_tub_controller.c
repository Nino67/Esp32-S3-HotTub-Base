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

#include "nvs_flash.h"
#include "hot_tub_globals.h"
#include "hot_tub_callbacks.h"
#include "hot_tub_controller.h"

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

// if (app_watchdog_feed_current_task() != ESP_OK)


// // Structure to hold the hot tub settings for NVS storage.
// typedef struct {
//     bool tempUnitCelsius;
//     float setpointTemp;
//     float highHysteresis;
//     float lowHysteresis;
//     float pumpPreRunTime;
//     float pumpPostRunTime;
// } hotTub_nvs_save_t;




// gpio_set_level
// // Pump state enumeration
// typedef enum {
//     PUMP_OFF,
//     PUMP_LOW,
//     PUMP_HIGH
// } pump_state_t;


// // Simulation modes
// typedef enum {
//     SIM_NONE,
//     SIM_MANUAL,
//     SIM_PHYSICS,
//     SIM_TRIANGLE
// } sim_mode_t;


// /**
//  * @brief Structure to hold the state of the hot tub controller.
//  */
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


void hottub_ctl_init(void);

static SemaphoreHandle_t s_mutex;
static HotTubController_t hottub_ctl;
static void lock_state(void);
static void unlock_state(void);
bool json_service_register_command(const char *, json_cmd_callback_t, uint8_t );



esp_err_t hot_tub_controller_init(void);
esp_err_t hot_tub_controller_snapshot(HotTubController_t *state);
bool hot_tub_controller_is_heater_on(void);
esp_err_t hot_tub_controller_set_heater_on(bool on);
bool hot_tub_controller_is_auto_mode(void);
esp_err_t hot_tub_controller_set_auto_mode(bool on);
bool hot_tub_controller_is_temp_unit_celsius(void);
esp_err_t hot_tub_controller_set_temp_unit_celsius(bool on);
bool hot_tub_controller_is_pump_on_light(void);
esp_err_t hot_tub_controller_set_pump_on_light(bool on);
bool hot_tub_controller_is_heater_on_light(void);
esp_err_t hot_tub_controller_set_heater_on_light(bool on);
float hot_tub_controller_get_water_temp(void);
void hot_tub_controller_set_water_temp(float temp);
float hot_tub_controller_get_air_temp(void);
void hot_tub_controller_set_air_temp(float temp);
float hot_tub_controller_get_humidity(void);
void hot_tub_controller_set_humidity(float humidity);
float hot_tub_controller_get_setpoint_temp(void);
void hot_tub_controller_set_setpoint_temp(float temp);
float hot_tub_controller_get_high_hysteresis(void);
void hot_tub_controller_set_high_hysteresis(float temp);
float hot_tub_controller_get_low_hysteresis(void);
void hot_tub_controller_set_low_hysteresis(float temp);
pump_state_t hot_tub_controller_pump_state_get(pump_state_t *state);
void hot_tub_controller_set_pump(pump_state_t targetSpeed); 
float hot_tub_controller_get_pump_pre_run_time(void);
void hot_tub_controller_set_pump_pre_run_time(float time);
float hot_tub_controller_get_pump_post_run_time(void);
void hot_tub_controller_set_pump_post_run_time(float time);
esp_err_t hot_tub_controller_gpio_set_level(int gpio_num, int level);
esp_err_t hot_tub_controller_settings_load_from_nvs(void);
esp_err_t hot_tub_controller_settings_save_to_nvs(void);
esp_err_t hot_tub_controller_publish_status(void);
esp_err_t hot_tub_controller_to_json(cJSON *json, const HotTubController_t *state);
// char **hot_tub_controller_split_command_type(const char *command_type);

// esp_err_t hot_tub_controller_register_callbacks();
// static void hottub_callback_response(cJSON *root, cJSON *response);

// static void hottub_auto_mode_get_callback(cJSON *root);
// static void hottub_auto_mode_set_callback(cJSON *root);


// JSON service callback functions 
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




    





//  esp_err_t hot_tub_controller_publish_status(void);


static void lock_state(void)
{
    if (s_mutex)
    {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}


static void unlock_state(void)
{
    if (s_mutex)
    {
        xSemaphoreGive(s_mutex);
    }
}


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

esp_err_t hot_tub_controller_register_callbacks()
{
    // if (!callback) {
    //     return ESP_ERR_INVALID_ARG;
    // }
    
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
}





















esp_err_t hot_tub_controller_snapshot(HotTubController_t *state)
{
    if (!state) {return ESP_ERR_INVALID_ARG;}
    lock_state();
    *state = hottub_ctl;
    unlock_state();
    // Figure out what to do with *state
    
    return ESP_OK;
}


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
}



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
}




esp_err_t hot_tub_controller_gpio_set_level(int gpio_num, int level)
{
    // Implement GPIO control logic here
    // For example, using the ESP-IDF GPIO API:
    // gpio_set_level(gpio_num, level);
    return ESP_OK;
}






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



//    bool heaterOn;
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

// typedef struct {
//     bool tempUnitCelsius;
//     float setpointTemp;
//     float highHysteresis;
//     float lowHysteresis;
//     float pumpPreRunTime;
//     float pumpPostRunTime;
// } hotTub_nvs_save_t;



/**
 * @brief Structure to hold the hot tub settings for NVS storage.
 */
typedef struct {
    bool tempUnitCelsius;
    float setpointTemp;
    float highHysteresis;
    float lowHysteresis;
    float pumpPreRunTime;
    float pumpPostRunTime;
} hotTub_nvs_settings_t;



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


















/*****************************************************************************/
/*****************************************************************************/
/****    Hot tub controller structs setters and getters                   ****/
/*****************************************************************************/
/*****************************************************************************/

/**
 * @brief Get the current heater state.
 *
 * @return true if the heater is on, false otherwise.
 */
bool hot_tub_controller_is_heater_on(void)
{
    lock_state();
    bool heater_on = hottub_ctl.heaterOn;
    unlock_state();
    return heater_on;
} 
//-----------------------------------------------------------------------------



/**
 * @brief Set the heater state.
 *
 * @param on true to turn the heater on, false to turn it off.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_set_heater_on(bool on)
{
    lock_state();
    hottub_ctl.heaterOn = on;
    unlock_state();
    return ESP_OK;
}
//------------------------------------------------------------------------------



/**
 * @brief Get the current auto mode state.
 *
 * @return true if auto mode is on, false otherwise.
 */
bool hot_tub_controller_is_auto_mode(void)
{
    lock_state();
    bool auto_mode = hottub_ctl.autoMode;
    unlock_state();
    return auto_mode;
}
//------------------------------------------------------------------------------



/**
 * @brief Set the auto mode state.
 *
 * @param on true to turn auto mode on, false to turn it off.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_set_auto_mode(bool on)
{
    lock_state();
    hottub_ctl.autoMode = on;
    unlock_state();
    return ESP_OK;
} //-----------------------------------------------------------------------------



/**
 * @brief Get the current temperature unit.
 *
 * @return true if the temperature unit is Celsius, false if Fahrenheit.
 */
bool hot_tub_controller_is_temp_unit_celsius(void)
{
    lock_state();
    bool temp_unit_celsius = hottub_ctl.tempUnitCelsius;
    unlock_state();
    return temp_unit_celsius;
}
//-----------------------------------------------------------------------------



/**
 * @brief Set the temperature unit.
 *
 * @param on true to set the temperature unit to Celsius, false for Fahrenheit.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_set_temp_unit_celsius(bool on)
{
    lock_state();
    hottub_ctl.tempUnitCelsius = on;
    unlock_state();
    return ESP_OK;
}   
//-----------------------------------------------------------------------------


/**
 * @brief Get the current pump on light state.
 *
 * @return true if the pump on light is on, false otherwise.
 */
bool hot_tub_controller_is_pump_on_light(void)
{
    lock_state();
    bool pump_on_light = hottub_ctl.pumpOnLight;
    unlock_state();
    return pump_on_light;
}
//-----------------------------------------------------------------------------



/**
 * @brief Set the pump on light state.
 *
 * @param on true to turn the pump on light on, false to turn it off.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_set_pump_on_light(bool on)
{
    lock_state();
    hottub_ctl.pumpOnLight = on;
    unlock_state();
    return ESP_OK;
} //-----------------------------------------------------------------------------



/**
 * @brief Get the current heater on light state.
 *
 * @return true if the heater on light is on, false otherwise.
 */
bool hot_tub_controller_is_heater_on_light(void)
{
    lock_state();
    bool heater_on_light = hottub_ctl.heaterOnLight;
    unlock_state();
    return heater_on_light;
}
//-----------------------------------------------------------------------------



/**
 * @brief Set the heater on light state.
 *
 * @param on true to turn the heater on light on, false to turn it off.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_set_heater_on_light(bool on)
{
    lock_state();
    hottub_ctl.heaterOnLight = on;
    unlock_state();
    return ESP_OK;
} 
//-----------------------------------------------------------------------------


/**
 * @brief Get the current water temperature.
 *
 * @return The current water temperature in degrees (Celsius or Fahrenheit based on settings).
 */
float hot_tub_controller_get_water_temp(void)
{
    lock_state();
    float temp = hottub_ctl.waterTemp;
    unlock_state();
    return temp;
}
//-----------------------------------------------------------------------------

/**
 * @brief Set the current water temperature.
 *
 * @param temp The new water temperature in degrees (Celsius or Fahrenheit based on settings).
 */
void hot_tub_controller_set_water_temp(float temp)
{
    lock_state();
    hottub_ctl.waterTemp = temp;
    unlock_state();
}
//-----------------------------------------------------------------------------




/**
 * @brief Get the current air temperature.
 *
 * @return The current air temperature in degrees (Celsius or Fahrenheit based on settings).
 */
float hot_tub_controller_get_air_temp(void)
{
    lock_state();
    float temp = hottub_ctl.airTemp;
    unlock_state();
    return temp;
}   
//-----------------------------------------------------------------------------

/**
 * @brief Set the current air temperature.
 *
 * @param temp The new air temperature in degrees (Celsius or Fahrenheit based on settings).
 */
void hot_tub_controller_set_air_temp(float temp)
{
    lock_state();
    hottub_ctl.airTemp = temp;
    unlock_state();
}
//-----------------------------------------------------------------------------




/**
 * @brief Get the current humidity.
 *
 * @return The current humidity as a percentage.
 */
float hot_tub_controller_get_humidity(void)
{
    lock_state();
    float humidity = hottub_ctl.humidity;
    unlock_state();
    return humidity;
}
//-----------------------------------------------------------------------------

/**
 * @brief Set the current humidity.
 *
 * @param humidity The new humidity as a percentage.
 */
void hot_tub_controller_set_humidity(float humidity)
{
    lock_state();
    hottub_ctl.humidity = humidity;
    unlock_state();
}   
//-----------------------------------------------------------------------------




/**
 * @brief Get the current setpoint temperature.
 *
 * @return The current setpoint temperature in degrees (Celsius or Fahrenheit based on settings).
 */
float hot_tub_controller_get_setpoint_temp(void)
{
    lock_state();
    float temp = hottub_ctl.setpointTemp;
    unlock_state();
    return temp;
}  
//----------------------------------------------------------------------------- 

/**
 * @brief Set the current setpoint temperature.
 *
 * @param temp The new setpoint temperature in degrees (Celsius or Fahrenheit based on settings).
 */
void hot_tub_controller_set_setpoint_temp(float temp)
{
    lock_state();
    hottub_ctl.setpointTemp = temp;
    unlock_state();
}   
//-----------------------------------------------------------------------------




/**
 * @brief Get the current high hysteresis value.
 *
 * @return The current high hysteresis value in degrees (Celsius or Fahrenheit based on settings).
 */
float hot_tub_controller_get_high_hysteresis(void)
{
    lock_state();
    float temp = hottub_ctl.highHysteresis;
    unlock_state();
    return temp;
}
//-----------------------------------------------------------------------------

/**
 * @brief Set the current high hysteresis value.
 *
 * @param temp The new high hysteresis value in degrees (Celsius or Fahrenheit based on settings).
 */
void hot_tub_controller_set_high_hysteresis(float temp)
{
    lock_state();
    hottub_ctl.highHysteresis = temp;
    unlock_state();
}
//-----------------------------------------------------------------------------




/**
 * @brief Get the current low hysteresis value.
 *
 * @return The current low hysteresis value in degrees (Celsius or Fahrenheit based on settings).
 */
float hot_tub_controller_get_low_hysteresis(void)
{
    lock_state();
    float temp = hottub_ctl.lowHysteresis;
    unlock_state();
    return temp;
}
//-----------------------------------------------------------------------------

/**
 * @brief Set the current low hysteresis value.
 *
 * @param temp The new low hysteresis value in degrees (Celsius or Fahrenheit based on settings).
 */

void hot_tub_controller_set_low_hysteresis(float temp)
{
    lock_state();
    hottub_ctl.lowHysteresis = temp;
    unlock_state();
}
//-----------------------------------------------------------------------------


/**
 * @brief Get the current pump pre-run time.
 *
 * @return The current pump pre-run time in seconds.
 */
float hot_tub_controller_get_pump_pre_run_time(void)
{
    lock_state();
    float time = hottub_ctl.pumpPreRunTime;
    unlock_state();
    return time;
}
//-----------------------------------------------------------------------------

/**
 * @brief Get the current pump post-run time.
 *
 * @return The current pump post-run time in seconds.
 */
void hot_tub_controller_set_pump_pre_run_time(float time)
{
    lock_state();
    hottub_ctl.pumpPreRunTime = time;
    unlock_state();
}
//-----------------------------------------------------------------------------




/**
 * @brief Set the current pump post-run time.
 *
 * @param time The new pump post-run time in seconds.
 */
float hot_tub_controller_get_pump_post_run_time(void)
{
    lock_state();
    float time = hottub_ctl.pumpPostRunTime;
    unlock_state();
    return time;
}
//-----------------------------------------------------------------------------

/**
 * @brief Set the current pump post-run time.
 *
 * @param time The new pump post-run time in seconds.
 */
void hot_tub_controller_set_pump_post_run_time(float time)
{
    lock_state();
    hottub_ctl.pumpPostRunTime = time;
    unlock_state();
}
//-----------------------------------------------------------------------------






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
