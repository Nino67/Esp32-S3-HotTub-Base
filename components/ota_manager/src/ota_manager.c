#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "ota_manager.h"
#include "rgb_led.h"
 
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "json_service.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_check.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_littlefs.h"
#include "esp_crt_bundle.h"
#include "esp_system.h"
#include "version.h"

#include "nvs_storage.h"



static const char *TAG = "ota_manager";
static const uint32_t BOOT_FAILURE_LIMIT = 3;
static bool s_ota_update_in_progress;
static portMUX_TYPE s_ota_mux = portMUX_INITIALIZER_UNLOCKED;

typedef struct
{
    int64_t total_received;
    int64_t content_length;
    int last_progress;
} ota_progress_ctx_t;




bool json_service_register_command(const char *cmd_string, 
                                   json_cmd_callback_t callback, 
                                   uint8_t target_core);

esp_err_t web_server_broadcast_json(const char *json);


// bool crc32_json_wrapper(const cJSON *json_obj,
//                         char *output,
//                         size_t output_size,
//                         size_t *output_len);

char *json_service_crc32_envelope_encode(const cJSON *json);

static void ota_manager_update_git_callback(cJSON *root);
static void ota_manager_update_manifest_callback(cJSON *root);
static esp_err_t ota_manager_run_github_ota(const char *url, const char *storage_url);
static esp_err_t ota_manager_run_manifest_ota(const char *manifest_url);

bool ota_manager_is_update_in_progress(void)
{
    portENTER_CRITICAL(&s_ota_mux);
    bool in_progress = s_ota_update_in_progress;
    portEXIT_CRITICAL(&s_ota_mux);
    return in_progress;
}

static bool ota_manager_reserve(void)
{
    portENTER_CRITICAL(&s_ota_mux);
    bool available = !s_ota_update_in_progress;
    if (available) {
        s_ota_update_in_progress = true;
    }
    portEXIT_CRITICAL(&s_ota_mux);
    return available;
}

static void ota_manager_release(void)
{
    set_heartbeat_interval(HEARTBEAT_INTERVAL_MS);
    portENTER_CRITICAL(&s_ota_mux);
    s_ota_update_in_progress = false;
    portEXIT_CRITICAL(&s_ota_mux);
}

static void ota_publish_progress_status(const char *status, int progress, int64_t bytes_written, int64_t total_bytes)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }

    cJSON_AddNumberToObject(root, "id", 0);
    cJSON_AddStringToObject(root, "type", "pub");
    cJSON_AddStringToObject(root, "cmd", "ota.manager.progress");
    cJSON_AddStringToObject(root, "status", "ok");

    cJSON *response = cJSON_AddObjectToObject(root, "response");
    if (response) {
        cJSON_AddStringToObject(response, "ota_status", status ? status : "unknown");
        cJSON_AddNumberToObject(response, "ota_progress", progress);
        if (bytes_written >= 0) {
            cJSON_AddNumberToObject(response, "ota_bytes_written", (double)bytes_written);
        }
        if (total_bytes >= 0) {
            cJSON_AddNumberToObject(response, "ota_total_expected_bytes", (double)total_bytes);
        }
    }

    char *encoded_msg = json_service_crc32_envelope_encode(root);
    cJSON_Delete(root);
    if (!encoded_msg) {
        return;
    }

    // ESP_LOGI(TAG, "Broadcasting OTA progress: %s", encoded_msg);
    esp_err_t err = web_server_broadcast_json(encoded_msg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "OTA progress broadcast failed: %s", esp_err_to_name(err));
    }
    free(encoded_msg);
}



