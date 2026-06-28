/**
 * @file rgb_led.c
 * @author Gaetano (Nino) Ricca (gricca1967@gmail.com)
 * @brief   RGB LED control for ESP32-S3 
 *
 * @details This file contains the functions to control the RGB WS2812 
 * LED on the ESP32-S3-DevKitC-1 based hot tub controller. It initializes 
 * the LED strip and provides functions to set the LED color. The RGB 
 * LED is used as a status indicator in the main application. The main
 * application cycles through red, green, and blue colors to indicate 
 * different states.
 *
 * @note Matching hardware:
 * - model: ESP32-S3-DevKitC-1.         SKU: ESP32-S3-DevKitC-1-N8R8
 * - mfg: RS Engineering.               date: 2026-06-22

 * @version 0.1
 * @date 2026-06-22
 *
 * @copyright Copyright (c) 2026
 *
 */


// INCLUDE FILES 
#include "rgb_led.h"
#include "app_watchdog.h"
#include "esp_log.h"
#include "esp_err.h"
#include "led_strip.h"


// DEFINES
#ifndef LED_PIN
#define LED_PIN 48 
// #define LED_PIN 38 
#endif

#ifndef NUM_LEDS
#define NUM_LEDS 1
#endif

#ifndef LED_MODEL
#define LED_MODEL LED_MODEL_WS2812
#endif

#ifndef LED_STRIP_INTENSITY
#define LED_STRIP_INTENSITY 1
#endif

#ifndef HEARTBEAT_INTERVAL_MS
#define HEARTBEAT_INTERVAL_MS 500
#endif


// GLOBAL STATIC VARIABLES
static const char *TAG = "rgb_led"; // tag for logging
static led_strip_handle_t led_strip = NULL; // handle for the RGB LED (WS2812)

static int _heartbeat_time_interval_ms_ = HEARTBEAT_INTERVAL_MS;


/**
 * @brief Initialize the RGB LED using the led_strip component.
 *
 * @param gpio_num The GPIO number connected to the LED strip's data line.
 * @return ESP_OK on success, or an error code on failure.  
 */
static esp_err_t init_ws2812_led(gpio_num_t gpio_num)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_num,  // The GPIO that connected to the LED strip's data line
        .max_leds = NUM_LEDS,       // The number of LEDs in the strip,
        .led_model = LED_MODEL,     // LED strip model, it determines the bit timing
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB, // The color component format is G-R-B
        .flags = {
        .invert_out = false, // don't invert the output signal
        }
    };

    /// RMT backend specific configuration
    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,    // different clock source can lead to different power consumption
        .resolution_hz = 10 * 1000 * 1000, // RMT counter clock frequency: 10MHz
        .mem_block_symbols = 64,           // the memory size of each RMT channel, in words (4 bytes)
        .flags = {
            .with_dma = false, // DMA feature is available on chips like ESP32-S3/P4
        }
    };

    /// Create the LED strip object
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED strip: %s", esp_err_to_name(err));
        return err;
    }
    ESP_ERROR_CHECK(clear_rgb_led()); // Clear the LED strip (turn off all LEDs)
 
    return ESP_OK;
} // End of init_ws2812_led
//-----------------------------------------------------------------------------



/**
 * @brief Initialize the RGB LED.
 *
 * @param gpio_num The GPIO number connected to the LED strip's data line.
 * @return ESP_OK on success, or an error code on failure.  
 */
esp_err_t init_rgb_led_pin(gpio_num_t gpio_num)
{
    return init_ws2812_led(gpio_num);
} // End of init_rgb_led_pin
//----------------------------------------------------------------------------- 



/**
 * @brief Initialize the RGB LED with preset LED_PIN.
 *
 * @return ESP_OK on success, or an error code on failure.  
 */
esp_err_t init_rgb_led(void)
{
    return init_ws2812_led(LED_PIN);
} // End of init_rgb_led
//----------------------------------------------------------------------------- 



