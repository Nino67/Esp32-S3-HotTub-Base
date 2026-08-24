#pragma once

#ifndef HOT_TUB_GLOBALS_H
#define HOT_TUB_GLOBALS_H

#include <stdbool.h>
#include <time.h>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"


// Defines shared with other components, 
// such as the system_status component, 
// and hot_tub_controller component to avoid conflicts.
#define TIME_BUFFER_SIZE 32
#define CORE_0 0
#define CORE_1 1
// GPIO pin definitions for pump control
#define GPIO_PUMP_LOW 25
#define GPIO_PUMP_HIGH 26
#define GPIO_DS18B20 4 // GPIO pin used for the DS18B20 data line
#define LOW_PASS_FILTER_ALPHA 0.1f // Alpha value for low-pass filter (0 < alpha < 1)
#define PUMP_DEAD_TIME_MS 2000

#define DEFAULT_HOTTUB_TIMING_LOOP_DELAY_MS 1000

#define DEFAULT_SAFETY_SWITCH_STATE false
#define DEFAULT_SETPOINT_TEMP 32.0
#define DEFAULT_SETPOINT_TEMP_MIN 10.0
#define DEFAULT_SETPOINT_TEMP_MAX 40.0
#define DEFAULT_LOW_PASS_FILTER_ALPHA 0.1f
#define DEFAULT_AUTO_MODE false
#define DEFAULT_SIMULATION_MODE 0

#define DEFAULT_HIGH_HYSTERESIS 1.0
#define DEFAULT_HIGH_HYSTERESIS_MIN 0.1
#define DEFAULT_HIGH_HYSTERESIS_MAX 5.0
#define DEFAULT_LOW_HYSTERESIS 1.0
#define DEFAULT_LOW_HYSTERESIS_MIN 0.1
#define DEFAULT_LOW_HYSTERESIS_MAX 5.0

#define DEFAULT_PUMP_PRE_RUN_TIME 2.0
#define DEFAULT_PUMP_PRE_RUN_TIME_MIN 1.0
#define DEFAULT_PUMP_PRE_RUN_TIME_MAX 20.0
#define DEFAULT_PUMP_POST_RUN_TIME 2.0
#define DEFAULT_PUMP_POST_RUN_TIME_MIN 1.0
#define DEFAULT_PUMP_POST_RUN_TIME_MAX 20.0

#define DEFAULT_MAX_WATER_TEMP 40.0
#define DEFAULT_MIN_WATER_TEMP 10.0
#define DEFAULT_TEMP_UNIT_CELSIUS true


#define HOT_TUB_CONTROLLER_TASK_STACK_SIZE 4096

#define HOT_TUB_CONTROLLER_TASK_PRIORITY 6
#define HOT_TUB_CONTROLLER_TASK_CORE 1

#define HOT_TUB_TEMPERATURE_TASK_STACK_SIZE 3072
#define HOT_TUB_TEMPERATURE_TASK_PRIORITY 5
#define HOT_TUB_TEMPERATURE_TASK_CORE 1

// Define error codes for the hot tub controller
#define HOT_TUB_ERR_NONE 0
#define HOT_TUB_ERR_INVALID_ARG 1010
#define HOT_TUB_ERR_NVS_READ 1020
#define HOT_TUB_ERR_NVS_WRITE 1030
#define HOT_TUB_ERR_SENSOR_READ 1040
#define HOT_TUB_ERR_PUMP_CONTROL 1050
#define HOT_TUB_ERR_HEATER_CONTROL 1060
#define HOT_TUB_ERR_JSON_PARSE 1070
#define HOT_TUB_ERR_JSON_RESPONSE 1080
#define HOT_TUB_ERR_CALLBACK_REGISTRATION 1090
#define HOT_TUB_ERR_TEMP_OUT_OF_RANGE 1100




// Define a structure to hold component callback objects
typedef struct {
    const char *command;
    void (*callback)(cJSON *root);
} callbacks_t;


// Safety switch states
typedef enum {
    SAFETY_SWITCH_OFF = false,
    SAFETY_SWITCH_ON = true
} safety_switch_t;


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
    bool safetySwitch;
    bool heaterOn;
    bool autoMode;
    bool tempUnitCelsius;
    bool pumpOnLight;
    bool heaterOnLight;
    
    float waterTemp;
    float filteredWaterTemp;
    float airTemp;
    float humidity;
    float setpointTemp;
    float lowPassFilterAlpha;
    float highHysteresis;
    float lowHysteresis;
    float pumpPreRunTime;
    float pumpPostRunTime;
    pump_state_t pumpState;
    char initialStartTime[TIME_BUFFER_SIZE];
    char lastUpdateTime[TIME_BUFFER_SIZE];
    sim_mode_t simulationMode;
    int errorCode; // To hold error codes for various operations
} HotTubController_t;


typedef struct {
    char *key_name;
    cJSON *key_value;
} param_tupple_t;



// // Structure to hold the hot tub settings for NVS storage.
// typedef struct {
//     bool tempUnitCelsius;
//     float setpointTemp;
//     float highHysteresis;
//     float lowHysteresis;
//     float pumpPreRunTime;
//     float pumpPostRunTime;
// } hotTub_nvs_save_t;


extern SemaphoreHandle_t s_mutex;
extern HotTubController_t hottub_ctl;
void lock_state(void);
void unlock_state(void);



#endif // HOT_TUB_GLOBALS_H