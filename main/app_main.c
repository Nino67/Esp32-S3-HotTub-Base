#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"
#include "esp_system.h"
#include "hot_tub_web_server.h"
#include "cJSON.h"
#include "json_service.h"

#include "hot_tub_app.h"

static const char *TAG = "app_main";

#define LED_PIN 48 
#define NUM_LEDS 1
#define LED_MODEL LED_MODEL_WS2812
#define LED_STRIP_INTENSITY 1
#define HEARTBEAT_INTERVAL_MS 1000

static esp_err_t init_rgb_led(gpio_num_t gpio_num);
static void hot_tub_app_task(void *arg);
void app_main(void);

// Global handle for the RGB LED strip (WS2812)
led_strip_handle_t led_strip = NULL;


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
    vTaskDelete(NULL);
} // End of hot_tub_app_task
//----------------------------------------------------------------------------- 



/**
 * @brief Initialize the RGB LED using the led_strip component.
 *
 * @param gpio_num The GPIO number connected to the LED strip's data line.
 * @return ESP_OK on success, or an error code on failure.  
 */
static esp_err_t init_rgb_led(gpio_num_t gpio_num)
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
 
    return ESP_OK;
} // End of init_rgb_led
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


    
    // Initialize the RGB LED strip
    ESP_ERROR_CHECK(init_rgb_led(LED_PIN));
                             
    // Clear the LED strip (turn off all LEDs)
    ESP_ERROR_CHECK(led_strip_clear(led_strip));


    // Heartbeat loop    
    char json_output[256];
    size_t json_output_len;

    for(;;) 
        {
            // Set the LED color to red
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, LED_STRIP_INTENSITY, 0, 0)); // Set the first LED to red
            ESP_ERROR_CHECK(led_strip_refresh(led_strip)); // Refresh the strip to apply changes
            {
                cJSON *json_obj = cJSON_CreateObject();
                if (json_obj != NULL) {
                    cJSON_AddStringToObject(json_obj, "status", "red");
                    if (crc32_json_wrapper(json_obj, json_output, sizeof(json_output), &json_output_len)) {
                        hot_tub_web_server_broadcast_json(json_output);
                    }
                    cJSON_Delete(json_obj);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS)); // Wait for 1 second


            // Set the LED color to green
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, LED_STRIP_INTENSITY, 0)); // Set the first LED to green
            ESP_ERROR_CHECK(led_strip_refresh(led_strip)); // Refresh the strip to apply changes
            {
                cJSON *json_obj = cJSON_CreateObject();
                if (json_obj != NULL) {
                    cJSON_AddStringToObject(json_obj, "status", "green");
                    if (crc32_json_wrapper(json_obj, json_output, sizeof(json_output), &json_output_len)) {
                        hot_tub_web_server_broadcast_json(json_output);
                    }
                    cJSON_Delete(json_obj);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS)); // Wait for 1 second

            // Set the LED color to blue
            ESP_ERROR_CHECK(led_strip_set_pixel(led_strip, 0, 0, 0, LED_STRIP_INTENSITY)); // Set the first LED to blue
            ESP_ERROR_CHECK(led_strip_refresh(led_strip)); // Refresh the strip to apply changes
            {
                cJSON *json_obj = cJSON_CreateObject();
                if (json_obj != NULL) {
                    cJSON_AddStringToObject(json_obj, "status", "blue");
                    if (crc32_json_wrapper(json_obj, json_output, sizeof(json_output), &json_output_len)) {
                        hot_tub_web_server_broadcast_json(json_output);
                    }
                    cJSON_Delete(json_obj);
                }
            }
            vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS)); // Wait for 1 second
        }   

} // End of app_main
//-----------------------------------------------------------------------------


















