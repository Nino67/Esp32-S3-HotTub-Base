
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
#
#include "json_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "nvs_flash.h"
#include "hot_tub_globals.h"
// #include "hot_tub_callbacks.h"
// #include "hot_tub_controller.h"

static const char *TAG = "hot_tub_struct_io";


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

