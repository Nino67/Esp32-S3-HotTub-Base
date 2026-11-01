/**
 * @file hot_tub_web_server.c
 * @author Gaetano (Nino) Ricca (gricca1967@gmail.com)
 * @brief   Hot Tub Web Server for ESP32-S3 
 *
 * @details This file contains the functions to control the web server for the ESP32-S3-DevKitC-1 based hot tub controller. It initializes 
 * the web server and provides functions to handle HTTP requests and serve web assets. The web server is used to provide a user interface
 * for monitoring and controlling the hot tub.
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
#include "web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "esp_check.h"
#include "esp_crc.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "cJSON.h"


#include "system_status.h"
#include "json_service.h"
#include "ota_manager.h"

//------------------------------------------------------------------------------


// GLOBAL STATIC VARIABLES
static const char *TAG = "web_server";

#define WEB_SERVER_MAX_OPEN_SOCKETS 12
#define WEB_SERVER_CORE 1
#define WEB_SERVER_WHOLE_FILE_MAX 32768
#define WEB_SERVER_CHUNK_SIZE 4096
#define WEB_SERVER_ETAG_CACHE_SIZE 16

static const char *LFS_BASE_PATH = "/littlefs";
static const char *LFS_INDEX = "/littlefs/www/index.html";
static const char *s_mounted_label;
static httpd_handle_t s_server;
static int s_ws_clients[4];
static uint8_t s_ws_fail_count[4];
static uint8_t s_ws_send_inflight[4];
static int64_t s_ws_retry_after_us[4];
static int64_t s_ws_last_ok_us[4];
static uint32_t s_ws_generation[4];
static uint32_t s_ws_next_generation;
static SemaphoreHandle_t s_ws_mutex;
static portMUX_TYPE s_ws_stats_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_ws_dropped_count;
static uint32_t s_ws_failed_count;
static webserver_status_t s_web_stats;

typedef struct {
    char path[96];
    uint32_t size;
    uint32_t crc;
} etag_entry_t;

// Static assets are only served from the single httpd task, so no locking is needed.
static etag_entry_t s_etag_cache[WEB_SERVER_ETAG_CACHE_SIZE];
static size_t s_etag_next;
//------------------------------------------------------------------------------

typedef struct {
    uint32_t generation;
    char *payload;
} ws_async_send_ctx_t;

// esp_err_t json_service_validate_crc32(const char *, char *, size_t, uint32_t *, uint32_t *);    
// 
// esp_err_t json_service_parse_json(const char *, cJSON **);

cJSON *system_status_get_json();
void json_service_dispatcher_core0(cJSON *root); 

char *json_service_crc32_envelope_encode(const cJSON *);

cJSON *json_service_crc32_envelope_decode(const char *);


cJSON *json_service_parse_rpc_envelope(const char *json_str, rpc_type_t *out_type, uint32_t *out_id, char **out_cmd, cJSON **out_params);



/**
 * @brief Mount the LittleFS filesystem, 
 * which contains the web assets. 
 */
static esp_err_t mount_littlefs(void)
{
    const esp_partition_t *running_app = esp_ota_get_running_partition();
    const char *primary_label = "storage_0";
    const char *fallback_label = "storage_1";

    if (running_app != NULL && running_app->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
        primary_label = "storage_1";
        fallback_label = "storage_0";
    }

    const char *mount_candidates[2] = { primary_label, fallback_label };

    for (size_t i = 0; i < 2; ++i)
    {
        const char *candidate = mount_candidates[i];
        ESP_LOGI(TAG, "Mounting LittleFS partition '%s'", candidate);

        esp_vfs_littlefs_conf_t conf = {
            .base_path = LFS_BASE_PATH,
            .partition_label = candidate,
            .format_if_mount_failed = false,
            .dont_mount = false,
        };

        esp_err_t err = esp_vfs_littlefs_register(&conf);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGW(TAG, "Failed to mount '%s': %s", candidate, esp_err_to_name(err));
            continue;
        }

        FILE *index = fopen(LFS_INDEX, "r");
        if (index == NULL)
        {
            char gz_index[sizeof("/littlefs/www/index.html.gz")];
            snprintf(gz_index, sizeof(gz_index), "%s.gz", LFS_INDEX);
            index = fopen(gz_index, "r");
        }
        if (index != NULL)
        {
            fclose(index);
            s_mounted_label = candidate;
            ESP_LOGI(TAG, "LittleFS mount validated using '%s'", candidate);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "Mounted '%s' but missing %s, trying fallback", candidate, LFS_INDEX);
        esp_vfs_littlefs_unregister(candidate);
    }

    ESP_LOGE(TAG, "No valid LittleFS partition found");
    return ESP_FAIL;
} // End of mount_littlefs
//-----------------------------------------------------------------------------


