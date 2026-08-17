
/*****************************************************************************/
/*****************************************************************************/
/****    Hot tub controller structs setters and getters                   ****/
/*****************************************************************************/
/*****************************************************************************/

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
#include "json_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "nvs_flash.h"
#include "hot_tub_globals.h"
#include "hot_tub_controller_nvs.h"
#include "hot_tub_struct_io.h"
// #include "hot_tub_callbacks.h"
// #include "hot_tub_controller.h"

static const char *TAG = "hot_tub_struct_io";

SemaphoreHandle_t s_mutex = NULL;
HotTubController_t hottub_ctl = {0};

/**
 * @brief Take a snapshot of the current hot tub controller state.
 *
 * @param state Pointer to a HotTubController_t structure to store the snapshot.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_snapshot_get(HotTubController_t *state)
{
    if (!state) {return ESP_ERR_INVALID_ARG;}
    
    lock_state();
    *state = hottub_ctl;
    unlock_state();
    
    return ESP_OK;
} // end of hot_tub_controller_snapshot_get()
//-----------------------------------------------------------------------------


esp_err_t hot_tub_controller_snapshot_set(const HotTubController_t *state)
{
    if (!state) {return ESP_ERR_INVALID_ARG;}
    
    lock_state();
    hottub_ctl = *state;
    unlock_state();

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
        return ESP_FAIL;
    }
   
    return ESP_OK;
} // end of hot_tub_controller_snapshot_set()





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

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
        return ESP_FAIL;
    }
    ESP_LOGW(TAG, "Heater state set to: %s", on ? "ON" : "OFF");    
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

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
        return ESP_FAIL;
    }

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

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
        return ESP_FAIL;
    }

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
    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
        return ESP_FAIL;
    }

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
 
    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
        return ESP_FAIL;
    }

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

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }

}
//-----------------------------------------------------------------------------

/**
 * @brief Get the current filtered water temperature.
 *
 * @return The current filtered water temperature in degrees.
 */
float hot_tub_controller_get_filtered_water_temp(void)
{
    lock_state();
    float temp = hottub_ctl.filteredWaterTemp;
    unlock_state();
    return temp;
}
//----------------------------------------------------------------------------- 

/**
 * @brief Set the current filtered water temperature.
 *
 * @param temp The new filtered water temperature in degrees.
 */
