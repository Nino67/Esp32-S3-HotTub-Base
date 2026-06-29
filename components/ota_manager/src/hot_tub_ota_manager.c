#include "hot_tub_ota_manager.h"
#include "rgb_led.h"

#include <inttypes.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"


#include "hot_tub_device_state.h"
#include "hot_tub_storage.h"

static const char *TAG = "hot_tub_ota";
static const uint32_t BOOT_FAILURE_LIMIT = 3;

typedef struct
{
    int64_t total_received;
    int64_t content_length;
    int last_progress;
} ota_progress_ctx_t;

static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt)
{
    ota_progress_ctx_t *ctx = evt ? evt->user_data : NULL;
    switch (evt->event_id)
    {
        case HTTP_EVENT_ON_CONNECTED:
            hot_tub_device_state_set_ota_status("started");
            hot_tub_device_state_set_ota_progress(0);
            if (ctx)
            {
                ctx->total_received = 0;
                ctx->content_length = esp_http_client_get_content_length(evt->client);
                ctx->last_progress = 0;
            }
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
                hot_tub_device_state_set_ota_progress(progress);
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            hot_tub_device_state_set_ota_progress(100);
            break;
        case HTTP_EVENT_ERROR:
            hot_tub_device_state_set_ota_status("failed");
            hot_tub_device_state_set_ota_progress(0);
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t hot_tub_ota_manager_note_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(running, &ota_state);
    if (err == ESP_OK && ota_state == ESP_OTA_IMG_PENDING_VERIFY)
    {
        uint32_t boot_failures = 0;
        ESP_RETURN_ON_ERROR(hot_tub_storage_increment_boot_failures(&boot_failures), TAG, "boot counter increment failed");
        ESP_LOGW(TAG, "Pending verify image boot count: %" PRIu32, boot_failures);

        if (boot_failures >= BOOT_FAILURE_LIMIT)
        {
            ESP_LOGE(TAG, "Boot failure limit reached, rolling back");
            esp_ota_mark_app_invalid_rollback_and_reboot();
        }
        return ESP_OK;
    }

    return hot_tub_storage_reset_boot_failures();
}

esp_err_t hot_tub_ota_manager_mark_app_ready(void)
{
    ESP_RETURN_ON_ERROR(hot_tub_storage_reset_boot_failures(), TAG, "reset boot counter failed");

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




esp_err_t hot_tub_ota_manager_trigger_github_ota(const char *url)
{
    if (url == NULL || url[0] == '\0') {
        ESP_LOGE(TAG, "OTA URL is empty");
        hot_tub_device_state_set_ota_status("failed");
        hot_tub_device_state_set_ota_progress(0);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting manual OTA update from GitHub: %s", url);
    hot_tub_device_state_set_ota_status("started");
    hot_tub_device_state_set_ota_progress(0);
    set_heartbeat_interval(OTA_HEARTBEAT_INTERVAL_MS);


    ota_progress_ctx_t *progress_ctx = calloc(1, sizeof(ota_progress_ctx_t));
    if (progress_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate OTA progress context");
        hot_tub_device_state_set_ota_status("failed");
        hot_tub_device_state_set_ota_progress(0);
        return ESP_ERR_NO_MEM;
    }

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
        hot_tub_device_state_set_ota_status("failed");
        hot_tub_device_state_set_ota_progress(0);
        hot_tub_device_state_set_ota_pending(false);
        free(progress_ctx);
        return ret;
    }

    free(progress_ctx);
    hot_tub_device_state_set_ota_status("success");
    hot_tub_device_state_set_ota_progress(100);
    set_heartbeat_interval(HEARTBEAT_INTERVAL_MS);

    ESP_LOGI(TAG, "OTA upgrade successful, rebooting into new partition...");
    esp_restart();
    return ESP_OK;
}
