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
// #include "json_service.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "nvs_flash.h"
#include "hot_tub_controller.h"

static const char *TAG = "hot_tub_controller";
static const char *NVS_HOTTUB_SETTINGS_NAMESPACE = "hottub_settings";


#define TIME_BUFFER_SIZE 32

// GPIO pin definitions for pump control
#define GPIO_PUMP_LOW 25
#define GPIO_PUMP_HIGH 26
#define PUMP_DEAD_TIME_MS 2000


#define DEFAULT_SETPOINT_TEMP 37.0
#define DEFAULT_HIGH_HYSTERESIS 2.0
#define DEFAULT_LOW_HYSTERESIS 1.0
#define DEFAULT_PUMP_PRE_RUN_TIME 4.0
#define DEFAULT_PUMP_POST_RUN_TIME 5.0
#define DEFAULT_TEMP_UNIT_CELSIUS true

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
void hot_tub_controller_set_pump(pump_state_t targetSpeed); 
float hot_tub_controller_get_pump_pre_run_time(void);
void hot_tub_controller_set_pump_pre_run_time(float time);
float hot_tub_controller_get_pump_post_run_time(void);
void hot_tub_controller_set_pump_post_run_time(float time);
esp_err_t hottub_ctl_to_json(cJSON *json, const HotTubController_t *state);
esp_err_t hot_tub_controller_gpio_set_level(int gpio_num, int level);
esp_err_t hot_tub_controller_settings_load_from_nvs(void);
esp_err_t hot_tub_controller_settings_save_to_nvs(void);
esp_err_t hot_tub_controller_publish_status(void);

    
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

    return ESP_OK;
} // end of hot_tub_controller_init()
//-----------------------------------------------------------------------------




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

    if (!snapshot) { return ESP_ERR_INVALID_ARG; }

    lock_state();
    *snapshot = hottub_ctl;
    unlock_state();

    cJSON *json = cJSON_CreateObject();
    esp_err_t err = hottub_ctl_to_json(json, snapshot);
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



esp_err_t hottub_ctl_to_json(cJSON *json, const HotTubController_t *state)
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

typedef struct {
    bool tempUnitCelsius;
    float setpointTemp;
    float highHysteresis;
    float lowHysteresis;
    float pumpPreRunTime;
    float pumpPostRunTime;
} hotTub_nvs_save_t;



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




// /**
//  * @brief Initialize hot tub control module
//  */
// void hottub_ctl_init(void) {
//     ESP_LOGI(TAG, "Control Initialized");

//     // Create mutex for hot tub control structure
//     hottub_mutex = xSemaphoreCreateMutex();

//     // Create a 1-element queue for the latest sensor temperature.
//     if (s_sensor_temperature_q == NULL) {
//         s_sensor_temperature_q = xQueueCreate(1, sizeof(int32_t));
//         if (s_sensor_temperature_q == NULL) {
//             ESP_LOGE(TAG, "Failed to create sensor temperature queue");
//         }
//     }

//     // Load settings from NVS
//     // get_hottub_nvs_settings();  // Already loaded in main.c

//     ESP_LOGI(TAG, "Hot Tub Settings Loaded: Setpoint=%.2f, Low Hysteresis=%.2f, High Hysteresis=%.2f, Temp Unit Celsius=%d",
//              hottub_ctl.setpoint,
//              hottub_ctl.low_hysteresis,
//              hottub_ctl.high_hysteresis,
//              hottub_ctl.temp_unit_celsius); 

//     vTaskDelay(pdMS_TO_TICKS(1000));         

//     // xTaskCreate(hottub_ctl_task, "hottub_ctl_task", 4096, NULL, configMAX_PRIORITIES - 1, &s_rx_task_handle);
//     xTaskCreatePinnedToCore(
//         hottub_ctl_task,
//         "hottub_ctl_task",
//         4096,
//         NULL,
//         configMAX_PRIORITIES - 10,
//         NULL,
//         1
//     );    

// } // end of hottub_ctl_init()
// /* ***************************************************************************** */