static const char *content_type_for_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext)
    {
        return "text/plain";
    }
    if (strcmp(ext, ".html") == 0)
    {
        return "text/html";
    }
    if (strcmp(ext, ".css") == 0)
    {
        return "text/css";
    }
    if (strcmp(ext, ".js") == 0)
    {
        return "application/javascript";
    }
    return "text/plain";
} // End of content_type_for_path
//-----------------------------------------------------------------------------



static void web_stats_publish(bool request_served, bool not_found)
{
    uint16_t ws_clients = 0;
    for (size_t i = 0; i < sizeof(s_ws_clients) / sizeof(s_ws_clients[0]); ++i)
    {
        if (s_ws_clients[i] > 0)
        {
            ws_clients++;
        }
    }

    webserver_status_t snapshot;
    portENTER_CRITICAL(&s_ws_stats_mux);
    s_web_stats.is_running = s_server != NULL;
    s_web_stats.active_connections = ws_clients;
    if (request_served)
    {
        s_web_stats.total_requests_served++;
    }
    if (not_found)
    {
        s_web_stats.file_not_found_404s++;
    }
    snapshot = s_web_stats;
    portEXIT_CRITICAL(&s_ws_stats_mux);

    (void)system_status_set_webserver_status(&snapshot);
} // End of web_stats_publish
//-----------------------------------------------------------------------------


/**
 * @brief Build a validator for a static file from a cached CRC of its stored bytes.
 *
 * The CRC is computed once per file per boot, so edits to the file always change the ETag.
 */
static bool file_etag(const char *path, size_t size, char *out, size_t out_len)
{
    uint32_t crc = 0;
    bool found = false;
    for (size_t i = 0; i < WEB_SERVER_ETAG_CACHE_SIZE; ++i)
    {
        if (s_etag_cache[i].size == size && strcmp(s_etag_cache[i].path, path) == 0)
        {
            crc = s_etag_cache[i].crc;
            found = true;
            break;
        }
    }

    if (!found)
    {
        FILE *file = fopen(path, "rb");
        if (!file)
        {
            return false;
        }
        uint8_t buffer[512];
        size_t read_bytes;
        while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
        {
            crc = esp_crc32_le(crc, buffer, read_bytes);
        }
        fclose(file);

        etag_entry_t *entry = &s_etag_cache[s_etag_next];
        s_etag_next = (s_etag_next + 1) % WEB_SERVER_ETAG_CACHE_SIZE;
        strlcpy(entry->path, path, sizeof(entry->path));
        entry->size = (uint32_t)size;
        entry->crc = crc;
    }

    snprintf(out, out_len, "\"%08" PRIx32 "-%zx\"", crc, size);
    return true;
} // End of file_etag
//-----------------------------------------------------------------------------


/**
 * @brief Send a file as the HTTP response.
 *
 * A pre-compressed "<path>.gz" is preferred when present. Small files are sent with one
 * Content-Length response; larger files use large chunks to avoid per-chunk framing overhead.
 * The ETag lets browsers revalidate cached assets with a 304 response.
 * 
 * @param req The HTTP request object.
 * @param path The file path to send.
 * @return ESP_OK on success, or an error code on failure.
 */
