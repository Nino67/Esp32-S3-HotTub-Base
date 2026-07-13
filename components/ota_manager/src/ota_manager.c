#include <inttypes.h>

#include "ota_manager.h"
#include "rgb_led.h"
 
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "json_service.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_check.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"

#include "nvs_storage.h"



static const char *TAG = "ota_manager";
static const uint32_t BOOT_FAILURE_LIMIT = 3;

typedef struct
{
    int64_t total_received;
    int64_t content_length;
    int last_progress;
} ota_progress_ctx_t;




bool json_service_register_command(const char *cmd_string, 
                                   json_cmd_callback_t callback, 
                                   uint8_t target_core);


// bool crc32_json_wrapper(const cJSON *json_obj,
//                         char *output,
//                         size_t output_size,
//                         size_t *output_len);

char *json_service_crc32_envelope_encode(const cJSON *json);

static void ota_manager_update_git_callback(cJSON *root);




static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt)
{
    ota_progress_ctx_t *ctx = evt ? evt->user_data : NULL;
    switch (evt->event_id)
    {
        case HTTP_EVENT_ON_CONNECTED:
            // hot_tub_device_state_set_ota_status("started");
            // hot_tub_device_state_set_ota_progress(0);
            // if (ctx)
            // {
            //     ctx->total_received = 0;
            //     ctx->content_length = esp_http_client_get_content_length(evt->client);
            //     ctx->last_progress = 0;
            // }
            break;
        case HTTP_EVENT_ON_HEADER:
            if (ctx && evt->header_key && evt->header_value && strcmp(evt->header_key, "Content-Length") == 0)
            {
                ctx->content_length = atoll(evt->header_value);
            }
            break;
        case HTTP_EVENT_ON_DATA:
            if (ctx && evt->data_len > 0)
            {
                ctx->total_received += evt->data_len;
                int progress = ctx->last_progress;
                if (ctx->content_length > 0)
                {
                    progress = (int)((ctx->total_received * 100LL) / ctx->content_length);
                }
                else if (progress < 95)
                {
                    progress += 6;
                }

                if (progress > 100)
                {
                    progress = 100;
                }
                ctx->last_progress = progress;
                // hot_tub_device_state_set_ota_progress(progress);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            // hot_tub_device_state_set_ota_progress(100);
            break;
        case HTTP_EVENT_ERROR:
            // hot_tub_device_state_set_ota_status("failed");
            // hot_tub_device_state_set_ota_progress(0);
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t ota_manager_note_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running)
    {
        return ESP_ERR_INVALID_STATE;
    }


    // Register the "ota.manager.update.git" command with the JSON service
    json_service_register_command("ota.manager.update.github", ota_manager_update_git_callback, 0);

    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        uint32_t boot_failures = 0;
        ESP_RETURN_ON_ERROR(nvs_storage_increment_boot_failures(&boot_failures), TAG, "boot counter increment failed");
        ESP_LOGW(TAG, "Pending verify image boot count: %" PRIu32, boot_failures);

        if (boot_failures >= BOOT_FAILURE_LIMIT)
        {
            ESP_LOGE(TAG, "Boot failure limit reached, rolling back");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        return ESP_OK;
    }

    return nvs_storage_reset_boot_failures();
}

esp_err_t ota_manager_mark_app_ready(void)
{
    ESP_RETURN_ON_ERROR(nvs_storage_reset_boot_failures(), TAG, "reset boot counter failed");

    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_ERR_OTA_ROLLBACK_INVALID_STATE)
    {
        return ESP_OK;
    }

    return err;
}




const char* get_active_storage_label(void) 
{
    // Check which app partition is currently running
    const esp_partition_t *running_app = esp_ota_get_running_partition();
    
    // If we booted into ota_1, we must mount storage_1
    if (running_app != NULL && running_app->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        return "storage_1";
    }
    
    // Default fallback: if on ota_0 (or factory), use storage_0
    return "storage_0";
}