// /**
//  * @brief Hot tub control task
//  * 
//  * @param arg Task argument (unused)
//  * 
//  * @return void
//  * 
//  * Description:
//  * This task runs in an infinite loop, performing the following actions every second:
//  */
// static void hottub_ctl_task(void *arg) {
//     hottub_ctl_t snapshot;
//     ESP_LOGI(TAG, "Hot Tub Control Task Started");
    
//     // Track ownership: Did the auto-controller start the pump for heating?
//     static bool auto_started_pump = false;
    
//     while (1) {
        
//         // Run sim step
//         // run_simulation_step();

//         // Acquire mutex to update state and take a snapshot
//         if (xSemaphoreTake(hottub_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {

//             // Apply latest sensor temperature sample (if any) before hysteresis.
//             // This allows external sensor updates to override the current temperature.
//             if (s_sensor_temperature_q != NULL) {
//                 int32_t rx_temp = 0;
//                 if (xQueueReceive(s_sensor_temperature_q, &rx_temp, 0) == pdTRUE) {
//                     hottub_ctl.temperature = rx_temp;
//                 }
//             }
            
//             // Automatic temperature control logic
//             if(hottub_ctl.auto_temp) {
//                 // Ensure Hysteresis values are safe
//                 int low_hys = (hottub_ctl.low_hysteresis > 0) ? hottub_ctl.low_hysteresis : 1;
//                 int high_hys = (hottub_ctl.high_hysteresis > 0) ? hottub_ctl.high_hysteresis : 1;

//                 // Ensure Delay values are safe
//                 int pre_delay = (hottub_ctl.pre_pump_delay >= 0) ? hottub_ctl.pre_pump_delay : 0;
//                 int post_delay = (hottub_ctl.post_pump_delay >= 0) ? hottub_ctl.post_pump_delay : 0;

//                 bool needs_heat = (hottub_ctl.temperature < hottub_ctl.setpoint - low_hys);
//                 bool heat_satisfied = (hottub_ctl.temperature > hottub_ctl.setpoint + high_hys);

//                 // --- HEATING LOGIC ---
//                 if (needs_heat) {
//                     if (hottub_ctl.heater_on) {
//                          // Already heating, keep going.
//                     } else {
//                         // We need to start heating. Check if pump is running.
//                         bool pump_is_running = (hottub_ctl.pump_state != PUMP_OFF);

//                         if (pump_is_running) {
//                              // Pump is running.
//                              // Logic: If WE started it (auto_started_pump) and timer is ticking, we wait.
//                              //        If USER started it (pump_running check passed but auto_started_pump might be false), 
//                              //        OR if timer is finished, we heat immediately.
                             
//                              if (auto_started_pump && pre_pump_timer > 0) {
//                                   // Wait for our pre-pump timer to finish.
//                                   ESP_LOGD(TAG, "Waiting for pre-pump delay: %d", pre_pump_timer);
//                              } else {
//                                   // Ready to heat.
//                                   // If user started pump manually, 'auto_started_pump' is false. 
//                                   // We turn heater ON and do NOT claim 'auto_started_pump' (so we don't shut it off later).
//                                   // If we started it, timer is 0 now.
//                                   hottub_ctl.heater_on = true;
//                                   ESP_LOGI(TAG, "Heater turned ON");
//                              }
//                         } else {
//                              // Pump is OFF. Start sequence.
//                              hottub_ctl.pump_state = PUMP_LOW; // Start pump
//                              auto_started_pump = true;         // Claim ownership
//                              pre_pump_timer = pre_delay;       // Start delay
//                              ESP_LOGI(TAG, "Pump started for heating. Pre-delay: %d s", pre_delay);
//                         }
//                     }
//                 } 
//                 // --- COOLING / SATISFIED LOGIC ---
//                 else if (heat_satisfied) {
//                     if (hottub_ctl.heater_on) {
//                         // Turn Heater OFF first
//                         hottub_ctl.heater_on = false;
//                         ESP_LOGI(TAG, "Heater turned OFF");