static esp_err_t send_file(httpd_req_t *req, const char *path)
{
    char stored_path[512];
    bool gzipped = false;
    struct stat st;

    int n = snprintf(stored_path, sizeof(stored_path), "%s.gz", path);
    if (n > 0 && n < (int)sizeof(stored_path) && stat(stored_path, &st) == 0 && S_ISREG(st.st_mode))
    {
        gzipped = true;
    }
    else
    {
        strlcpy(stored_path, path, sizeof(stored_path));
        if (stat(stored_path, &st) != 0 || !S_ISREG(st.st_mode))
        {
            web_stats_publish(false, true);
            return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        }
    }

    const size_t size = (size_t)st.st_size;
    httpd_resp_set_type(req, content_type_for_path(path));
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    if (gzipped)
    {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    char etag[40];
    if (file_etag(stored_path, size, etag, sizeof(etag)))
    {
        httpd_resp_set_hdr(req, "ETag", etag);
        char if_none_match[48];
        if (httpd_req_get_hdr_value_str(req, "If-None-Match", if_none_match, sizeof(if_none_match)) == ESP_OK &&
            strcmp(if_none_match, etag) == 0)
        {
            httpd_resp_set_status(req, "304 Not Modified");
            web_stats_publish(true, false);
            return httpd_resp_send(req, NULL, 0);
        }
    }

    FILE *file = fopen(stored_path, "rb");
    if (!file)
    {
        web_stats_publish(false, true);
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    }

    esp_err_t result = ESP_OK;
    char *whole = NULL;
    if (size > 0 && size <= WEB_SERVER_WHOLE_FILE_MAX)
    {
        whole = malloc(size);
    }

    if (whole != NULL && fread(whole, 1, size, file) == size)
    {
        result = httpd_resp_send(req, whole, size);
    }
    else
    {
        size_t chunk_size = WEB_SERVER_CHUNK_SIZE;
        char *buffer = malloc(chunk_size);
        char small_buffer[512];
        if (buffer == NULL)
        {
            buffer = small_buffer;
            chunk_size = sizeof(small_buffer);
        }
        rewind(file);

        size_t read_bytes;
        while ((read_bytes = fread(buffer, 1, chunk_size, file)) > 0)
        {
            result = httpd_resp_send_chunk(req, buffer, read_bytes);
            if (result != ESP_OK)
            {
                break;
            }
        }
        if (result == ESP_OK)
        {
            result = httpd_resp_send_chunk(req, NULL, 0);
        }
        if (buffer != small_buffer)
        {
            free(buffer);
        }
    }

    free(whole);
    fclose(file);
    web_stats_publish(result == ESP_OK, false);
    return result;
} // End of send_file
//-----------------------------------------------------------------------------



/**
 * @brief Handle requests for the index page.
 * 
 * @param req The HTTP request object.
 * @return ESP_OK on success, or an error code on failure.
 */
static esp_err_t index_handler(httpd_req_t *req)
{
    return send_file(req, LFS_INDEX);
} // End of index_handler
//-----------------------------------------------------------------------------


/**
 * @brief Handle requests for static assets (CSS, JS).
 * 
 * @param req The HTTP request object.
 * @return ESP_OK on success, or an error code on failure.
 */
static esp_err_t asset_handler(httpd_req_t *req)
{
    char path[400];
    int n = snprintf(path, sizeof(path), "%s/www%s", LFS_BASE_PATH, req->uri);
    if (n < 0 || n >= (int)sizeof(path))
    {
        return httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "URI too long");
    }
    return send_file(req, path);
}


/**
 * @brief Disable Nagle on accepted sockets.
 *
 * The HTTP server sends WebSocket headers/payloads and chunked HTTP framing as separate small
 * writes. With Nagle enabled each one can wait for a delayed ACK from the client.
 */
static esp_err_t web_server_on_open(httpd_handle_t hd, int sockfd)
{
    (void)hd;
    int enable = 1;
    if (setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &enable, sizeof(enable)) != 0)
    {
        ESP_LOGW(TAG, "Failed to disable Nagle on fd=%d", sockfd);
    }
    return ESP_OK;
} // End of web_server_on_open
//-----------------------------------------------------------------------------


/**
 * @brief Add a WebSocket client to the tracking table.
 *
 * @param fd The file descriptor of the WebSocket client to track.
 */
