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
#include "driver/gpio.h"

#include "nvs_flash.h"
#include "ntp_time_sync.h"
#include "app_watchdog.h"
#include "hot_tub_globals.h"
#include "hot_tub_callbacks.h"
#include "hot_tub_controller.h"
#include "hot_tub_struct_io.h"
#include "hot_tub_ds18b20.h"

static const char *TAG = "hot_tub_controller";

// Function prototypes
extern bool json_service_register_command(const char *, json_cmd_callback_t, uint8_t );
esp_err_t hot_tub_controller_init(void);
void hot_tub_controller_main_task(void *arg);
esp_err_t hot_tub_controller_gpio_set_level(gpio_num_t gpio_num, bool level);
esp_err_t hot_tub_controller_publish_status(void);
esp_err_t hot_tub_controller_to_json(cJSON *json, const HotTubController_t *state);
esp_err_t hot_tub_controller_settings_save_to_nvs(void);
esp_err_t hot_tub_controller_settings_load_from_nvs(void);
esp_err_t hot_tub_controller_snapshot_get(HotTubController_t *);
bool hot_tub_controller_is_heater_on(void);
void hot_tub_controller_set_low_hysteresis(float low_hysteresis);
void hot_tub_controller_set_high_hysteresis(float high_hysteresis);
void hot_tub_controller_set_pump_pre_run_time(float pre_run_time);
void hot_tub_controller_set_pump_post_run_time(float post_run_time);
esp_err_t hot_tub_controller_verify_hysteresis(HotTubController_t *state);
esp_err_t hot_tub_controller_verify_pump_delay_times(HotTubController_t *state);
esp_err_t hot_tub_controller_load_saved_settings(void);
esp_err_t ntp_utils_time_get_local(struct tm *out_time);
static float hottub_controller_temperature_filter(float new_temp, float prev_temp, float alpha);



/**
 * @brief Load saved hot tub settings from NVS or set defaults if not found.
 *
 * This function checks if the NVS is initialized and attempts to load the saved settings
 * for the hot tub controller. If no settings are found, it sets default values and saves them to NVS.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_controller_load_saved_settings(void) {
    // check if NVS is initialized
    if (nvs_flash_init() != ESP_OK) {
        ESP_LOGW(TAG, "NVS not initialized, initializing now...");
        if (nvs_flash_init() != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize NVS");
            return ESP_FAIL;
        }
    }

    // Load settings from NVS, if not found, save default values
    esp_err_t err = hot_tub_controller_settings_load_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No settings found in NVS, saving defaults...");
        // // Set default values
        
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
} // end of hot_tub_controller_load_saved_settings()
//-----------------------------------------------------------------------------


/**
 * @brief Verify that the hysteresis values in the hot tub controller state are within safe limits.
 *
 * @param state Pointer to the HotTubController_t structure containing the current state.
 * @return ESP_OK if the hysteresis values are valid, or an error code if they are not.
 */