//                         // If we own the pump, engage cooldown.
//                         if (auto_started_pump) {
//                             post_pump_timer = post_delay;
//                             ESP_LOGI(TAG, "Starting post-heat cool down: %d s", post_delay);
//                         } else {
//                             // Manual mode: Leave pump running.
//                             ESP_LOGI(TAG, "Pump left ON (User Manual Mode)");
//                         }
//                     }
//                 }
                
//                 // --- PUMP SHUTDOWN LOGIC (runs every loop) ---
//                 // Shut down the pump if:
//                 // 1. Heater is OFF (safety)
//                 // 2. WE started it (auto_started_pump)
//                 // 3. Post-heat delay has expired (post_pump_timer == 0)
//                 // 4. We do NOT currently need heat (prevents shutdown during pre-heat delay)
//                 if (!hottub_ctl.heater_on && auto_started_pump && post_pump_timer == 0 && !needs_heat && pre_pump_timer == 0) {
//                     hottub_ctl.pump_state = PUMP_OFF;
//                     auto_started_pump = false;
//                     ESP_LOGI(TAG, "Pump turned OFF (Cool down complete)");
//                 }
//                 // Else (In Deadband): Do nothing, maintain state.
                
//                 // --- SAFETY INTERLOCK (runs every loop) ---
//                 // CRITICAL: Ensure pump is NEVER off when heater is on
//                 if (hottub_ctl.heater_on && hottub_ctl.pump_state == PUMP_OFF) {
//                     ESP_LOGE(TAG, "SAFETY VIOLATION: Heater ON with pump OFF! Forcing pump to LOW.");
//                     hottub_ctl.pump_state = PUMP_LOW;
//                     auto_started_pump = true; // Claim ownership for safety
//                 }
//             } else {
//                 // Auto temp is disabled - clean up any auto-started equipment
//                 if (auto_started_pump) {
//                     // Turn off heater if it's on
//                     if (hottub_ctl.heater_on) {
//                         hottub_ctl.heater_on = false;
//                         ESP_LOGI(TAG, "Heater turned OFF (auto_temp disabled)");
//                     }
//                     // Turn off pump if we started it
//                     if (hottub_ctl.pump_state != PUMP_OFF) {
//                         hottub_ctl.pump_state = PUMP_OFF;
//                         ESP_LOGI(TAG, "Pump turned OFF (auto_temp disabled)");
//                     }
//                     auto_started_pump = false;
//                     pre_pump_timer = 0;
//                     post_pump_timer = 0;
//                 }
//             }
            
//             // Decrement timers after logic
//             if (pre_pump_timer > 0) pre_pump_timer--;
//             if (post_pump_timer > 0) post_pump_timer--;
            
//             // Update timestamp
//             struct tm now_time;
//             if (ntp_utils_time_get_local(&now_time) == ESP_OK) {
//                 // Format current time into hottub_ctl.timestamp
//                 if (strftime(hottub_ctl.timestamp, sizeof(hottub_ctl.timestamp), "%Y-%m-%d %H:%M:%S", &now_time) == 0) {
//                     strncpy(hottub_ctl.timestamp, "Failed to format current time", sizeof(hottub_ctl.timestamp) - 1);
//                     hottub_ctl.timestamp[sizeof(hottub_ctl.timestamp) - 1] = '\0';
//                 }
//             } else {
//                 // ESP_LOGW(TAG, "Failed to get local time"); 
//             }

//             // Take a snapshot for JSON generation
//             memcpy(&snapshot, &hottub_ctl, sizeof(hottub_ctl_t));

//             xSemaphoreGive(hottub_mutex);
//         } else {
//             ESP_LOGE(TAG, "Failed to acquire mutex in control task");
//             vTaskDelay(pdMS_TO_TICKS(100));
//             continue;
//         }

//         // Create JSON string with CRC and send over UART (using the snapshot)
//         char *json_str = hottub_ctl_to_json_string(&snapshot);
//         if (json_str != NULL) {
//             // If uart_ctl_send_json() expects a cJSON* object, parse the string back:
//             cJSON *json_to_send = cJSON_Parse(json_str);
//             if (json_to_send != NULL) {
                
// // ***************************************************************************
// // ***************************************************************************






