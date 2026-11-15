#include "esp_err.h"
#include "esp_log.h"


#include "hot_tub_globals.h"

#include "hot_tub_hal.h"





#ifndef heaterPowerPin
#define heaterPowerPin 5
#endif

#ifndef pumpLowPowerPin
#define pumpLowPowerPin 6
#endif

#ifndef pumpHighPowerPin
#define pumpHighPowerPin 7
#endif


// Initialize the hot tub hardware abstraction layer
esp_err_t hot_tub_hal_init(void)
{
    // Initialize the hot tub hardware pins mode and state
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << heaterPowerPin) | (1ULL << pumpLowPowerPin) | (1ULL << pumpHighPowerPin),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    gpio_set_level(heaterPowerPin, 0);
    gpio_set_level(pumpLowPowerPin, 0);
    gpio_set_level(pumpHighPowerPin, 0);

    return ESP_OK;
} // end of hot_tub_hal_init(void)
/*****************************************************************************/



/**
 * @brief Set the heater state
 *
 * This function controls the heater by setting the corresponding GPIO pin.
 *
 * @param on true to turn the heater on, false to turn it off.
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
static esp_err_t hot_tub_hal_set_heater(bool on)
{
    gpio_set_level(heaterPowerPin, on ? 1 : 0);
    return ESP_OK;
} // end of hot_tub_hal_set_heater(bool on)
/*****************************************************************************/


/**
 * @brief Set the low power pump state
 *
 * This function controls the low power pump by setting the corresponding GPIO pin.
 *
 * @param on true to turn the pump on, false to turn it off.
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
static esp_err_t hot_tub_hal_set_pump_low(bool on)
{
    gpio_set_level(pumpLowPowerPin, on ? 1 : 0);
    return ESP_OK;
} // end of hot_tub_hal_set_pump_low(bool on)
/*****************************************************************************/


/**
 * @brief Set the high power pump state
 *
 * This function controls the high power pump by setting the corresponding GPIO pin.
 *
 * @param on true to turn the pump on, false to turn it off.
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
static esp_err_t hot_tub_hal_set_pump_high(bool on)
{
    gpio_set_level(pumpHighPowerPin, on ? 1 : 0);
    return ESP_OK;
} // end of hot_tub_hal_set_pump_high(bool on)
/*****************************************************************************/     


static esp_err_t hot_tub_hal_set_pump_off(void)  
{
    gpio_set_level(pumpLowPowerPin, 0);
    gpio_set_level(pumpHighPowerPin, 0);
    return ESP_OK;
}


// Update the heater and pump states based on the desired inputs
esp_err_t hot_tub_hal_update(bool heaterOn, pump_state_t pumpState)
{

    static int lastPumpState = PUMP_OFF;

    // If the pump state has changed or is off, 
    // turn off both pumps and update the last state.
    if ((pumpState != lastPumpState) || (pumpState == PUMP_OFF)) { 
        hot_tub_hal_set_pump_off();
        hot_tub_hal_set_heater(false);
        lastPumpState = pumpState;
        return ESP_OK;  
    }

    if (pumpState == PUMP_OFF) {
        hot_tub_hal_set_pump_off();
        hot_tub_hal_set_heater(false);
        return ESP_OK;
    }

    if (pumpState == PUMP_LOW) {
        hot_tub_hal_set_pump_low(true);
    } else {
        hot_tub_hal_set_pump_low(false);
    }   
 
    if (pumpState == PUMP_HIGH) {
        hot_tub_hal_set_pump_high(true);
    } else {
        hot_tub_hal_set_pump_high(false);
    }
 
    if (pumpState != PUMP_OFF) {
        if (heaterOn) {
            hot_tub_hal_set_heater(true);
        } else {
            hot_tub_hal_set_heater(false);
        }
    }

    ESP_LOGI("hot_tub_hal_update", "Heater: %s, Pump State: %d", heaterOn ? "ON" : "OFF", pumpState);   

    return ESP_OK;
} // end of hot_tub_hal_update(void)