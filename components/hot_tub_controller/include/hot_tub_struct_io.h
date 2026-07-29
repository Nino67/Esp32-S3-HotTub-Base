#pragma once

#ifndef HOT_TUB_STRUCT_IO_H
#define HOT_TUB_STRUCT_IO_H

#include "esp_err.h"
#include "hot_tub_controller.h"

esp_err_t hot_tub_struct_io_save_settings_to_nvs(void);
esp_err_t hot_tub_controller_snapshot_get(HotTubController_t *);
esp_err_t hot_tub_controller_snapshot_set(const HotTubController_t *state);
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
float hot_tub_controller_get_filtered_water_temp(void);
void hot_tub_controller_set_filtered_water_temp(float temp);
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
pump_state_t hot_tub_controller_pump_state_get(pump_state_t *state);
void hot_tub_controller_set_pump(pump_state_t targetSpeed); 
float hot_tub_controller_get_pump_pre_run_time(void);
void hot_tub_controller_set_pump_pre_run_time(float time);
float hot_tub_controller_get_pump_post_run_time(void);
void hot_tub_controller_set_pump_post_run_time(float time);
void hot_tub_controller_get_last_update_time(char *buffer, size_t buffer_size);
void hot_tub_controller_set_last_update_time(const char *time_str);
float hot_tub_controller_get_low_pass_filter_alpha(void);
void hot_tub_controller_set_low_pass_filter_alpha(float alpha);
bool hot_tub_controller_get_safety_switch(void);
void hot_tub_controller_set_safety_switch(bool state);



#endif // HOT_TUB_STRUCT_IO_H