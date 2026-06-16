#include "hot_tub_wifi_manager.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "hot_tub_device_state.h"

static const char *TAG = "hot_tub_wifi";

static bool s_started;
static bool s_sta_enabled;
static bool s_ap_enabled;
static SemaphoreHandle_t s_mutex;
static hot_tub_wifi_status_t s_status;

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

static void update_ap_ip(void)
{
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!ap_netif)
    {
        return;
    }

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(ap_netif, &ip_info) == ESP_OK)
    {
        esp_ip4addr_ntoa(&ip_info.ip, s_status.ap_ip, sizeof(s_status.ap_ip));
    }
}

static void update_sta_ip(const ip_event_got_ip_t *event)
{
    if (!event)
    {
        return;
    }

    esp_ip4addr_ntoa(&event->ip_info.ip, s_status.sta_ip, sizeof(s_status.sta_ip));
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START)
    {
        if (!s_ap_enabled)
        {
            return;
        }

        lock_state();
        s_status.ap_started = true;
        update_ap_ip();
        unlock_state();
        ESP_LOGI(TAG, "AP started at %s", s_status.ap_ip);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        if (s_sta_enabled)
        {
            esp_wifi_connect();
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        lock_state();
        s_status.sta_connected = false;
        s_status.sta_ip[0] = '\0';
        unlock_state();
        hot_tub_device_state_set_wifi_connected(false, NULL);
        if (s_sta_enabled)
        {
            esp_wifi_connect();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        lock_state();
        s_status.sta_connected = true;
        update_sta_ip(event);
        unlock_state();
        hot_tub_device_state_set_wifi_connected(true, s_status.sta_ip);
        ESP_LOGI(TAG, "STA got IP: %s", s_status.sta_ip);
    }
}

esp_err_t hot_tub_wifi_manager_start(const hot_tub_wifi_credentials_t *sta_credentials)
{
    if (s_started)
    {
        return ESP_OK;
    }

    if (!s_mutex)
    {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop init failed");

    s_sta_enabled = sta_credentials && sta_credentials->ssid[0] != '\0';
    s_ap_enabled = !s_sta_enabled;

    if (s_sta_enabled)
    {
        esp_netif_create_default_wifi_sta();
    }
    else
    {
        esp_netif_create_default_wifi_ap();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "wifi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL), TAG, "ip event handler failed");

    wifi_config_t ap_config = {0};
    strlcpy((char *)ap_config.ap.ssid, "HotTub-Controller", sizeof(ap_config.ap.ssid));
    strlcpy((char *)ap_config.ap.password, "hottub123", sizeof(ap_config.ap.password));
    ap_config.ap.ssid_len = strlen((char *)ap_config.ap.ssid);
    ap_config.ap.channel = 1;
    ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    ap_config.ap.max_connection = 4;
    ap_config.ap.ssid_hidden = 0;
    if (strlen((char *)ap_config.ap.password) == 0)
    {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    if (s_sta_enabled)
    {
        wifi_config_t sta_config = {0};
        strlcpy((char *)sta_config.sta.ssid, sta_credentials->ssid, sizeof(sta_config.sta.ssid));
        strlcpy((char *)sta_config.sta.password, sta_credentials->password, sizeof(sta_config.sta.password));
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "sta config failed");
    }
    else
    {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "wifi mode failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "ap config failed");
    }

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    if (s_ap_enabled)
    {
        lock_state();
        s_status.ap_started = true;
        update_ap_ip();
        unlock_state();
    }

    if (s_sta_enabled)
    {
        ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "sta connect failed");
    }

    hot_tub_device_state_set_wifi_connected(false, NULL);
    s_started = true;
    return ESP_OK;
}

esp_err_t hot_tub_wifi_manager_get_status(hot_tub_wifi_status_t *status)
{
    if (!status)
    {
        return ESP_ERR_INVALID_ARG;
    }

    lock_state();
    *status = s_status;
    unlock_state();
    return ESP_OK;
}

bool hot_tub_wifi_manager_is_connected(void)
{
    bool connected;
    lock_state();
    connected = s_status.sta_connected;
    unlock_state();
    return connected;
}
