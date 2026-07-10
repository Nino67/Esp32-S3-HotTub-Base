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
 

// INCLUDE FILES
#include "app_controller.h"
#include "esp_err.h"
#include "esp_log.h"
#include "app_watchdog.h"
#include "ntp_time_sync.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include "system_status.h"




#ifdef CONFIG_SPIRAM
#include "esp_psram.h"
#include "psram_allocator_c.h"
#endif

#define MASTER  0
#define UNIT_ID MASTER


// FUNCTION PROTOTYPES
static void hot_tub_app_task(void *arg);
void app_main(void);
void time_maintenance_task(void *arg);


static const char *TAG = "app_main";



/**
 * @brief Task function for the Hot Tub application.
 *
 * @param arg Pointer to task arguments (not used). 
 */
static void hot_tub_app_task(void *arg)
{
    SemaphoreHandle_t startup_sem = (SemaphoreHandle_t)arg;

    ESP_LOGI(TAG, "Starting Hot Tub Controller on core %d", xPortGetCoreID());
    esp_err_t err = app_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "app_start failed: %s", esp_err_to_name(err));
    }

    if (startup_sem != NULL) {
        xSemaphoreGive(startup_sem);
    }

    esp_err_t unregister_err = app_watchdog_unregister_current_task();
    if (unregister_err != ESP_OK && unregister_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to unregister app_start from watchdog: %s", esp_err_to_name(unregister_err));
    }

    vTaskDelete(NULL);
} // End of hot_tub_app_task
//----------------------------------------------------------------------------- 


esp_err_t psram_check(void)
{
#ifdef CONFIG_SPIRAM
    if (esp_psram_is_initialized()) {
        size_t psram_size = esp_psram_get_size();
        size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGI(TAG, "PSRAM size = %u bytes, free = %u bytes", (unsigned)psram_size, (unsigned)psram_free);
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "PSRAM not initialized");
        return ESP_ERR_NOT_FOUND;
    }
#else
    ESP_LOGW(TAG, "PSRAM support is disabled in config");
    return ESP_ERR_NOT_SUPPORTED;
#endif
} // End of psram_check
//-----------------------------------------------------------------------------     










/**
 * @brief Main application entry point.
 */
void app_main(void)
{
    size_t default_heap_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "Default heap free size = %u bytes", (unsigned)default_heap_free);

#ifdef CONFIG_SPIRAM
    esp_err_t err = psram_check();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PSRAM check failed: %s", esp_err_to_name(err));
    }

    if (psram_allocator_init()) {
        size_t total = psram_allocator_get_total_size();
        size_t used = psram_allocator_get_used_size();
        size_t remaining = psram_allocator_get_remaining_size();
        size_t active = psram_allocator_get_active_allocation_count();

        ESP_LOGI(TAG, "PSRAM allocator total=%u used=%u remaining=%u active=%u",
                 (unsigned)total,
                 (unsigned)used,
                 (unsigned)remaining,
                 (unsigned)active);

        uint8_t *buffer = (uint8_t *)psram_allocator_malloc(256);
        if (buffer) {
            for (size_t i = 0; i < 256; ++i) {
                buffer[i] = (uint8_t)i;
            }
            ESP_LOGW(TAG, "PSRAM buffer: %02X %02X %02X ... %02X %02X %02X",
                     buffer[0], buffer[1], buffer[2],
                     buffer[253], buffer[254], buffer[255]);
            ESP_LOGI(TAG, "PSRAM allocator total=%u", (unsigned)psram_allocator_get_total_size());
            ESP_LOGI(TAG, "PSRAM allocator used=%u remaining=%u active=%u",
                     (unsigned)psram_allocator_get_used_size(),
                     (unsigned)psram_allocator_get_remaining_size(),
                     (unsigned)psram_allocator_get_active_allocation_count());
            psram_allocator_free(buffer);
            ESP_LOGI(TAG, "PSRAM buffer allocated and freed through C wrapper");
        } else {
            ESP_LOGW(TAG, "PSRAM allocator malloc failed");
        }
    } else {
        ESP_LOGW(TAG, "PSRAM allocator initialization failed");
    }
#endif








    SemaphoreHandle_t startup_done_sem = xSemaphoreCreateBinary();
    if (startup_done_sem == NULL) {
        ESP_LOGW(TAG, "Failed to create startup semaphore");
    }

    TaskHandle_t hot_tub_app_task_handle = NULL;
    xTaskCreatePinnedToCore(hot_tub_app_task,
                            "hot_tub_app",
                            16384,
                            startup_done_sem,
                            5,
                            &hot_tub_app_task_handle,
                            0);

    if (startup_done_sem != NULL) {
        if (xSemaphoreTake(startup_done_sem, portMAX_DELAY) != pdTRUE) {
            ESP_LOGW(TAG, "hot_tub_app startup semaphore wait failed");
        }
        vSemaphoreDelete(startup_done_sem);
    }

    TaskHandle_t time_maintenance_task_handle = NULL;
    xTaskCreatePinnedToCore(time_maintenance_task,
                            "time_maintenance_task",
                            4096,
                            NULL,
                            5,
                            &time_maintenance_task_handle,
                            0);

    if (hot_tub_app_task_handle != NULL && time_maintenance_task_handle != NULL) {
        esp_err_t status_err = system_status_init(time_maintenance_task_handle);
        if (status_err != ESP_OK) {
            ESP_LOGW(TAG, "system_status_init failed: %s", esp_err_to_name(status_err));
        } else {
            esp_err_t snap_err = system_status_snapshot();
            if (snap_err != ESP_OK) {
                ESP_LOGW(TAG, "system_status_snapshot failed: %s", esp_err_to_name(snap_err));
            }
        }
    } else {
        if (hot_tub_app_task_handle == NULL) {
            ESP_LOGW(TAG, "Failed to create hot_tub_app task handle");
        }
        if (time_maintenance_task_handle == NULL) {
            ESP_LOGW(TAG, "Failed to create time_maintenance_task handle for status tracking");
        }
    }




} // End of app_main
//-----------------------------------------------------------------------------


















