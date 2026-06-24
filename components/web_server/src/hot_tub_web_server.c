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
#include "hot_tub_web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_crc.h"

#include "hot_tub_device_state.h"
//------------------------------------------------------------------------------


// GLOBAL STATIC VARIABLES
static const char *TAG = "hot_tub_web";
static const char *LFS_BASE_PATH = "/littlefs";
static const char *LFS_INDEX = "/littlefs/www/index.html";
static httpd_handle_t s_server;
static int s_ws_clients[4];
//------------------------------------------------------------------------------



/**
 * @brief Mount the LittleFS filesystem, 
 * which contains the web assets. 
 */
static esp_err_t mount_littlefs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = LFS_BASE_PATH,
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err == ESP_ERR_INVALID_STATE)
    {
        return ESP_OK;
    }
    return err;
} // End of mount_littlefs
//-----------------------------------------------------------------------------


/**
 * @brief Determine the content type based on the file extension.
 * 
 * @param path The file path to determine the content type for.
 * @return The content type string corresponding to the file extension.
 */
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



/**
 * @brief Send a file as the HTTP response.
 * 
 * @param req The HTTP request object.
 * @param path The file path to send.
 * @return ESP_OK on success, or an error code on failure.
 */
static esp_err_t send_file(httpd_req_t *req, const char *path)
{
    FILE *file = fopen(path, "r");
    if (!file)
    {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
    }

    httpd_resp_set_type(req, content_type_for_path(path));

    char buffer[512];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK)
        {
            fclose(file);
            httpd_resp_sendstr_chunk(req, NULL);
            return ESP_FAIL;
        }
    }

    fclose(file);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
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
    char path[640];
    int n = snprintf(path, sizeof(path), "%s/www%s", LFS_BASE_PATH, req->uri);
    if (n < 0 || n >= (int)sizeof(path))
    {
        return httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "URI too long");
    }
    return send_file(req, path);
}


/**
 * @brief Handle WebSocket connections and messages.
 * 
 * @param req The HTTP request object representing the WebSocket connection.
 * @return ESP_OK on success, or an error code on failure.
 */
static void track_client(int fd)
{
    for (size_t i = 0; i < sizeof(s_ws_clients) / sizeof(s_ws_clients[0]); ++i)
    {
        if (s_ws_clients[i] == fd)
        {
            return;
        }
        if (s_ws_clients[i] == 0)
        {
            s_ws_clients[i] = fd;
            return;
        }
    }
} // End of track_client
//-----------------------------------------------------------------------------



/**
 * @brief Remove a WebSocket client from the tracking list.
 * 
 * @param fd The file descriptor of the WebSocket client to untrack.
 */
static void untrack_client(int fd)
{
    for (size_t i = 0; i < sizeof(s_ws_clients) / sizeof(s_ws_clients[0]); ++i)
    {
        if (s_ws_clients[i] == fd)
        {
            s_ws_clients[i] = 0;
            return;
        }
    }
} // End of untrack_client
//-----------------------------------------------------------------------------



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
    if (err != ESP_OK)
    {
        untrack_client(sockfd);
        return err;
    }

    if (frame.type == HTTPD_WS_TYPE_CLOSE)
    {
        untrack_client(sockfd);
        return ESP_OK;
    }

    if (frame.type != HTTPD_WS_TYPE_TEXT)
    {
        return ESP_OK;
    }

    if (frame.len == 0)
    {
        return ESP_OK;
    }

    char *payload = calloc(1, frame.len + 1);
    if (!payload)
    {
        return ESP_ERR_NO_MEM;
    }

    frame.payload = (uint8_t *)payload;
    err = httpd_ws_recv_frame(req, &frame, frame.len);
    if (err == ESP_OK)
    {
        hot_tub_device_state_set_last_command(payload);

        char response[256];
        if (hot_tub_device_state_format_json(response, sizeof(response)) == ESP_OK)
        {
            httpd_ws_frame_t out = {
                .type = HTTPD_WS_TYPE_TEXT,
                .payload = (uint8_t *)response,
                .len = strlen(response),
            };
            err = httpd_ws_send_frame(req, &out);
        }
    }
    else
    {
        untrack_client(sockfd);
    }

    ESP_LOGI(TAG, "Received WS message: %s", frame.payload);

    free(payload);
    return err;
} // End of ws_handler
//-----------------------------------------------------------------------------



/**
 * @brief Start the HTTP server and register URI handlers.
 * 
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_web_server_start(void)
{
    if (s_server)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(mount_littlefs(), TAG, "littlefs mount failed");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
    config.core_id = 0;
    config.lru_purge_enable = true;
    config.max_uri_handlers = 12;

    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &config), TAG, "httpd start failed");

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_handler,
    };
    httpd_uri_t app_js_uri = {
        .uri = "/app.js",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t crc_wrapper_uri = {
        .uri = "/crc32_wrapper.js",
        .method = HTTP_GET,
        .handler = asset_handler,
    };
    httpd_uri_t styles_uri = {
        .uri = "/styles.css",
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
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &app_js_uri), TAG, "js handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &crc_wrapper_uri), TAG, "crc wrapper handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &styles_uri), TAG, "css handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &ws_uri), TAG, "ws handler failed");

    return ESP_OK;
} // End of hot_tub_web_server_start
//-----------------------------------------------------------------------------



/**
 * @brief Broadcast a JSON message to all connected WebSocket clients.
 * 
 * @param json The JSON string to broadcast.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t hot_tub_web_server_broadcast_json(const char *json)
{
    if (!s_server || !json)
    {
        return ESP_ERR_INVALID_STATE;
    }

    httpd_ws_frame_t frame = {
        .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json,
        .len = strlen(json),
    };

    esp_err_t result = ESP_OK;
    for (size_t i = 0; i < sizeof(s_ws_clients) / sizeof(s_ws_clients[0]); ++i)
    {
        if (s_ws_clients[i] != 0)
        {
            esp_err_t err = httpd_ws_send_frame_async(s_server, s_ws_clients[i], &frame);
            if (err != ESP_OK)
            {
                s_ws_clients[i] = 0;
                result = err;
            }
        }
    }

    return result;
} // End of hot_tub_web_server_broadcast_json
//-----------------------------------------------------------------------------

















// /**
//  * @brief Disconnect all connected WebSocket clients.
//  */
// static void disconnect_all_clients(void)
// {
//     for (size_t i = 0; i < sizeof(s_ws_clients) / sizeof(s_ws_clients[0]); ++i)
//     {
//         if (s_ws_clients[i] != 0)
//         {
//             httpd_ws_frame_t close_frame = {
//                 .type = HTTPD_WS_TYPE_CLOSE,
//                 .payload = NULL,
//                 .len = 0,
//             };
//             httpd_ws_send_frame_async(s_server, s_ws_clients[i], &close_frame);
//             s_ws_clients[i] = 0;
//         }
//     }
// } // End of disconnect_all_clients