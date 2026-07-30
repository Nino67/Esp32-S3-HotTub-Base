#pragma once

#ifndef HOT_TUB_CONTROLLER_H
#define HOT_TUB_CONTROLLER_H

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <time.h>

#include "hot_tub_globals.h"
#include "cJSON.h"


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


// // Define constants for default values and limits
// #ifndef TIME_BUFFER_SIZE
// #define TIME_BUFFER_SIZE 32
// #endif // TIME_BUFFER_SIZE

// #ifndef CORE_0
// #define CORE_0 0
// #endif // CORE_0

// #ifndef CORE_1
// #define CORE_1 1
// #endif // CORE_1

// // GPIO pin definitions for pump control
// #ifndef GPIO_PUMP_LOW
// #define GPIO_PUMP_LOW 25
// #endif // GPIO_PUMP_LOW

// #ifndef GPIO_PUMP_HIGH
// #define GPIO_PUMP_HIGH 26
// #endif // GPIO_PUMP_HIGH

// #ifndef PUMP_DEAD_TIME_MS
// #define PUMP_DEAD_TIME_MS 2000
// #endif // PUMP_DEAD_TIME_MS

// #ifndef DEFAULT_HOTTUB_TIMING_LOOP_DELAY_MS
// #define DEFAULT_HOTTUB_TIMING_LOOP_DELAY_MS 1000
// #endif // DEFAULT_HOTTUB_TIMING_LOOP_DELAY_MS

// #ifndef DEFAULT_SETPOINT_TEMP
// #define DEFAULT_SETPOINT_TEMP 36.0
// #endif // DEFAULT_SETPOINT_TEMP

// #ifndef DEFAULT_SETPOINT_TEMP_MIN
// #define DEFAULT_SETPOINT_TEMP_MIN 20.0
// #endif // DEFAULT_SETPOINT_TEMP_MIN

// #ifndef DEFAULT_SETPOINT_TEMP_MAX
// #define DEFAULT_SETPOINT_TEMP_MAX 40.0
// #endif // DEFAULT_SETPOINT_TEMP_MAX

// #ifndef DEFAULT_HIGH_HYSTERESIS
// #define DEFAULT_HIGH_HYSTERESIS 1.0
// #endif // DEFAULT_HIGH_HYSTERESIS

// #ifndef DEFAULT_HIGH_HYSTERESIS_MIN
// #define DEFAULT_HIGH_HYSTERESIS_MIN 0.1
// #endif // DEFAULT_HIGH_HYSTERESIS_MIN

// #ifndef DEFAULT_HIGH_HYSTERESIS_MAX
// #define DEFAULT_HIGH_HYSTERESIS_MAX 5.0
// #endif // DEFAULT_HIGH_HYSTERESIS_MAX

// #ifndef DEFAULT_LOW_HYSTERESIS
// #define DEFAULT_LOW_HYSTERESIS 1.0
// #endif // DEFAULT_LOW_HYSTERESIS

// #ifndef DEFAULT_LOW_HYSTERESIS_MIN
// #define DEFAULT_LOW_HYSTERESIS_MIN 0.1
// #endif // DEFAULT_LOW_HYSTERESIS_MIN

// #ifndef DEFAULT_LOW_HYSTERESIS_MAX
// #define DEFAULT_LOW_HYSTERESIS_MAX 5.0
// #endif // DEFAULT_LOW_HYSTERESIS_MAX

// #ifndef DEFAULT_PUMP_PRE_RUN_TIME
// #define DEFAULT_PUMP_PRE_RUN_TIME 2.0
// #endif // DEFAULT_PUMP_PRE_RUN_TIME

// #ifndef DEFAULT_PUMP_PRE_RUN_TIME_MIN
// #define DEFAULT_PUMP_PRE_RUN_TIME_MIN 1.0
// #endif // DEFAULT_PUMP_PRE_RUN_TIME_MIN

// #ifndef DEFAULT_PUMP_PRE_RUN_TIME_MAX
// #define DEFAULT_PUMP_PRE_RUN_TIME_MAX 20.0
// #endif // DEFAULT_PUMP_PRE_RUN_TIME_MAX

// #ifndef DEFAULT_PUMP_POST_RUN_TIME
// #define DEFAULT_PUMP_POST_RUN_TIME 2.0
// #endif // DEFAULT_PUMP_POST_RUN_TIME

// #ifndef DEFAULT_PUMP_POST_RUN_TIME_MIN
// #define DEFAULT_PUMP_POST_RUN_TIME_MIN 1.0
// #endif // DEFAULT_PUMP_POST_RUN_TIME_MIN

// #ifndef DEFAULT_PUMP_POST_RUN_TIME_MAX
// #define DEFAULT_PUMP_POST_RUN_TIME_MAX 20.0
// #endif // DEFAULT_PUMP_POST_RUN_TIME_MAX

// #ifndef DEFAULT_TEMP_UNIT_CELSIUS
// #define DEFAULT_TEMP_UNIT_CELSIUS true
// #endif // DEFAULT_TEMP_UNIT_CELSIUS




void hot_tub_controller_task(void *arg);
esp_err_t hot_tub_controller_init(void);
esp_err_t ntp_time_sync_init(void);
// esp_err_t hot_tub_controller_snapshot_get(HotTubController_t *);
esp_err_t hot_tub_controller_to_json(cJSON *json, const HotTubController_t *state);
esp_err_t hot_tub_controller_publish_status(void);
esp_err_t hot_tub_controller_settings_save_to_nvs(void);


    
#endif // HOT_TUB_CONTROLLER_H
//-----------------------------------------------------------------------------
