#pragma once

#ifndef HOT_TUB_CALLBACKS_H
#define HOT_TUB_CALLBACKS_H

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_err.h"
#include "esp_log.h"
#include "cJSON.h"


#include "hot_tub_globals.h"

typedef struct {
    callbacks_t *callbacks;
    size_t num_callbacks;
} callbacks_registry_t;

extern callbacks_registry_t *hot_tub_controller_callbacks;

void get_current_time(char *strftime_buf, size_t buf_size);


esp_err_t hot_tub_controller_register_callbacks();
void hottub_callback_response(cJSON *root, cJSON *response);

// Hot tub status callback in use
void hottub_status_get_callback(cJSON *root);

void hottub_auto_mode_get_callback(cJSON *root);
void hottub_auto_mode_set_callback(cJSON *root);
void hottub_heater_status_get_callback(cJSON *root);
void hottub_heater_status_set_callback(cJSON *root);
void hottub_temperature_unit_get_callback(cJSON *root);
void hottub_temperature_unit_set_callback(cJSON *root);
void hottub_water_temperature_get_callback(cJSON *root);
void hottub_water_temperature_set_callback(cJSON *root);
void hottub_air_temperature_get_callback(cJSON *root);
void hottub_air_temperature_set_callback(cJSON *root);
void hottub_humidity_get_callback(cJSON *root);
void hottub_humidity_set_callback(cJSON *root);
void hottub_setpoint_temperature_get_callback(cJSON *root);
void hottub_setpoint_temperature_set_callback(cJSON *root);
void hottub_high_hysteresis_get_callback(cJSON *root);
void hottub_high_hysteresis_set_callback(cJSON *root);
void hottub_low_hysteresis_get_callback(cJSON *root);
void hottub_low_hysteresis_set_callback(cJSON *root);
void hottub_pump_state_get_callback(cJSON *root);
void hottub_pump_state_set_callback(cJSON *root);
void hottub_pump_pre_run_time_get_callback(cJSON *root);
void hottub_pump_pre_run_time_set_callback(cJSON *root);
void hottub_pump_post_run_time_get_callback(cJSON *root);
void hottub_pump_post_run_time_set_callback(cJSON *root);
void hottub_filtered_water_temp_get_callback(cJSON *root) ;
void hottub_filtered_water_temp_set_callback(cJSON *root) ;
void hottub_low_pass_filter_alpha_get_callback(cJSON *root);
void hottub_low_pass_filter_alpha_set_callback(cJSON *root);
void hottub_safety_switch_get_callback(cJSON *root);
void hottub_safety_switch_set_callback(cJSON *root);
void hottub_simulation_mode_get_callback(cJSON *root);
void hottub_simulation_mode_set_callback(cJSON *root);
void hottub_broadcast_status_callback(void);

    




// // Register the "hot_tub_controller" command with the JSON service
// json_service_register_command("hottub.status.get", hottub_status_get_callback, 0);
// json_service_register_command("hottub.automode.get", hottub_auto_mode_get_callback, 0);
// json_service_register_command("hottub.automode.set", hottub_auto_mode_set_callback, 0);
// json_service_register_command("hottub.heater.status.get", hottub_heater_status_get_callback, 0);    
// json_service_register_command("hottub.heater.status.set", hottub_heater_status_set_callback, 0);    
// json_service_register_command("hottub.temperature.unit.get", hottub_temperature_unit_get_callback, 0);
// json_service_register_command("hottub.temperature.unit.set", hottub_temperature_unit_set_callback, 0);


#endif // HOT_TUB_CALLBACKS_H