static void track_client(int fd)
{
    if (fd <= 0 || s_ws_mutex == NULL)
    {
        return;
    }

    if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "track_client mutex timeout");
        return;
    }

    for (size_t i = 0; i < sizeof(s_ws_clients) / sizeof(s_ws_clients[0]); ++i)
    {
        if (s_ws_clients[i] == fd)
        {
            xSemaphoreGive(s_ws_mutex);
            return;
        }
    }

    for (size_t i = 0; i < sizeof(s_ws_clients) / sizeof(s_ws_clients[0]); ++i)
    {
        if (s_ws_clients[i] == 0)
        {
            s_ws_clients[i] = fd;
            s_ws_fail_count[i] = 0;
            s_ws_send_inflight[i] = 0;
            s_ws_retry_after_us[i] = 0;
            s_ws_last_ok_us[i] = esp_timer_get_time();
            s_ws_generation[i] = ++s_ws_next_generation;
            xSemaphoreGive(s_ws_mutex);
            web_stats_publish(false, false);
            return;
        }
    }

    xSemaphoreGive(s_ws_mutex);
    ESP_LOGW(TAG, "WS client table full, cannot track fd=%d", fd);
} // End of track_client
//-----------------------------------------------------------------------------



/**
 * @brief Remove a WebSocket client from the tracking list.
 * 
 * @param fd The file descriptor of the WebSocket client to untrack.
 */
static void untrack_client(int fd)
{
    if (fd <= 0 || s_ws_mutex == NULL)
    {
        return;
    }

    if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "untrack_client mutex timeout");
        return;
    }

    for (size_t i = 0; i < sizeof(s_ws_clients) / sizeof(s_ws_clients[0]); ++i)
    {
        if (s_ws_clients[i] == fd)
        {
            s_ws_clients[i] = 0;
            s_ws_fail_count[i] = 0;
            s_ws_send_inflight[i] = 0;
            s_ws_retry_after_us[i] = 0;
            s_ws_last_ok_us[i] = 0;
            xSemaphoreGive(s_ws_mutex);
            web_stats_publish(false, false);
            return;
        }
    }

    xSemaphoreGive(s_ws_mutex);
} // End of untrack_client
//-----------------------------------------------------------------------------

static void clear_client_slot(int fd, uint32_t generation)
{
    if (fd <= 0 || s_ws_mutex == NULL)
    {
        return;
    }

    if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        ESP_LOGW(TAG, "clear_client_slot mutex timeout");
        return;
    }

    for (size_t i = 0; i < sizeof(s_ws_clients) / sizeof(s_ws_clients[0]); ++i)
    {
        if (s_ws_clients[i] == fd && s_ws_generation[i] == generation)
        {
            s_ws_clients[i] = 0;
            s_ws_fail_count[i] = 0;
            s_ws_send_inflight[i] = 0;
            s_ws_retry_after_us[i] = 0;
            s_ws_last_ok_us[i] = 0;
            break;
        }
    }

    xSemaphoreGive(s_ws_mutex);
    web_stats_publish(false, false);
}

static void ws_record_issue(bool dropped, int fd, esp_err_t err)
{
    uint32_t count;
    portENTER_CRITICAL(&s_ws_stats_mux);
    count = dropped ? ++s_ws_dropped_count : ++s_ws_failed_count;
    portEXIT_CRITICAL(&s_ws_stats_mux);
    if (count == 1 || count % 10 == 0)
    {
        ESP_LOGW(TAG, "WS %s count=%" PRIu32 " fd=%d err=%s",
                 dropped ? "dropped" : "send failure", count, fd, esp_err_to_name(err));
    }
}