static esp_err_t ota_http_event_handler(esp_http_client_event_t *evt)
{
    ota_progress_ctx_t *ctx = evt ? evt->user_data : NULL;
    switch (evt->event_id)
    {
        case HTTP_EVENT_ON_CONNECTED:
            if (ctx)
            {
                ctx->total_received = 0;
                ctx->content_length = esp_http_client_get_content_length(evt->client);
                ctx->last_progress = 0;
            }
            ESP_LOGI(TAG, "OTA HTTP connection established");
            ota_publish_progress_status("downloading", 0, 0, ctx ? ctx->content_length : -1);
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
                if (progress != ctx->last_progress)
                {
                    int previous_progress = ctx->last_progress;
                    ctx->last_progress = progress;
                    if (progress == 100 || progress / 10 != previous_progress / 10)
                    {
                        ESP_LOGI(TAG,
                                 "OTA firmware download progress: %d%% (%" PRId64 " bytes)",
                                 progress,
                                 ctx->total_received);
                    }
                    ota_publish_progress_status("downloading", progress, ctx->total_received, ctx->content_length);
                }
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ota_publish_progress_status("download_complete", 100, ctx ? ctx->total_received : -1, ctx ? ctx->content_length : -1);
            break;
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "OTA HTTP client reported a download error");
            ota_publish_progress_status("failed", ctx ? ctx->last_progress : 0, ctx ? ctx->total_received : -1, ctx ? ctx->content_length : -1);
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

static esp_err_t ota_manager_run_manifest_ota(const char *manifest_url)
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
    err = ota_manager_run_github_ota(ota_url, storage_url);

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




static esp_err_t ota_manager_run_github_ota(const char *url, const char *storage_url)
{
    ESP_LOGW(TAG, "Inside ota_manager_trigger_github_ota with URL: %s", url);
    if (url == NULL || url[0] == '\0') {
        ESP_LOGE(TAG, "OTA URL is empty");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Starting manual OTA update from GitHub: %s", url);
    set_heartbeat_interval(OTA_HEARTBEAT_INTERVAL_MS);
    ota_publish_progress_status("requested", 0, 0, -1);


    ota_progress_ctx_t *progress_ctx = calloc(1, sizeof(ota_progress_ctx_t));
    if (progress_ctx == NULL) {
        ESP_LOGE(TAG, "Failed to allocate OTA progress context");
        ota_publish_progress_status("failed", 0, 0, -1);
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
        ota_publish_progress_status("failed", 0, 0, -1);
        free(progress_ctx);
        return ESP_ERR_INVALID_STATE;
    }

    char *derived_storage_url = NULL;
    const char *storage_url_to_use = storage_url;
    if (!storage_url_to_use || storage_url_to_use[0] == '\0') {
        derived_storage_url = derive_storage_image_url(url, storage_label);
        if (!derived_storage_url) {
            ESP_LOGE(TAG, "Unable to derive storage image URL for partition '%s'", storage_label);
            ota_publish_progress_status("failed", 0, 0, -1);
            free(progress_ctx);
            return ESP_ERR_INVALID_ARG;
        }
        storage_url_to_use = derived_storage_url;
    }

    char *partition_storage_url = strstr(storage_url_to_use, storage_label)
        ? strdup(storage_url_to_use)
        : derive_storage_image_url(url, storage_label);
    free(derived_storage_url);
    if (!partition_storage_url) {
        ESP_LOGE(TAG, "Unable to determine URL for %s", storage_label);
        ota_publish_progress_status("failed", 0, -1, -1);
        free(progress_ctx);
        return ESP_ERR_NO_MEM;
    }

    const esp_partition_t *storage_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, storage_label);
    if (!storage_part || esp_littlefs_partition_mounted(storage_part)) {
        ESP_LOGE(TAG, "OTA storage partition '%s' is missing or mounted", storage_label);
        ota_publish_progress_status("failed", 0, -1, -1);
        free(partition_storage_url);
        free(progress_ctx);
        return storage_part ? ESP_ERR_INVALID_STATE : ESP_ERR_NOT_FOUND;
    }

    ota_publish_progress_status("preparing_storage", 0, -1, -1);
    esp_err_t storage_ret = download_partition_image(partition_storage_url, storage_part);
    free(partition_storage_url);
    if (storage_ret != ESP_OK) {
        ESP_LOGE(TAG, "Storage image update for %s failed: %s", storage_label, esp_err_to_name(storage_ret));
        ota_publish_progress_status("failed", 0, -1, -1);
        free(progress_ctx);
        return storage_ret;
    }

    ESP_LOGI(TAG, "Inactive storage image updated; downloading firmware");
    esp_err_t ret = esp_https_ota(&ota_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA upgrade failed: %s", esp_err_to_name(ret));
        ota_publish_progress_status("failed", progress_ctx->last_progress, progress_ctx->total_received, progress_ctx->content_length);
        free(progress_ctx);
        return ret;
    }

    ESP_LOGI(TAG, "OTA firmware image downloaded and verified");
    ota_publish_progress_status("flashing", 100, progress_ctx->total_received, progress_ctx->content_length);
    free(progress_ctx);
    ota_publish_progress_status("pending_reboot", 100, -1, -1);
    set_heartbeat_interval(HEARTBEAT_INTERVAL_MS);

    ESP_LOGI(TAG, "OTA upgrade successful, rebooting into new partition...");
    esp_restart();
    return ESP_OK;
} // End of ota_manager_trigger_github_ota
//-----------------------------------------------------------------------------

esp_err_t ota_manager_trigger_github_ota(const char *url, const char *storage_url)
{
    if (!url || !url[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ota_manager_reserve()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ota_manager_run_github_ota(url, storage_url);
    ota_manager_release();
    return err;
}

esp_err_t ota_manager_trigger_manifest_ota(const char *manifest_url)
{
    if (!manifest_url || !manifest_url[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ota_manager_reserve()) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ota_manager_run_manifest_ota(manifest_url);
    ota_manager_release();
    return err;
}

/**
 * @brief Callback function to handle OTA update requests from the JSON service.
 * 
 * @param root The cJSON object containing the OTA update request.
 *
 * @note This function is registered with the JSON service to handle the "ota.manager.update.github" command.
 *       It extracts the OTA URL (GitHub)from the request and triggers the OTA update process.
 *       https://raw.githubusercontent.com/Nino67/Esp32-S3-HotTub-Base/main/firmware/hot_tub_controller.bin
 */
typedef struct {
    bool manifest;
    char *url;
    char *storage_url;
} ota_web_request_t;

static void ota_manager_web_task(void *arg)
{
    ota_web_request_t *request = arg;
    esp_err_t err = request->manifest
        ? ota_manager_run_manifest_ota(request->url)
        : ota_manager_run_github_ota(request->url, request->storage_url);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Web OTA request failed: %s", esp_err_to_name(err));
        ota_publish_progress_status("failed", 0, -1, -1);
    } else {
        ota_publish_progress_status("up_to_date", 100, -1, -1);
    }
    free(request->url);
    free(request->storage_url);
    free(request);
    ota_manager_release();
    vTaskDelete(NULL);
}

static void ota_manager_queue_web_request(cJSON *root, bool manifest)
{
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    cJSON *url = cJSON_IsObject(params)
        ? cJSON_GetObjectItemCaseSensitive(params, manifest ? "manifest_url" : "url") : NULL;
    cJSON *storage = !manifest && cJSON_IsObject(params)
        ? cJSON_GetObjectItemCaseSensitive(params, "storage_url") : NULL;
    esp_err_t result = ESP_OK;
    ota_web_request_t *request = NULL;

    if (!cJSON_IsString(url) || !url->valuestring || strncmp(url->valuestring, "https://", 8) != 0 ||
        url->valuestring[8] == '\0' ||
        (storage && (!cJSON_IsString(storage) || !storage->valuestring ||
                     (storage->valuestring[0] &&
                      (strncmp(storage->valuestring, "https://", 8) != 0 || storage->valuestring[8] == '\0'))))) {
        result = ESP_ERR_INVALID_ARG;
    } else if (!ota_manager_reserve()) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        request = calloc(1, sizeof(*request));
        if (request) {
            request->manifest = manifest;
            request->url = strdup(url->valuestring);
            if (storage && storage->valuestring[0]) {
                request->storage_url = strdup(storage->valuestring);
            }
        }
        if (!request || !request->url ||
            (storage && storage->valuestring[0] && !request->storage_url)) {
            result = ESP_ERR_NO_MEM;
        } else if (xTaskCreatePinnedToCore(ota_manager_web_task, "web_ota", 8192,
                                           request, 5, NULL, 0) != pdPASS) {
            result = ESP_ERR_NO_MEM;
        }
        if (result != ESP_OK) {
            if (request) {
                free(request->url);
                free(request->storage_url);
                free(request);
            }
            ota_manager_release();
        }
    }

    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON_SetValuestring(type, "res");
    cJSON_AddStringToObject(root, "status", result == ESP_OK ? "ok" : "error");
    cJSON *response = cJSON_AddObjectToObject(root, "response");
    if (response) {
        cJSON_AddStringToObject(response, "ota_status", result == ESP_OK ? "requested" : "failed");
        if (result != ESP_OK) {
            cJSON_AddStringToObject(response, "error", esp_err_to_name(result));
            cJSON_AddNumberToObject(response, "error_code", result);
            ESP_LOGE(TAG, "Web OTA request rejected: %s", esp_err_to_name(result));
        }
    } else {
        ESP_LOGE(TAG, "Failed to create web OTA response");
    }
}

static void ota_manager_update_manifest_callback(cJSON *root)
{
    ota_manager_queue_web_request(root, true);
}

static void ota_manager_update_git_callback(cJSON *root)
{
    ota_manager_queue_web_request(root, false);
}
