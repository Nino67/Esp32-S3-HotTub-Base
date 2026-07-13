#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"


#include "hot_tub_controller.h"

static const char *TAG = "hot_tub_controller";
#define TIME_BUFFER_SIZE 32

// Pump state enumeration
typedef enum {
    PUMP_OFF,
    PUMP_LOW,
    PUMP_HIGH
} pump_state_t;

// Simulation modes
typedef enum {
    SIM_NONE,
    SIM_MANUAL,
    SIM_PHYSICS,
    SIM_TRIANGLE
} sim_mode_t;


/**
 * @brief Structure to hold the state of the hot tub controller.
 */
typedef struct {
    bool heaterOn;
    bool automaticMode;
    bool tempUnitsCelsius;
    bool pumpOnLight;
    bool heaterOnLight;
    bool pumpOn;
    bool pumpLowSpeed;
    bool pumpHighSpeed;
    
    float waterTemp;
    float airTemp;
    float humidity;
    float setpointTemp;
    float highHysteresis;
    float lowHysteresis;
    float pumpPreRunTime;
    float pumpPostRunTime;
} HotTubController_t;


// Hot tub control structure
typedef struct {
    const char *id;                         // NVS namespace
    char timestamp[TIME_BUFFER_SIZE];       // when this frame was recorded (UTC)
    int32_t temperature;                    // Current temperature of the hot tub
    int32_t setpoint;                       // Desired temperature setpoint
    int32_t brightness;                     // Display brightness
    int32_t low_hysteresis;                 // Lower hysteresis value
    int32_t high_hysteresis;                // Upper hysteresis value    
    int32_t pre_pump_delay;                 // No delay by default
    int32_t post_pump_delay;                // No delay by default
    pump_state_t pump_state;                // Pump status
    bool heater_on;                         // Heater status
    bool auto_temp;                         // Default to auto temperature control mode
    bool temp_unit_celsius;                 // Temperature unit: true for Celsius, false for Fahrenheit
} hottub_ctl_t;





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