static void ws_finish_send(int socket, uint32_t generation, esp_err_t err)
{
    bool close_client = false;
    bool matched = false;
    xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
    for (size_t i = 0; i < sizeof(s_ws_clients) / sizeof(s_ws_clients[0]); ++i)
    {
        if (s_ws_clients[i] == socket && s_ws_generation[i] == generation)
        {
            matched = true;
            s_ws_send_inflight[i] = 0;
            if (err == ESP_OK)
            {
                s_ws_fail_count[i] = 0;
                s_ws_retry_after_us[i] = 0;
                s_ws_last_ok_us[i] = esp_timer_get_time();
            }
            else
            {
                int64_t now_us = esp_timer_get_time();
                if (s_ws_fail_count[i] < UINT8_MAX)
                {
                    s_ws_fail_count[i]++;
                }
                s_ws_retry_after_us[i] = now_us + 1500000;
                close_client = s_ws_fail_count[i] >= 30 ||
                    (s_ws_last_ok_us[i] > 0 && now_us - s_ws_last_ok_us[i] > 30000000);
                if (close_client)
                {
                    s_ws_clients[i] = 0;
                }
            }
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);

    if (err != ESP_OK)
    {
        ws_record_issue(false, socket, err);
    }
    if (matched && close_client)
    {
        ESP_LOGW(TAG, "Closing stalled WS client fd=%d", socket);
        httpd_sess_trigger_close(s_server, socket);
        web_stats_publish(false, false);
    }
}

static void ws_async_send_complete(esp_err_t err, int socket, void *arg)
{
    ws_async_send_ctx_t *ctx = (ws_async_send_ctx_t *)arg;
    if (ctx != NULL)
    {
        ws_finish_send(socket, ctx->generation, err);
        free(ctx->payload);
        free(ctx);
    }
}



/**
 * @brief Handle WebSocket connections and messages.
 * 
 * @param req The HTTP request object representing the WebSocket connection.
 * @return ESP_OK on success, or an error code on failure.
 */
static esp_err_t ws_handler(httpd_req_t *req)
{
    // The first call is the HTTP GET upgrade handshake, not a WS data frame.
    if (req->method == HTTP_GET)
    {
        track_client(httpd_req_to_sockfd(req));
        return ESP_OK;
    }

    int sockfd = httpd_req_to_sockfd(req);
    track_client(sockfd);

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = NULL,
        .len = 0,
    };

    esp_err_t err = httpd_ws_recv_frame(req, &frame, 0);
    if (err != ESP_OK) {
        untrack_client(sockfd);
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE) {
        untrack_client(sockfd);
        return ESP_OK;
    }

    if (frame.type != HTTPD_WS_TYPE_TEXT) { return ESP_OK; }

    if (frame.len == 0) { return ESP_OK; }

    char *payload = calloc(1, frame.len + 1);
    if (!payload) { return ESP_ERR_NO_MEM; }

    frame.payload = (uint8_t *)payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err == ESP_OK)
    {
        cJSON * root = json_service_crc32_envelope_decode(payload);
        if (root != NULL)
        {
            json_service_dispatcher_core0(root);
            char *encoded_msg = json_service_crc32_envelope_encode(root);
            if (encoded_msg == NULL)
            {
                ESP_LOGE(TAG, "Failed to encode WebSocket response");
                err = ESP_ERR_NO_MEM;
            }
            else
            {
                httpd_ws_frame_t out_frame = {
                    .type = HTTPD_WS_TYPE_TEXT,
                    .payload = (uint8_t *)encoded_msg,
                    .len = strlen(encoded_msg),
                };
                err = httpd_ws_send_frame(req, &out_frame);
                free(encoded_msg);
            }
            cJSON_Delete(root);
        }
    }
    else
    {
        untrack_client(sockfd);
    }
    if (payload) { free(payload); }
    return err;
} // End of ws_handler
//-----------------------------------------------------------------------------


