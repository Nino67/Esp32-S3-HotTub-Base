#include "hot_tub_web_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_check.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"

#include "hot_tub_device_state.h"

static const char *TAG = "hot_tub_web";
static const char *LFS_BASE_PATH = "/littlefs";
static const char *LFS_INDEX = "/littlefs/www/index.html";

static httpd_handle_t s_server;
static int s_ws_clients[4];

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
}

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
}

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
}

static esp_err_t index_handler(httpd_req_t *req)
{
    return send_file(req, LFS_INDEX);
}

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
}

static esp_err_t ws_handler(httpd_req_t *req)
{
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
        return err;
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
            httpd_ws_send_frame(req, &out);
        }
    }

    free(payload);
    return err;
}

esp_err_t hot_tub_web_server_start(void)
{
    if (s_server)
    {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(mount_littlefs(), TAG, "littlefs mount failed");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 8192;
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
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &styles_uri), TAG, "css handler failed");
    ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &ws_uri), TAG, "ws handler failed");

    return ESP_OK;
}

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
}
