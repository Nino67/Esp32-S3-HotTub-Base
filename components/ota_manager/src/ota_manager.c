#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

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
#include "esp_partition.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "version.h"

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
static void ota_manager_update_manifest_callback(cJSON *root);



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

static esp_err_t ota_manager_download_url_to_buffer(const char *url, char **out_buffer, size_t *out_length)
{
    if (!url || !out_buffer) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 60000,
        .disable_auto_redirect = false,
        .max_redirection_count = 5,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to init HTTP client for URL: %s", url);
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed for URL %s: %s", url, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        content_length = 0;
    }

    size_t buffer_size = (content_length > 0 && content_length < 65536) ? (size_t)content_length + 1 : 65536;
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    size_t total_read = 0;
    while (true) {
        int read_bytes = esp_http_client_read(client, buffer + total_read, buffer_size - total_read - 1);
        if (read_bytes < 0) {
            ESP_LOGE(TAG, "HTTP read failed for URL %s: %d", url, read_bytes);
            err = ESP_FAIL;
            break;
        }
        if (read_bytes == 0) {
            break;
        }

        total_read += (size_t)read_bytes;
        if (total_read + 1 >= buffer_size) {
            size_t new_size = buffer_size * 2;
            char *new_buffer = realloc(buffer, new_size);
            if (!new_buffer) {
                ESP_LOGE(TAG, "Failed to realloc HTTP buffer");
                err = ESP_ERR_NO_MEM;
                break;
            }
            buffer = new_buffer;
            buffer_size = new_size;
        }
    }

    if (err == ESP_OK) {
        buffer[total_read] = '\0';
        *out_buffer = buffer;
        if (out_length) {
            *out_length = total_read;
        }
    } else {
        free(buffer);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return err;
}

