/**
 * @file app_main.c
 * @author Gaetano (Nino) Ricca (gricca1967@gmail.com)
 * @brief   Main application for ESP32-S3 
 *
 * @details This file contains the main application logic for 
 * the ESP32-S3-based hot tub controller. It initializes the 
 * RGB LED starts the main application task, and implements 
 * a heartbeat loop that updates the LED color and broadcasts 
 * status updates over WebSocket. The application is designed to 
 * run on an ESP32-S3 microcontroller and is part of a larger 
 * project that includes web server functionality and JSON-based 
 * communication.
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


#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_system.h"
#include "hot_tub_web_server.h"
#include "cJSON.h"
#include "json_service.h"
#include "app_watchdog.h"
// #include "rgb_led.h"
#include "hot_tub_app.h"


// #define LED_PIN 48 
// #define NUM_LEDS 1
// #define LED_MODEL LED_MODEL_WS2812
// #define LED_STRIP_INTENSITY 1
// #define HEARTBEAT_INTERVAL_MS 1000

// esp_err_t init_ws2812_led(gpio_num_t gpio_num);
static void hot_tub_app_task(void *arg);
void app_main(void);


static const char *TAG = "app_main";




/**
 * @brief Task function for the Hot Tub application.
 *
 * @param arg Pointer to task arguments (not used). 
 */
static void hot_tub_app_task(void *arg)
{
    ESP_LOGI(TAG, "Starting Hot Tub Controller on core %d", xPortGetCoreID());
    esp_err_t err = hot_tub_app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "hot_tub_app_start failed: %s", esp_err_to_name(err));
    }

    esp_err_t unregister_err = app_watchdog_unregister_current_task();
    if (unregister_err != ESP_OK && unregister_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to unregister hot_tub_app from watchdog: %s", esp_err_to_name(unregister_err));
    }

    vTaskDelete(NULL);
} // End of hot_tub_app_task
//----------------------------------------------------------------------------- 




/**
 * @brief Main application entry point.
 */
void app_main(void)
{


    xTaskCreatePinnedToCore(hot_tub_app_task,
                            "hot_tub_app",
                            8192,
                            NULL,
                            5,
                            NULL,
                            0);


    
 


} // End of app_main
//-----------------------------------------------------------------------------


