/**
 * @brief Set the color of the RGB LED.
 *
 * @param red The intensity of the red component (0-255).
 * @param green The intensity of the green component (0-255).
 * @param blue The intensity of the blue component (0-255).
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t set_rgb_led_color(uint8_t red, uint8_t green, uint8_t blue)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Set the color of the first LED
    ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, red, green, blue));
    // Refresh the strip to apply changes
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));

    return ESP_OK;
} // End of set_rgb_led_color
//----------------------------------------------------------------------------- 



/**
 * @brief Clear the RGB LED (turn off all LEDs).
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t clear_rgb_led(void)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Clear the LED strip (turn off all LEDs)
    ESP_ERROR_CHECK(led_strip_clear(led_strip));
    // Refresh the strip to apply changes
    ESP_ERROR_CHECK(led_strip_refresh(led_strip));

    return ESP_OK;
} // End of clear_rgb_led
//----------------------------------------------------------------------------- 



/**
 * @brief Deinitialize the RGB LED.
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t deinit_rgb_led(void)
{
    if (led_strip == NULL) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Deinitialize the LED strip
    ESP_ERROR_CHECK(led_strip_del(led_strip));
    led_strip = NULL;

    return ESP_OK;
} // End of deinit_rgb_led
//----------------------------------------------------------------------------- 



/**
 * @brief Cycle through the RGB LED colors (red, green, blue).
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t cycle_rgb_led_colors(void)
{
    static uint8_t color = 0;

    if (led_strip == NULL) {
        ESP_LOGE(TAG, "LED strip not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    switch (color) {
        case 0:
            ESP_ERROR_CHECK(set_rgb_led_color(LED_STRIP_INTENSITY, 0, 0)); // Red
            color = 1;
            break;
        case 1:
            ESP_ERROR_CHECK(set_rgb_led_color(0, LED_STRIP_INTENSITY, 0)); // Green
            color = 2;
            break;
        case 2:
            ESP_ERROR_CHECK(set_rgb_led_color(0, 0, LED_STRIP_INTENSITY)); // Blue
            color = 0;
            break;
    }    
    
    if (color < 0 || color > 2) {
        color = 0; // Reset to color is out of bounds
    }

    return ESP_OK;
} // End of cycle_rgb_led_colors
//----------------------------------------------------------------------------- 



/**
 * @brief Heartbeat loop task for cycling RGB LED colors.
 *
 * @param arg Pointer to task arguments (not used).
 */
static void heartbeat_loop_task(void *arg)
{
    for (;;) 
    {
        ESP_ERROR_CHECK(cycle_rgb_led_colors());
        if (app_watchdog_feed_current_task() != ESP_OK)
        {
            ESP_LOGW(TAG, "heartbeat task failed to feed watchdog");
        }
        vTaskDelay(pdMS_TO_TICKS(_heartbeat_time_interval_ms_));
    }
    vTaskDelete(NULL);
    ESP_LOGI(TAG, "Heartbeat loop task deleted");
} // End of heartbeat_loop
//-----------------------------------------------------------------------------



/**
 * @brief Set the heartbeat interval for the RGB LED color cycling.
 *
 * @param interval_ms The interval in milliseconds for the heartbeat.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t set_heartbeat_interval(int interval_ms)
{
    if (interval_ms <= 0) {
        ESP_LOGE(TAG, "Invalid heartbeat interval: %d ms", interval_ms);
        return ESP_ERR_INVALID_ARG;
    }
    _heartbeat_time_interval_ms_ = interval_ms;
    ESP_LOGI(TAG, "Heartbeat interval set to %d ms", _heartbeat_time_interval_ms_);
    return ESP_OK;
} // End of set_heartbeat_interval
//----------------------------------------------------------------------------- 



esp_err_t rgb_led_heartbeat(void)
{
    ESP_ERROR_CHECK(init_rgb_led()); // Initialize the RGB LED once before starting the task
    TaskHandle_t task_handle = NULL;
    BaseType_t result = xTaskCreatePinnedToCore(
                            heartbeat_loop_task,
                            "heartbeat_loop",
                            4096,
                            NULL,
                            5,
                            &task_handle,
                            0);
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create heartbeat task");
        return ESP_ERR_NO_MEM;
    }

    if (app_watchdog_register_task(task_handle, "heartbeat_loop") != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register heartbeat task with watchdog");
        vTaskDelete(task_handle);
        return ESP_FAIL;
    }

    return ESP_OK;
} // End of rgb_led_heartbeat
//----------------------------------------------------------------------------- 