void hot_tub_controller_set_filtered_water_temp(float temp)
{
    lock_state();
    hottub_ctl.filteredWaterTemp = temp;
    unlock_state();
 
    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
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

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
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

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
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
    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
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

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
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
   
    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
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
 * @brief Set the current pump pre-run time.
 *
 * @param time The new pump pre-run time in seconds.
 */
void hot_tub_controller_set_pump_pre_run_time(float time)
{
    lock_state();
    hottub_ctl.pumpPreRunTime = time;
    unlock_state();
    
    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
}
//-----------------------------------------------------------------------------

/**
 * @brief Get the current pump post-run time.
 *
 * @return The current pump post-run time in seconds.
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
    
    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
}
//-----------------------------------------------------------------------------

/**
 * @brief Set the initial start time of the hot tub controller.
 *
 * @param time_str Pointer to a string representing the initial start time.
 */
void hot_tub_controller_set_initial_start_time(const char *time_str)
{
    lock_state();
    strncpy(hottub_ctl.initialStartTime, time_str, sizeof(hottub_ctl.initialStartTime) - 1);
    hottub_ctl.initialStartTime[sizeof(hottub_ctl.initialStartTime) - 1] = '\0'; // Ensure null-termination
    unlock_state();

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
}
//-----------------------------------------------------------------------------

/**
 * @brief Get the initial start time of the hot tub controller.
 *
 * @param buffer Pointer to a character array where the initial start time will be stored.
 * @param buffer_size Size of the buffer to ensure no overflow occurs.
 */
void hot_tub_controller_get_initial_start_time(char *buffer, size_t buffer_size)
{
    lock_state();
    strncpy(buffer, hottub_ctl.initialStartTime, buffer_size);
    unlock_state();
}
//-----------------------------------------------------------------------------

/**
 * @brief Get the last update time of the hot tub controller state.
 *
 * @param buffer Pointer to a character array where the last update time will be stored.
 * @param buffer_size Size of the buffer to ensure no overflow occurs.
 */
void hot_tub_controller_get_last_update_time(char *buffer, size_t buffer_size)
{
    lock_state();
    strncpy(buffer, hottub_ctl.lastUpdateTime, buffer_size);
    unlock_state();
} 
//----------------------------------------------------------------------------- 

/**
 * @brief Set the last update time of the hot tub controller state.
 *
 * @param time_str Pointer to a string representing the last update time.
 */
void hot_tub_controller_set_last_update_time(const char *time_str)
{
    lock_state();
    strncpy(hottub_ctl.lastUpdateTime, time_str, sizeof(hottub_ctl.lastUpdateTime) - 1);
    hottub_ctl.lastUpdateTime[sizeof(hottub_ctl.lastUpdateTime) - 1] = '\0'; // Ensure null-termination
    unlock_state();
} 
//-----------------------------------------------------------------------------

/**
 * @brief Get the current safety switch state.
 *
 * @return true if the safety switch is on, false otherwise.
 */
safety_switch_t hot_tub_controller_get_safety_switch(void)
{
    lock_state();
    safety_switch_t state = hottub_ctl.safetySwitch;
    unlock_state();
    return state;
}
//-----------------------------------------------------------------------------

/**
 * @brief Set the current safety switch state.
 *
 * @param state The new safety switch state (SAFETY_SWITCH_OFF, SAFETY_SWITCH_ON, etc.).
 */
void hot_tub_controller_set_safety_switch(safety_switch_t state)
{
    lock_state();
    hottub_ctl.safetySwitch = state;
    unlock_state();

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
}
//-----------------------------------------------------------------------------

/**
 * @brief Get the current low-pass filter alpha value.
 *
 * @return The current low-pass filter alpha value.
 */
float hot_tub_controller_get_low_pass_filter_alpha(void)
{
    lock_state();
    float alpha = hottub_ctl.lowPassFilterAlpha;
    unlock_state();
    return alpha;
}
//-----------------------------------------------------------------------------

/**
 * @brief Set the current low-pass filter alpha value.
 *
 * @param alpha The new low-pass filter alpha value.
 */
void hot_tub_controller_set_low_pass_filter_alpha(float alpha)
{
    
    if (alpha < 0.0f || alpha > 1.0f) {
        ESP_LOGE(TAG, "Invalid low-pass filter alpha value: %f. Must be between 0.0 and 1.0. Setting to default %f", alpha, DEFAULT_LOW_PASS_FILTER_ALPHA);
        alpha = DEFAULT_LOW_PASS_FILTER_ALPHA;
    }
    lock_state();
    hottub_ctl.lowPassFilterAlpha = alpha;
    unlock_state();

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
}
//-----------------------------------------------------------------------------


/**
 * @brief Get the current simulation mode.
 *
 * @return The current simulation mode (SIM_MODE_OFF, SIM_MODE_ON, etc.).
 */
sim_mode_t hot_tub_controller_get_simulation_mode(void)
{
    lock_state();
    sim_mode_t mode = hottub_ctl.simulationMode;
    unlock_state();
    return mode;
}
//-----------------------------------------------------------------------------


/**
 * @brief Set the current simulation mode.
 *
 * @param mode The new simulation mode 
 *
 * @note (SIM_NONE, SIM_MANUAL, SIM_PHYSICS, SIM_TRIANGLE).
 */
void hot_tub_controller_set_simulation_mode(sim_mode_t mode)
{
    lock_state();
    hottub_ctl.simulationMode = mode;
    unlock_state();

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
}
//-----------------------------------------------------------------------------


/**
 * @brief Get the current pump state.
 *
 * @return The current pump state (PUMP_OFF, PUMP_LOW, PUMP_HIGH).
 */
int hot_tub_controller_get_error_code(void)
{
    lock_state();
    int error_code = hottub_ctl.errorCode;
    unlock_state();
    return error_code;
}
//-----------------------------------------------------------------------------


/**
 * @brief Set the current error code.
 *
 * @param error_code The new error code to set.
 */
void hot_tub_controller_set_error_code(int error_code)
{
    lock_state();
    hottub_ctl.errorCode = error_code;
    unlock_state();

    if (hot_tub_struct_io_save_settings_to_nvs() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings to NVS");
    }
}
//-----------------------------------------------------------------------------


/**
 * @brief Lock the hot tub controller state for thread-safe access.
 */
void lock_state(void)
{
    if (s_mutex)
    {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
} // End of lock_state
//-----------------------------------------------------------------------------

/**
 * @brief Unlock the hot tub controller state after thread-safe access.
 */
void unlock_state(void)
{
    if (s_mutex)
    {
        xSemaphoreGive(s_mutex);
    }
} // End of unlock_state
//-----------------------------------------------------------------------------

