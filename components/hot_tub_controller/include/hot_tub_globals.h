#pragma once

#ifndef HOT_TUB_GLOBALS_H
#define HOT_TUB_GLOBALS_H

#include <stdbool.h>
#include <time.h>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"



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
    bool autoMode;
    bool tempUnitCelsius;
    bool pumpOnLight;
    bool heaterOnLight;
    
    float waterTemp;
    float airTemp;
    float humidity;
    float setpointTemp;
    float highHysteresis;
    float lowHysteresis;
    float pumpPreRunTime;
    float pumpPostRunTime;
    pump_state_t pumpState;
    time_t lastUpdateTime;
    sim_mode_t simulationMode;
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