#pragma once

#ifndef HOT_TUB_HAL_H
#define HOT_TUB_HAL_H

// Includes
#include "esp_err.h"
#include "driver/gpio.h" 

#include "hot_tub_globals.h"





/**
 * @brief Initialize the hot tub hardware abstraction layer
 *
 * This function configures the GPIO pins for the heater and pump controls,
 * setting them as outputs with pulldown enabled and initially low.
 *
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
esp_err_t hot_tub_hal_init(void);   




esp_err_t hot_tub_hal_update(bool heaterOn, pump_state_t pumpState);


#endif // HOT_TUB_HAL_H