static esp_err_t ota_manager_parse_manifest(const char *manifest, char **out_version, char **out_ota_url, char **out_storage_url)
{
    if (!manifest || !out_version || !out_ota_url || !out_storage_url) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *root = cJSON_Parse(manifest);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse manifest JSON");
        return ESP_ERR_INVALID_ARG;
    }

    if (!cJSON_IsObject(root)) {
        ESP_LOGE(TAG, "Manifest JSON must be an object");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *version_item = cJSON_GetObjectItemCaseSensitive(root, "version");
    cJSON *ota_url_item = cJSON_GetObjectItemCaseSensitive(root, "url");
    cJSON *storage_url_item = cJSON_GetObjectItemCaseSensitive(root, "storage_url");

    if (!cJSON_IsString(version_item) || !cJSON_IsString(ota_url_item) || !cJSON_IsString(storage_url_item)) {
        ESP_LOGE(TAG, "Manifest JSON missing required string fields");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    if (version_item->valuestring[0] == '\0' || ota_url_item->valuestring[0] == '\0' || storage_url_item->valuestring[0] == '\0') {
        ESP_LOGE(TAG, "Manifest JSON fields must not be empty");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    *out_version = strdup(version_item->valuestring);
    *out_ota_url = strdup(ota_url_item->valuestring);
    *out_storage_url = strdup(storage_url_item->valuestring);

    if (!*out_version || !*out_ota_url || !*out_storage_url) {
        free(*out_version);
        free(*out_ota_url);
        free(*out_storage_url);
        cJSON_Delete(root);
        return ESP_ERR_NO_MEM;
    }

    cJSON_Delete(root);
    return ESP_OK;
}

static esp_err_t ota_manager_trigger_manifest_ota(const char *manifest_url)
{
    char *manifest = NULL;
    size_t manifest_length = 0;
    esp_err_t err = ota_manager_download_url_to_buffer(manifest_url, &manifest, &manifest_length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to download OTA manifest from %s: %s", manifest_url, esp_err_to_name(err));
        return err;
    }

    char *remote_version = NULL;
    char *ota_url = NULL;
    char *storage_url = NULL;
    err = ota_manager_parse_manifest(manifest, &remote_version, &ota_url, &storage_url);
    free(manifest);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Manifest version: %s, current version: %s", remote_version, APP_VERSION);
    if (strcmp(remote_version, APP_VERSION) == 0) {
        ESP_LOGI(TAG, "Manifest version matches current firmware, no update required.");
        free(remote_version);
        free(ota_url);
        free(storage_url);
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Remote firmware version differs; performing OTA from %s", ota_url);
    err = ota_manager_trigger_github_ota(ota_url, storage_url);

    free(remote_version);
    free(ota_url);
    free(storage_url);
    return err;
}

esp_err_t ota_manager_note_boot(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running)
    {
        return ESP_ERR_INVALID_STATE;
    }


    // Register the OTA update commands with the JSON service
    json_service_register_command("ota.manager.update.github", ota_manager_update_git_callback, 0);
    json_service_register_command("ota.manager.update.manifest", ota_manager_update_manifest_callback, 0);

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

static const char* storage_label_for_next_app(void)
{
    const esp_partition_t *next_app = esp_ota_get_next_update_partition(NULL);
    if (next_app == NULL) {
        ESP_LOGE(TAG, "Unable to determine next OTA app partition");
        return NULL;
    }

    if (next_app->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        ESP_LOGI(TAG, "Next OTA target is ota_1, using storage_1");
        return "storage_1";
    }

    if (next_app->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
        ESP_LOGI(TAG, "Next OTA target is ota_0, using storage_0");
        return "storage_0";
    }

    ESP_LOGE(TAG, "Unexpected next OTA app partition subtype: 0x%02x", next_app->subtype);
    return NULL;
}

static char *derive_storage_image_url(const char *ota_url, const char *storage_label)
{
    if (!ota_url || !storage_label) {
        return NULL;
    }

    const char *storage_filename = strcmp(storage_label, "storage_0") == 0 ? "storage_0.bin" : "storage_1.bin";
    const char *needle = "hot_tub_controller.bin";
    const char *match = strstr(ota_url, needle);
    size_t prefix_len;

    if (match) {
        prefix_len = match - ota_url;
    } else {
        const char *slash = strrchr(ota_url, '/');
        if (!slash) {
            return NULL;
        }
        prefix_len = (slash - ota_url) + 1;
    }

    size_t output_len = prefix_len + strlen(storage_filename) + 1;
    char *storage_url = malloc(output_len);
    if (!storage_url) {
        return NULL;
    }

    memcpy(storage_url, ota_url, prefix_len);
    storage_url[prefix_len] = '\0';
    strcat(storage_url, storage_filename);
    return storage_url;
}

static esp_err_t download_partition_image(const char *url, const esp_partition_t *partition)
{
    ESP_LOGI(TAG, "Downloading storage image from '%s' to partition '%s'", url, partition->label);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 60000,
        .disable_auto_redirect = false,
        .max_redirection_count = 5,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client for storage image");
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open for storage image failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }

    int64_t content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    if (status_code != 200) {
        ESP_LOGE(TAG, "Storage image HTTP status %d for URL: %s", status_code, url);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }

    if (content_length <= 0 || (size_t)content_length > partition->size) {
        ESP_LOGE(TAG,
                 "Invalid storage image content length (%lld) for partition '%s' size %u",
                 (long long)content_length,
                 partition->label,
                 (unsigned int)partition->size);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_INVALID_SIZE;
    }

    err = esp_partition_erase_range(partition, 0, partition->size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase partition '%s': %s", partition->label, esp_err_to_name(err));
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return err;
    }

    char buffer[4096];
    size_t offset = 0;
    while (true) {
        int read_bytes = esp_http_client_read(client, buffer, sizeof(buffer));
        if (read_bytes == 0) {
            break;
        }
        if (read_bytes < 0) {
            ESP_LOGE(TAG, "Error reading storage image HTTP response: %d", read_bytes);
            err = ESP_FAIL;
            break;
        }
        if (offset + (size_t)read_bytes > partition->size) {
            ESP_LOGE(TAG, "Storage image exceeds partition size (%s)", partition->label);
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        err = esp_partition_write(partition, offset, buffer, read_bytes);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to write storage image to '%s': %s", partition->label, esp_err_to_name(err));
            break;
        }
        offset += (size_t)read_bytes;
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        return err;
    }

    if ((int64_t)offset != content_length) {
        ESP_LOGE(TAG,
                 "Storage image size mismatch: wrote %u bytes but expected %lld bytes",
                 (unsigned int)offset,
                 (long long)content_length);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Wrote %u bytes to storage partition '%s'", (unsigned int)offset, partition->label);
    return ESP_OK;
}




esp_err_t ota_manager_trigger_github_ota(const char *url, const char *storage_url)
{
    ESP_LOGW(TAG, "Inside ota_manager_trigger_github_ota with URL: %s", url);
    if (url == NULL || url[0] == '\0') {
        ESP_LOGE(TAG, "OTA URL is empty");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting manual OTA update from GitHub: %s", url);
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

    const char *storage_label = storage_label_for_next_app();
    if (!storage_label) {
        free(progress_ctx);
        return ESP_ERR_INVALID_STATE;
    }

    char *derived_storage_url = NULL;
    const char *storage_url_to_use = storage_url;
    if (!storage_url_to_use || storage_url_to_use[0] == '\0') {
        derived_storage_url = derive_storage_image_url(url, storage_label);
        if (!derived_storage_url) {
            ESP_LOGE(TAG, "Unable to derive storage image URL for partition '%s'", storage_label);
            free(progress_ctx);
            return ESP_ERR_INVALID_ARG;
        }
        storage_url_to_use = derived_storage_url;
    }

    ESP_LOGI(TAG, "Using storage image URL: %s for partition %s", storage_url_to_use, storage_label);

    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA upgrade failed: %s", esp_err_to_name(ret));
        free(progress_ctx);
        free(derived_storage_url);
        return ret;
    }

    free(progress_ctx);

    // Update both storage partitions so the new OTA slot and its alternate slot both have valid web assets.
    const char *storage_labels[2] = { "storage_0", "storage_1" };
    for (size_t i = 0; i < 2; ++i) {
        char *partition_storage_url = NULL;
        if (storage_url_to_use && strstr(storage_url_to_use, storage_labels[i])) {
            partition_storage_url = strdup(storage_url_to_use);
        } else {
            partition_storage_url = derive_storage_image_url(url, storage_labels[i]);
        }
        if (!partition_storage_url) {
            ESP_LOGE(TAG, "Unable to derive URL for %s", storage_labels[i]);
            free(derived_storage_url);
            return ESP_ERR_INVALID_ARG;
        }

        const esp_partition_t *storage_part = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA,
            ESP_PARTITION_SUBTYPE_ANY,
            storage_labels[i]);
        if (!storage_part) {
            ESP_LOGE(TAG, "Storage partition '%s' not found", storage_labels[i]);
            free(partition_storage_url);
            free(derived_storage_url);
            return ESP_ERR_NOT_FOUND;
        }

        esp_err_t storage_ret = download_partition_image(partition_storage_url, storage_part);
        free(partition_storage_url);
        if (storage_ret != ESP_OK) {
            ESP_LOGE(TAG, "Storage image update for %s failed: %s", storage_labels[i], esp_err_to_name(storage_ret));
            free(derived_storage_url);
            return storage_ret;
        }
    }

    free(derived_storage_url);
    // hot_tub_device_state_set_ota_status("success");
    // hot_tub_device_state_set_ota_progress(100);
    set_heartbeat_interval(HEARTBEAT_INTERVAL_MS);

    ESP_LOGI(TAG, "OTA upgrade successful, rebooting into new partition...");
    esp_restart();
    return ESP_OK;
} // End of ota_manager_trigger_github_ota
//-----------------------------------------------------------------------------



/**
 * @brief Callback function to handle OTA update requests from the JSON service.
 * 
 * @param root The cJSON object containing the OTA update request.
 *
 * @note This function is registered with the JSON service to handle the "ota.manager.update.github" command.
 *       It extracts the OTA URL (GitHub)from the request and triggers the OTA update process.
 *       https://raw.githubusercontent.com/Nino67/Esp32-S3-HotTub-Base/main/firmware/hot_tub_controller.bin
 */
static void ota_manager_update_manifest_callback(cJSON *root) {
    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    const uint32_t id = cJSON_IsNumber(id_item) ? id_item->valueint : 0;
    const char *type_str = cJSON_IsString(type_item) && type_item->valuestring != NULL ? type_item->valuestring : NULL;
    const char *cmd_str = cJSON_IsString(cmd) && cmd->valuestring != NULL ? cmd->valuestring : NULL;
    const char *params_str = cJSON_IsObject(params) ? cJSON_PrintUnformatted(params) : NULL;

    ESP_LOGI(TAG, "OTA manifest envelope: id=%d, type=%s, cmd=%s, params=%s",
             id,
             type_str ? type_str : "null",
             cmd_str ? cmd_str : "null",
             params_str ? params_str : "null");

    if (params && cJSON_IsObject(params)) {
        cJSON *manifest_url_item = cJSON_GetObjectItemCaseSensitive(params, "manifest_url");
        if (cJSON_IsString(manifest_url_item) && manifest_url_item->valuestring && manifest_url_item->valuestring[0] != '\0') {
            const char *manifest_url = manifest_url_item->valuestring;
            ESP_LOGI(TAG, "Triggering OTA update from manifest URL: %s", manifest_url);
            esp_err_t ota_result = ota_manager_trigger_manifest_ota(manifest_url);
            if (ota_result != ESP_OK) {
                ESP_LOGE(TAG, "Manifest OTA update failed with error: %s", esp_err_to_name(ota_result));
            }
        } else {
            ESP_LOGE(TAG, "Invalid or missing 'manifest_url' parameter for OTA update.");
        }
    } else {
        ESP_LOGE(TAG, "Missing 'params' object for OTA update command.");
    }

    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddItemToObject(root, "response", cJSON_CreateString("ota manifest update completed"));
    cJSON_SetValuestring(type_item, "res");
}

static void ota_manager_update_git_callback(cJSON *root) {
    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    const uint32_t id = cJSON_IsNumber(id_item) ? id_item->valueint : 0;
    const char *type_str = cJSON_IsString(type_item) && type_item->valuestring != NULL ? type_item->valuestring : NULL;
    const char *cmd_str = cJSON_IsString(cmd) && cmd->valuestring != NULL ? cmd->valuestring : NULL;
    const char *params_str = cJSON_IsObject(params) ? cJSON_PrintUnformatted(params) : NULL;

    ESP_LOGI(TAG, "OTA update envelope: id=%d, type=%s, cmd=%s, params=%s",
             id,
             type_str ? type_str : "null",
             cmd_str ? cmd_str : "null",
             params_str ? params_str : "null");

    if (params && cJSON_IsObject(params)) {
        cJSON *url_item = cJSON_GetObjectItemCaseSensitive(params, "url");
        if (cJSON_IsString(url_item) && url_item->valuestring != NULL) {
            const char *ota_url = url_item->valuestring;
            const char *storage_url = NULL;
            cJSON *storage_url_item = cJSON_GetObjectItemCaseSensitive(params, "storage_url");
            if (cJSON_IsString(storage_url_item) && storage_url_item->valuestring != NULL) {
                storage_url = storage_url_item->valuestring;
            }

            char *derived_storage_url = NULL;
            const char *storage_url_to_report = storage_url;
            if (!storage_url_to_report || storage_url_to_report[0] == '\0') {
                const char *storage_label = storage_label_for_next_app();
                if (storage_label) {
                    derived_storage_url = derive_storage_image_url(ota_url, storage_label);
                    storage_url_to_report = derived_storage_url;
                }
            }

            ESP_LOGI(TAG, "Triggering OTA update from URL: %s storage_url: %s",
                     ota_url,
                     storage_url_to_report ? storage_url_to_report : "(none)");

            esp_err_t ota_result = ota_manager_trigger_github_ota(ota_url, storage_url);
            if (ota_result != ESP_OK) {
                ESP_LOGE(TAG, "OTA update failed with error: %s", esp_err_to_name(ota_result));
            }
            if (storage_url_to_report) {
                cJSON_AddStringToObject(root, "storage_url", storage_url_to_report);
            }
            free(derived_storage_url);
        } else {
            ESP_LOGE(TAG, "Invalid or missing 'url' parameter for OTA update.");
        }
    } else {
        ESP_LOGE(TAG, "Missing 'params' object for OTA update command.");
    }

    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddItemToObject(root, "response", cJSON_CreateString("ota update completed"));
    cJSON_SetValuestring(type_item, "res");
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