esp_err_t ota_manager_trigger_github_ota(const char *url)
{
    ESP_LOGW(TAG, "Inside ota_manager_trigger_github_ota with URL: %s", url);
    if (url == NULL || url[0] == '\0') {
        ESP_LOGE(TAG, "OTA URL is empty");
        // hot_tub_device_state_set_ota_status("failed");
        // hot_tub_device_state_set_ota_progress(0);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting manual OTA update from GitHub: %s", url);
    // hot_tub_device_state_set_ota_status("started");
    // hot_tub_device_state_set_ota_progress(0);
    set_heartbeat_interval(OTA_HEARTBEAT_INTERVAL_MS);


    ota_progress_ctx_t *progress_ctx = calloc(1, sizeof(ota_progress_ctx_t));
    if (progress_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate OTA progress context");
        // hot_tub_device_state_set_ota_status("failed");
        // hot_tub_device_state_set_ota_progress(0);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "OTA progress context allocated at %p", (void *)progress_ctx);
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 60000,
        .disable_auto_redirect = false,
        .max_redirection_count = 5,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = ota_http_event_handler,
        .user_data = progress_ctx,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA upgrade failed: %s", esp_err_to_name(ret));
        // hot_tub_device_state_set_ota_status("failed");
        // hot_tub_device_state_set_ota_progress(0);
        // hot_tub_device_state_set_ota_pending(false);
        free(progress_ctx);
        return ret;
    }

    free(progress_ctx);
    // hot_tub_device_state_set_ota_status("success");
    // hot_tub_device_state_set_ota_progress(100);
    set_heartbeat_interval(HEARTBEAT_INTERVAL_MS);

    ESP_LOGI(TAG, "OTA upgrade successful, rebooting into new partition...");
    esp_restart();
    return ESP_OK;
} // End of ota_manager_trigger_github_ota
//-----------------------------------------------------------------------------



// /**
//  * @brief Callback function to handle the "system_status" command received via JSON service.
//  *
//  * @param root The cJSON object containing the command and its data.
//  */
//  static void system_status_callback(cJSON *root) {
    
//     cJSON  *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
//     cJSON  *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
//     cJSON  *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");

//     const uint32_t id = cJSON_IsNumber(id_item) ? id_item->valueint : 0;
//     const char *type_str = cJSON_IsString(type_item) && type_item->valuestring != NULL ? type_item->valuestring : NULL;
//     const char *cmd_str = cJSON_IsString(cmd) && cmd->valuestring != NULL ? cmd->valuestring : NULL;
    
//     ESP_LOGD(TAG, "System status envelope: id=%d, type=%s, cmd=%s", 
//              id,
//              type_str ? type_str : "null",
//              cmd_str ? cmd_str : "null");
             
//     cJSON *status_snapshot_current = system_status_get_json();
//     cJSON_AddStringToObject(root, "status", "ok");
//     cJSON_AddItemToObject(root, "response", cJSON_Duplicate(status_snapshot_current, 1));
//     cJSON_SetValuestring(type_item, "res");
//     if (status_snapshot_current) { cJSON_Delete(status_snapshot_current); }

// } // End of system_status_callback
// //-----------------------------------------------------------------------------



static void ota_manager_update_git_callback(cJSON *root) {
    cJSON  *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON  *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON  *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    cJSON  *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    const uint32_t id = cJSON_IsNumber(id_item) ? id_item->valueint : 0;
    const char *type_str = cJSON_IsString(type_item) && type_item->valuestring != NULL ? type_item->valuestring : NULL;
    const char *cmd_str = cJSON_IsString(cmd) && cmd->valuestring != NULL ? cmd->valuestring : NULL;
    const char *params_str = cJSON_IsObject(params) ? cJSON_PrintUnformatted(params) : NULL;
 
    ESP_LOGD(TAG, "OTA update envelope: id=%d, type=%s, cmd=%s, params=%s", 
             id,
             type_str ? type_str : "null",
             cmd_str ? cmd_str : "null",
             params_str ? params_str : "null");

    if (params && cJSON_IsObject(params)) {
        cJSON *url_item = cJSON_GetObjectItemCaseSensitive(params, "url");
        if (cJSON_IsString(url_item) && url_item->valuestring != NULL) {
            // const char *ota_url = url_item->valuestring;
            // ESP_LOGI(TAG, "Triggering OTA update from URL: %s", ota_url);
            // esp_err_t ota_result = ota_manager_trigger_github_ota(ota_url);
            // if (ota_result != ESP_OK) {
            //     ESP_LOGE(TAG, "OTA update failed with error: %s", esp_err_to_name(ota_result));
            //     // Optionally, you can send a response back indicating failure
            // }
        } else {
            ESP_LOGE(TAG, "Invalid or missing 'url' parameter for OTA update.");
        }
    } else {
        ESP_LOGE(TAG, "Missing 'params' object for OTA update command.");
    }
}











// /**
//  * @brief Handle an OTA update request.
//  * 
//  * @param root The JSON object containing the OTA update request.
//  * @return ESP_OK on success, or an error code on failure.
//  */
// esp_err_t web_server_ota_update_requested(cJSON *root)
// {   
//     ESP_LOGW(TAG, "Inside OTA update request handler");
//     if (!s_server)
//     {
//         return ESP_ERR_INVALID_STATE;
//     }
//     ESP_LOGI(TAG, "OTA update request received: %s", cJSON_Print(root));
//     // Implement OTA update request handling here
//     cJSON *url_item = cJSON_GetObjectItemCaseSensitive(root, "url");
    
//     if (cJSON_IsString(url_item) && url_item->valuestring && url_item->valuestring[0] != '\0')
//     {
//         char *url_copy = strdup(url_item->valuestring);
//         if (url_copy != NULL)
//         {
//             // web_server_device_state_set_ota_pending(true);
//             // web_server_device_state_set_ota_status("requested");
//             // web_server_device_state_set_ota_progress(0);
//             if (xTaskCreatePinnedToCore(ota_update_task, "ota_update", 8192, url_copy, 5, NULL, 0) != pdPASS)
//             {
//                 ESP_LOGE(TAG, "Failed to create OTA task");
//                 free(url_copy);
//                 // web_server_device_state_set_ota_pending(false);
//                 // web_server_device_state_set_ota_status("failed");
//             }
//         }
//         else
//         {
//             ESP_LOGE(TAG, "Failed to allocate OTA URL copy");
//             // web_server_device_state_set_ota_status("failed");
//             // web_server_device_state_set_ota_pending(false);
//         }
//     }
//     else
//     {
//         ESP_LOGE(TAG, "OTA update command missing valid url");
//         // web_server_device_state_set_ota_status("failed");
//         // web_server_device_state_set_ota_pending(false);
//     }

//     ESP_LOGW(TAG, "OTA update request processing completed");
//     return ESP_OK;
// }

