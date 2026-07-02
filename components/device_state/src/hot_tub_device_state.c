#include "hot_tub_device_state.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "ntp_time_sync.h"

static SemaphoreHandle_t s_mutex;
static hot_tub_device_state_t s_state;


/// Function prototypes
void hot_tub_device_state_set_time_stamp(const struct tm *time_stamp);



static void lock_state(void)
{
    if (s_mutex)
    {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
    }
}

static void unlock_state(void)
{
    if (s_mutex)
    {
        xSemaphoreGive(s_mutex);
    }
}

esp_err_t hot_tub_device_state_init(void)
{
    if (!s_mutex)
    {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    lock_state();
    memset(&s_state, 0, sizeof(s_state));
    unlock_state();
    return ESP_OK;
}

esp_err_t hot_tub_device_state_get_snapshot(hot_tub_device_state_t *state)
{
    if (!state)
    {
        return ESP_ERR_INVALID_ARG;
    }

    lock_state();
    *state = s_state;
    unlock_state();
    return ESP_OK;
}

esp_err_t hot_tub_device_state_format_json(char *buffer, size_t buffer_size)
{
    if (!buffer || buffer_size == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    struct tm now_tm;
    if (ntp_utils_time_get_local(&now_tm) == ESP_OK) {
        hot_tub_device_state_set_time_stamp(&now_tm);
    }

    lock_state();
    int written = snprintf(buffer, buffer_size,
                           "{ \"id\":%d,\n"
                           "  \"time_stamp\":\"%s\",\n"
                           "  \"wifi_connected\":%s,\n"
                           "  \"ble_connected\":%s,\n"
                           "  \"ota_pending\":%s,\n"
                           "  \"ota_status\":\"%s\",\n"
                           "  \"ota_progress\":%d,\n"
                           "  \"wifi_ip\":\"%s\",\n"
                           "  \"last_command_length\":%zu }",
                           s_state.id,
                           s_state.time_stamp,
                           s_state.wifi_connected ? "true" : "false",
                           s_state.ble_connected ? "true" : "false",
                           s_state.ota_pending ? "true" : "false",
                           s_state.ota_status,
                           s_state.ota_progress,
                           s_state.wifi_ip,
                           s_state.last_command_length);
    unlock_state();

    if (written < 0 || (size_t)written >= buffer_size)
    {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void hot_tub_device_state_set_wifi_connected(bool connected, const char *ip_address)
{
    lock_state();
    s_state.wifi_connected = connected;
    if (ip_address)
    {
        strlcpy(s_state.wifi_ip, ip_address, sizeof(s_state.wifi_ip));
    }
    else if (!connected)
    {
        s_state.wifi_ip[0] = '\0';
    }
    unlock_state();
}

void hot_tub_device_state_set_ble_connected(bool connected)
{
    lock_state();
    s_state.ble_connected = connected;
    unlock_state();
}

void hot_tub_device_state_set_ota_pending(bool pending)
{
    lock_state();
    s_state.ota_pending = pending;
    unlock_state();
}

void hot_tub_device_state_set_ota_status(const char *status)
{
    lock_state();
    if (status)
    {
        strlcpy(s_state.ota_status, status, sizeof(s_state.ota_status));
    }
    else
    {
        s_state.ota_status[0] = '\0';
    }
    unlock_state();
}

void hot_tub_device_state_set_ota_progress(int progress)
{
    lock_state();
    if (progress < 0)
    {
        progress = 0;
    }
    else if (progress > 100)
    {
        progress = 100;
    }
    s_state.ota_progress = progress;
    unlock_state();
}

void hot_tub_device_state_set_last_command(const char *command)
{
    lock_state();
    s_state.last_command_length = command ? strlen(command) : 0;
    unlock_state();
}


void hot_tub_device_state_set_time_stamp(const struct tm *time_stamp)
{
    char buf[32] = {0};
    if (time_stamp && strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", time_stamp) > 0)
    {
        ESP_LOGI("time_task", "Tick time: %s", buf);
    }

    lock_state();
    if (time_stamp)
    {
        strlcpy(s_state.time_stamp, buf, sizeof(s_state.time_stamp));
    }
    unlock_state();
}

void hot_tub_device_state_set_id(int id)
{
    lock_state();
    s_state.id = id;
    unlock_state();
}