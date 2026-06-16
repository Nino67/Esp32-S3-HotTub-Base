#include "hot_tub_device_state.h"

#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_mutex;
static hot_tub_device_state_t s_state;

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

    lock_state();
    int written = snprintf(buffer, buffer_size,
                           "{\"wifi_connected\":%s,\"ble_connected\":%s,\"ota_pending\":%s,\"wifi_ip\":\"%s\",\"last_command_length\":%u}",
                           s_state.wifi_connected ? "true" : "false",
                           s_state.ble_connected ? "true" : "false",
                           s_state.ota_pending ? "true" : "false",
                           s_state.wifi_ip,
                           (unsigned int)s_state.last_command_length);
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

void hot_tub_device_state_set_last_command(const char *command)
{
    lock_state();
    s_state.last_command_length = command ? strlen(command) : 0;
    unlock_state();
}