/**
 * @brief Start the HTTP server and register URI handlers.
 * 
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t web_server_start(void)
{
    if (s_server)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(mount_littlefs(), TAG, "littlefs mount failed");

    size_t fs_total = 0;
    size_t fs_used = 0;
    if (esp_littlefs_info(s_mounted_label, &fs_total, &fs_used) == ESP_OK)
    {
        filesystem_status_t fs_status = {
            .is_mounted = true,
            .total_space_bytes = (uint32_t)fs_total,
            .used_space_bytes = (uint32_t)fs_used,
        };
        (void)system_status_set_filesystem_status(&fs_status);
    }

    if (s_ws_mutex == NULL)
    {
        s_ws_mutex = xSemaphoreCreateMutex();
        if (s_ws_mutex == NULL)
        {
            ESP_LOGE(TAG, "Failed to create WS mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    // Core 0 also runs Wi-Fi, lwIP and BLE; core 1 only hosts the controller tasks.
    config.core_id = WEB_SERVER_CORE;
    config.lru_purge_enable = true;
    config.max_open_sockets = WEB_SERVER_MAX_OPEN_SOCKETS;
    config.max_uri_handlers = 16;
    config.open_fn = web_server_on_open;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "httpd start failed");
    web_stats_publish(false, false);

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
    };
    httpd_uri_t app_js_uri = {
        .uri = "/js/app.js",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t message_parser_uri = {
        .uri = "/js/communication/message_parser.js",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t chart_manager_uri = {
        .uri = "/js/chart_manager.js",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t crc_wrapper_uri = {
        .uri = "/js/communication/crc32_wrapper.js",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t ws_manager_uri = {
        .uri = "/js/communication/ws_manager.js",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t callback_manager_uri = {
        .uri = "/js/communication/callback_manager.js",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t globals_uri = {
        .uri = "/js/globals.js",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t callbacks_uri = {
        .uri = "/js/callbacks.js",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t styles_uri = {
        .uri = "/css/styles.css",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t uplot_uri = {
        .uri = "/vendor/uPlot.iife.min.js",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t ws_uri = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = ws_handler,
        .is_websocket = true,
    };
    
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &index_uri), TAG, "index handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &app_js_uri), TAG, "app_js handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &message_parser_uri), TAG, "message_parser handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &chart_manager_uri), TAG, "chart_manager handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &ws_manager_uri), TAG, "ws_manager handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &crc_wrapper_uri), TAG, "crc wrapper handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &callback_manager_uri), TAG, "callback_manager handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &globals_uri), TAG, "globals handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &callbacks_uri), TAG, "callbacks handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &styles_uri), TAG, "css handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &uplot_uri), TAG, "uPlot handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &ws_uri), TAG, "ws handler failed");

    return ESP_OK;
} // End of web_server_start
//-----------------------------------------------------------------------------



/**
 * @brief Broadcast a JSON message to all connected WebSocket clients.
 * 
 * @param json The JSON string to broadcast.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t web_server_broadcast_json(const char *json)
{
    if (!s_server || !json || s_ws_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };

    bool failed = false;
    const size_t ws_slots = sizeof(s_ws_clients) / sizeof(s_ws_clients[0]);
    for (size_t i = 0; i < ws_slots; ++i)
    {
        xSemaphoreTake(s_ws_mutex, portMAX_DELAY);
        int client_fd = s_ws_clients[i];
        uint32_t generation = s_ws_generation[i];
        bool busy = client_fd > 0 &&
            (s_ws_send_inflight[i] != 0 || s_ws_retry_after_us[i] > esp_timer_get_time());
        if (client_fd > 0 && !busy)
        {
            s_ws_send_inflight[i] = 1;
        }
        xSemaphoreGive(s_ws_mutex);

        if (client_fd <= 0)
        {
            continue;
        }
        if (busy)
        {
            ws_record_issue(true, client_fd, ESP_ERR_INVALID_STATE);
            failed = true;
            continue;
        }
        if (httpd_ws_get_fd_info(s_server, client_fd) != HTTPD_WS_CLIENT_WEBSOCKET)
        {
            ws_finish_send(client_fd, generation, ESP_ERR_INVALID_STATE);
            clear_client_slot(client_fd, generation);
            failed = true;
            continue;
        }

        ws_async_send_ctx_t *ctx = calloc(1, sizeof(*ctx));
        if (ctx != NULL)
        {
            ctx->generation = generation;
            ctx->payload = strdup(json);
        }
        if (ctx == NULL || ctx->payload == NULL)
        {
            free(ctx);
            ws_finish_send(client_fd, generation, ESP_ERR_NO_MEM);
            failed = true;
            continue;
        }

        frame.payload = (uint8_t *)ctx->payload;
        frame.len = strlen(ctx->payload);
        esp_err_t err = httpd_ws_send_data_async(s_server, client_fd, &frame, ws_async_send_complete, ctx);
        if (err != ESP_OK)
        {
            ws_finish_send(client_fd, generation, err);
            free(ctx->payload);
            free(ctx);
            failed = true;
        }
    }

    return failed ? ESP_FAIL : ESP_OK;
} // End of web_server_broadcast_json
//-----------------------------------------------------------------------------