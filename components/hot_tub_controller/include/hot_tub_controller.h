#pragma once

#ifndef HOT_TUB_CONTROLLER_H
#define HOT_TUB_CONTROLLER_H

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>
#include <time.h>




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



void hot_tub_controller_task(void *arg);
esp_err_t hot_tub_controller_init(void);
esp_err_t ntp_time_sync_init(void);
esp_err_t hot_tub_controller_snapshot(HotTubController_t *);
esp_err_t hot_tub_controller_publish_status(void);
esp_err_t hot_tub_controller_settings_save_to_nvs(void);


    
#endif // HOT_TUB_CONTROLLER_H
//-----------------------------------------------------------------------------