esp_err_t hot_tub_controller_verify_hysteresis(HotTubController_t *state) {
    
    if (!state) {
        ESP_LOGE(TAG, "Invalid argument: state is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (state->lowHysteresis < DEFAULT_LOW_HYSTERESIS_MIN || state->lowHysteresis > DEFAULT_LOW_HYSTERESIS_MAX) {
        ESP_LOGW(TAG, "Low hysteresis value %.2f is out of range [%.2f, %.2f], using default %.2f.", state->lowHysteresis, DEFAULT_LOW_HYSTERESIS_MIN, DEFAULT_LOW_HYSTERESIS_MAX, DEFAULT_LOW_HYSTERESIS);
        state->lowHysteresis = DEFAULT_LOW_HYSTERESIS;
    }

    if (state->highHysteresis < DEFAULT_HIGH_HYSTERESIS_MIN || state->highHysteresis > DEFAULT_HIGH_HYSTERESIS_MAX) {
        ESP_LOGW(TAG, "High hysteresis value %.2f is out of range [%.2f, %.2f], using default %.2f.", state->highHysteresis, DEFAULT_HIGH_HYSTERESIS_MIN, DEFAULT_HIGH_HYSTERESIS_MAX, DEFAULT_HIGH_HYSTERESIS);
        state->highHysteresis = DEFAULT_HIGH_HYSTERESIS;
    }

    return ESP_OK;
} // end of hot_tub_controller_verify_hysteresis()
//-----------------------------------------------------------------------------


/**
 * @brief Verify that the pump pre-run and post-run times in the hot tub controller state are within safe limits.
 *
 * @param state Pointer to the HotTubController_t structure containing the current state.
 * @return ESP_OK if the pump delay times are valid, or an error code if they are not.
 */
esp_err_t hot_tub_controller_verify_pump_delay_times(HotTubController_t *state) {
    
    if (!state) {
        ESP_LOGE(TAG, "Invalid argument: state is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (state->pumpPreRunTime < DEFAULT_PUMP_PRE_RUN_TIME_MIN || state->pumpPreRunTime > DEFAULT_PUMP_PRE_RUN_TIME_MAX) {
        ESP_LOGW(TAG, "Pump pre-run time %.2f is out of range [%.2f, %.2f], using default %.2f.", state->pumpPreRunTime, DEFAULT_PUMP_PRE_RUN_TIME_MIN, DEFAULT_PUMP_PRE_RUN_TIME_MAX, DEFAULT_PUMP_PRE_RUN_TIME);
        state->pumpPreRunTime = DEFAULT_PUMP_PRE_RUN_TIME;
    }

    if (state->pumpPostRunTime < DEFAULT_PUMP_POST_RUN_TIME_MIN || state->pumpPostRunTime > DEFAULT_PUMP_POST_RUN_TIME_MAX) {
        ESP_LOGW(TAG, "Pump post-run time %.2f is out of range [%.2f, %.2f], using default %.2f.", state->pumpPostRunTime, DEFAULT_PUMP_POST_RUN_TIME_MIN, DEFAULT_PUMP_POST_RUN_TIME_MAX, DEFAULT_PUMP_POST_RUN_TIME);
        state->pumpPostRunTime = DEFAULT_PUMP_POST_RUN_TIME;
    }

    return ESP_OK;
} // end of hot_tub_controller_verify_pump_delay_times()
//-----------------------------------------------------------------------------


/**
 * @brief Apply a simple low-pass filter to the temperature readings.
 *
 * This function smooths out the temperature readings by applying an exponential moving average filter.
 *
 * @param new_temp The new temperature reading.
 * @param prev_temp The previous filtered temperature value.
 * @param alpha The smoothing factor (0 < alpha < 1). A higher alpha gives more weight to the new reading.
 * @return The filtered temperature value.
 */
static float hottub_controller_temperature_filter(float new_temp, float prev_temp, float alpha) 
{
    return alpha * new_temp + (1.0f - alpha) * prev_temp;
} // end of hottub_controller_temperature_filter()
//-----------------------------------------------------------------------------






/**
 * @brief Main loop for the hot tub controller task.
 *
 * This function runs in a FreeRTOS task and continuously monitors the hot tub's state,
 * controlling the heater and pump based on the current temperature, setpoint, and hysteresis values.
 *
 * @param arg Pointer to any arguments passed to the task (not used).
 * @return ESP_OK on successful execution, or an error code on failure.
 */
void hot_tub_controller_main_task(void *arg)
{
    HotTubController_t snapshot;

    // Track ownership: Did the auto-controller start the pump for heating?
    static bool auto_started_pump = false;

    // Timers for pump delays (in seconds)
    static int pre_pump_timer = 0;
    static int post_pump_timer = 0;
     
    esp_err_t err = ESP_OK;

    while (1) 
    {
        // Clear the snapshot structure
        memset(&snapshot, 0, sizeof(snapshot));

        // Take a snapshot of the current state
        err = hot_tub_controller_snapshot_get(&snapshot);
        if (err != ESP_OK) {
            err = hot_tub_controller_settings_load_from_nvs();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to reload settings from NVS.");
            }
         }   

        // get the current water temperature
        float water_temp = snapshot.waterTemp;
        if (hot_tub_ds18b20_read_temperature(&water_temp) == ESP_OK) {
            snapshot.waterTemp = hottub_controller_temperature_filter(water_temp, snapshot.waterTemp, 0.1f);
            // snapshot.waterTemp = water_temp;
            ESP_LOGI(TAG, "Current water temperature: %.2f", snapshot.waterTemp);
        } else {
            ESP_LOGW(TAG, "DS18B20 read failed, keeping previous waterTemp %.2f", snapshot.waterTemp);
        }

        // --- AUTO TEMPERATURE CONTROL LOGIC ---
        if(snapshot.autoMode) 
        {

            // Verify hysteresis values are within safe limits
            err = hot_tub_controller_verify_hysteresis(&snapshot);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to verify hysteresis: %s", esp_err_to_name(err));
            }
                    
            // Verify pump delay times are within safe limits
            err = hot_tub_controller_verify_pump_delay_times(&snapshot);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to verify pump delay times: %s", esp_err_to_name(err));
            }

            bool needs_heat = (snapshot.waterTemp < snapshot.setpointTemp - snapshot.lowHysteresis);
            bool heat_satisfied = (snapshot.waterTemp > snapshot.setpointTemp + snapshot.highHysteresis);

            // --- HEATING LOGIC ---
            if (needs_heat) 
            {
                if (snapshot.heaterOn) 
                {
                    // Already heating, keep going.
                } 
                else  
                {
                    // We need to start heating. Check if pump is running.
                    if (snapshot.pumpState != PUMP_OFF) 
                    {
                        // Pump is running.
                        // Logic: If WE started it (auto_started_pump) and timer is ticking, we wait.
                        //        If USER started it (pump_running check passed but auto_started_pump might be false), 
                        //        OR if timer is finished, we heat immediately.
                        if (auto_started_pump && pre_pump_timer > 0) 
                        {
                            // Wait for our pre-pump timer to finish.
                            ESP_LOGI(TAG, "Waiting for pre-pump delay: %d", pre_pump_timer);
                        } 
                        else 
                        {
                            // Ready to heat.
                            // If user started pump manually, 'auto_started_pump' is false. 
                            // We turn heater ON and do NOT claim 'auto_started_pump' (so we don't shut it off later).
                            // If we started it, timer is 0 now.
                            snapshot.heaterOn = true;
                            ESP_LOGI(TAG, "Heater turned ON");
                        } // End of if (auto_started_pump && pre_pump_timer > 0)

                    } 
                    else // Pump is OFF. We need to start it first. 
                    {
                        // Pump is OFF. Start sequence.
                        snapshot.pumpState = PUMP_LOW; // Start pump
                        auto_started_pump = true;      // Claim ownership
                        pre_pump_timer = snapshot.pumpPreRunTime;    // Start delay
                        ESP_LOGI(TAG, "Pump started for heating. Pre-delay: %d s", pre_pump_timer);
                    } // End of if (snapshot.heaterOn)
                } // End of if (snapshot.pumpState != PUMP_OFF)
            } // End of if(needs_heat)
            //---------------------------------------------------------------------
            // --- COOLING / SATISFIED LOGIC ---
            else if (heat_satisfied) 
            {
                if (snapshot.heaterOn) 
                {
                    // Turn Heater OFF first
                    snapshot.heaterOn = false;
                    ESP_LOGI(TAG, "Heater turned OFF");

                    // If we own the pump, engage cooldown.
                    if (auto_started_pump) 
                    {
                        post_pump_timer = snapshot.pumpPostRunTime;
                        ESP_LOGI(TAG, "Starting post-heat cool down: %d s", snapshot.pumpPostRunTime);
                    } 
                    else 
                    {
                        // Manual mode: Leave pump running.
                        ESP_LOGI(TAG, "Pump left ON (User Manual Mode)");
                    }
                }
            } // End of else if(heat_satisfied) 
            //---------------------------------------------------------------------
            // --- PUMP SHUTDOWN LOGIC (runs every loop) ---
            // Shut down the pump if:
            // 1. Heater is OFF (safety)
            // 2. WE started it (auto_started_pump)
            // 3. Post-heat delay has expired (post_pump_timer == 0)
            // 4. We do NOT currently need heat (prevents shutdown during pre-heat delay)
            if (!snapshot.heaterOn && auto_started_pump && post_pump_timer == 0 && !needs_heat && pre_pump_timer == 0) 
            {
                snapshot.pumpState = PUMP_OFF;
                auto_started_pump = false;
                ESP_LOGI(TAG, "Pump turned OFF (Cool down complete)");
            }
            // Else (In Deadband): Do nothing, maintain state.
            
            // --- SAFETY INTERLOCK (runs every loop) ---
            // CRITICAL: Ensure pump is NEVER off when heater is on
            if (snapshot.heaterOn && snapshot.pumpState == PUMP_OFF) 
            {
                ESP_LOGE(TAG, "SAFETY VIOLATION: Heater ON with pump OFF! Forcing pump to LOW.");
                snapshot.pumpState = PUMP_LOW;
                auto_started_pump = true; // Claim ownership for safety
            }
        } 
        else // Else of if(snapshot.autoMode) 
        {
            // Auto temp is disabled - clean up any auto-started equipment
            if (auto_started_pump) 
            {
                // Turn off heater if it's on
                if (snapshot.heaterOn) 
                {
                    snapshot.heaterOn = false;
                    ESP_LOGI(TAG, "Heater turned OFF (auto_temp disabled)");
                }
                // Turn off pump if we started it
                if (snapshot.pumpState != PUMP_OFF) 
                {
                    snapshot.pumpState = PUMP_OFF;
                    ESP_LOGI(TAG, "Pump turned OFF (auto_temp disabled)");
                }
                auto_started_pump = false;
                pre_pump_timer = 0;
                post_pump_timer = 0;

            } // End of if(auto_started_pump)
        } // End of else (autoMode disabled)
        
        // Decrement timers after logic 
        // { later on need to isolate and actually use seconds }
        if (pre_pump_timer > 0) pre_pump_timer--;
        if (post_pump_timer > 0) post_pump_timer--;
        
        // if (ntp_utils_time_get_local(&now_tm) == ESP_OK) {

        // // Update timestamp
        // struct tm now_time;
        // if (ntp_utils_time_get_local(&now_time) == ESP_OK) 
        // {
        //     // Convert struct tm to time_t
        //     time_t now_epoch = mktime(&now_time);
        //     snapshot.lastUpdateTime = now_epoch;
        // } 
        // else 
        // {
        //     ESP_LOGW(TAG, "Failed to get local time"); 
        //     snapshot.lastUpdateTime = time(NULL); // Fallback to system time
        // }

        // Save the updated snapshot back to the controller state
        err = hot_tub_controller_snapshot_set(&snapshot);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save hot tub controller snapshot: %s", esp_err_to_name(err));
        }

        // // Call to update the GPIOs based on the new state
        // err = hot_tub_controller_gpio_update(&snapshot);
        // if (err != ESP_OK) {
        //     ESP_LOGE(TAG, "Failed to update GPIOs: %s", esp_err_to_name(err));
        // }   
        
        if (app_watchdog_feed_current_task() != ESP_OK)
        {
            ESP_LOGW(TAG, "hot tub controller main task failed to feed watchdog");
        }

        vTaskDelay(pdMS_TO_TICKS(DEFAULT_HOTTUB_TIMING_LOOP_DELAY_MS));
    
    } // End of while(1) loop 

} // end of hot_tub_controller_main_loop()
//-----------------------------------------------------------------------------





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
    esp_err_t err;

    // Create a mutex for thread-safe access to the hot tub controller state
    if (!s_mutex) 
    {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex) { return ESP_ERR_NO_MEM; }
    }

    // Initialize the hottub_ctl structure to zero values
    lock_state();
    memset(&hottub_ctl, 0, sizeof(hottub_ctl));
    unlock_state();
   
    err = hot_tub_controller_load_saved_settings();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load saved settings: %s", esp_err_to_name(err));
    }

    err = hot_tub_ds18b20_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize DS18B20 sensor: %s", esp_err_to_name(err));
    }

    // Register the JSON service callbacks
    err = hot_tub_controller_register_callbacks();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register hot tub controller callbacks: %s", esp_err_to_name(err));
        return err;
    }


    TaskHandle_t task_handle = NULL;
    BaseType_t result = xTaskCreatePinnedToCore(
                            hot_tub_controller_main_task,
                            "hot_tub_controller_main_task",
                            4096,
                            NULL,
                            6,
                            &task_handle,
                            0);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create hot tub controller main task");
        return ESP_ERR_NO_MEM;
    }

    if (app_watchdog_register_task(task_handle, "hot_tub_controller_main_task") != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register hot tub controller main task with watchdog");
        vTaskDelete(task_handle);
        return ESP_FAIL;
    }

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

    if(!json_service_register_command("hottub.water.temperature.set", hottub_water_temperature_set_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.air.temperature.get", hottub_air_temperature_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.air.temperature.set", hottub_air_temperature_set_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.humidity.get", hottub_humidity_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.humidity.set", hottub_humidity_set_callback, CORE_0)) {
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

    if (!json_service_register_command("hottub.filtered.water.temp.get", hottub_filtered_water_temp_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.filtered.water.temp.set", hottub_filtered_water_temp_set_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.air.temp.get", hottub_air_temperature_get_callback, CORE_0)) {
        return ESP_FAIL;
    }

    if (!json_service_register_command("hottub.air.temp.set", hottub_air_temperature_set_callback, CORE_0)) {
        return ESP_FAIL;
    }

    return ESP_OK;
} // end of hot_tub_controller_register_callbacks()
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

    cJSON_AddBoolToObject(json, "safetySwitch", state->safetySwitch);
    cJSON_AddBoolToObject(json, "heaterOn", state->heaterOn);
    cJSON_AddBoolToObject(json, "autoMode", state->autoMode);
    cJSON_AddBoolToObject(json, "tempUnitCelsius", state->tempUnitCelsius);
    cJSON_AddBoolToObject(json, "pumpOnLight", state->pumpOnLight);
    cJSON_AddBoolToObject(json, "heaterOnLight", state->heaterOnLight);
    
    cJSON_AddNumberToObject(json, "waterTemp", state->waterTemp);
    cJSON_AddNumberToObject(json, "filteredWaterTemp", state->filteredWaterTemp);
    cJSON_AddNumberToObject(json, "airTemp", state->airTemp);
    cJSON_AddNumberToObject(json, "humidity", state->humidity);
    cJSON_AddNumberToObject(json, "setpointTemp", state->setpointTemp);
    cJSON_AddNumberToObject(json, "highHysteresis", state->highHysteresis);
    cJSON_AddNumberToObject(json, "lowHysteresis", state->lowHysteresis);
    cJSON_AddNumberToObject(json, "pumpState", state->pumpState);
    cJSON_AddNumberToObject(json, "pumpPreRunTime", state->pumpPreRunTime);
    cJSON_AddNumberToObject(json, "pumpPostRunTime", state->pumpPostRunTime);
    cJSON_AddNumberToObject(json, "simulationMode", state->simulationMode);
    cJSON_AddStringToObject(json, "lastUpdateTime", state->lastUpdateTime);

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
    // wrap to avoid multiple variable accesses and ensure safe state transitions
    
    static pump_state_t currentSpeed = PUMP_OFF;
    
    // If already there, do nothing
    if (targetSpeed == currentSpeed) return;
 
    // ALWAYS kill both relays first (Safe State)
    hot_tub_controller_gpio_set_level(GPIO_PUMP_LOW, 0);
    hot_tub_controller_gpio_set_level(GPIO_PUMP_HIGH, 0);
    
    // Mandatory dead-time delay to let the motor arcs quench
    // (Crucial when switching directly between Low and High)
    if (currentSpeed != PUMP_OFF && targetSpeed != PUMP_OFF) {
        vTaskDelay(pdMS_TO_TICKS(PUMP_DEAD_TIME_MS)); // 2 second pause
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












