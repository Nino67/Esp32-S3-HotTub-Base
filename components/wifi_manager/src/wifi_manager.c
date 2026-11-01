#include "wifi_manager.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "nvs_storage.h"
#include "system_status.h"

static const char *TAG = "wifi_manager";

#define WIFI_PROVISION_GOT_IP_BIT       BIT0
#define WIFI_PROVISION_DISCONNECTED_BIT BIT1
#define WIFI_MANAGER_QUEUE_LENGTH      6
#define WIFI_PROVISION_ATTEMPTS         5
#define WIFI_PROVISION_WAIT_MS          5000
#define WIFI_STA_MAX_ATTEMPTS           5
#define WIFI_STA_RETRY_DELAY_MS         1000
#define WIFI_FALLBACK_RETRY_MS          60000
#define WIFI_FALLBACK_RETRY_ATTEMPTS    2
#define WIFI_HOSTNAME                   "hottub"

typedef struct
{
    wifi_credentials_t credentials;
    uint32_t request_id;
} wifi_provision_request_data_t;

typedef enum
{
    WIFI_MANAGER_WORK_CONNECT_STA,
    WIFI_MANAGER_WORK_ENTER_AP_FALLBACK,
    WIFI_MANAGER_WORK_PROVISION,
    WIFI_MANAGER_WORK_RETRY_STA,
    WIFI_MANAGER_WORK_REFRESH_AP_CLIENTS
} wifi_manager_work_type_t;

typedef struct
{
    wifi_manager_work_type_t type;
    wifi_provision_request_data_t provision;
} wifi_manager_work_t;

static bool s_started;
static bool s_sta_enabled;
static bool s_ap_enabled;
static bool s_provisioning_active;
static bool s_active_credentials_valid;
static bool s_ap_fallback_active;
static bool s_ap_fallback_pending;
static bool s_sta_attempt_pending;
static bool s_fallback_retry_pending;
static uint8_t s_sta_connect_attempts;
static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_provision_queue;
static EventGroupHandle_t s_wifi_events;
static esp_timer_handle_t s_fallback_retry_timer;
static wifi_status_t s_status;
static wireless_status_t s_published_network;
static wifi_credentials_t s_active_credentials;
static wifi_manager_provision_result_cb_t s_provision_result_cb;
static char s_sta_ssid[33];

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

static bool credentials_are_valid(const wifi_credentials_t *credentials)
{
    if (!credentials)
    {
        return false;
    }

    size_t ssid_length = strnlen(credentials->ssid, sizeof(credentials->ssid));
    size_t password_length = strnlen(credentials->password, sizeof(credentials->password));
    return ssid_length > 0 &&
           ssid_length < sizeof(credentials->ssid) &&
           password_length < sizeof(credentials->password);
}

static void fill_ap_config(wifi_config_t *config)
{
    memset(config, 0, sizeof(*config));
    strlcpy((char *)config->ap.ssid, "HotTub-Controller", sizeof(config->ap.ssid));
    strlcpy((char *)config->ap.password, "hottub123", sizeof(config->ap.password));
    config->ap.ssid_len = strlen((char *)config->ap.ssid);
    config->ap.channel = 1;
    config->ap.authmode = WIFI_AUTH_WPA2_PSK;
    config->ap.max_connection = 4;
    config->ap.ssid_hidden = 0;
}

static void fill_sta_config(const wifi_credentials_t *credentials, wifi_config_t *config)
{
    memset(config, 0, sizeof(*config));
    size_t ssid_length = strnlen(credentials->ssid, sizeof(credentials->ssid));
    size_t password_length = strnlen(credentials->password, sizeof(credentials->password));
    memcpy(config->sta.ssid, credentials->ssid, ssid_length);
    memcpy(config->sta.password, credentials->password, password_length);
    config->sta.threshold.authmode = credentials->password[0] == '\0' ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
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
    if (event)
    {
        esp_ip4addr_ntoa(&event->ip_info.ip, s_status.sta_ip, sizeof(s_status.sta_ip));
    }
}

static void publish_network_status(wifi_mode_state_t mode, const char *ssid, const char *ip)
{
    // Start from the last published copy so the AP client list survives mode/IP updates.
    wireless_status_t status;
    lock_state();
    status = s_published_network;
    unlock_state();

    status.current_mode = mode;
    status.internet_connected = mode == WIFI_STATE_STA_CONNECTED;
    strlcpy(status.active_ssid, ssid ? ssid : "", sizeof(status.active_ssid));
    strlcpy(status.ip_address, ip ? ip : "", sizeof(status.ip_address));

    lock_state();
    s_published_network = status;
    unlock_state();

    esp_err_t err = system_status_set_network_status(&status);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to publish network status: %s", esp_err_to_name(err));
    }
}

static bool enqueue_wifi_manager_work(const wifi_manager_work_t *work)
{
    if (!s_provision_queue || xQueueSend(s_provision_queue, work, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Wi-Fi manager work queue is unavailable or full");
        return false;
    }

    return true;
}

static void request_ap_client_refresh(void)
{
    // Best effort: a dropped refresh is corrected by the next client event.
    const wifi_manager_work_t work = { .type = WIFI_MANAGER_WORK_REFRESH_AP_CLIENTS };
    if (s_provision_queue)
    {
        (void)xQueueSend(s_provision_queue, &work, 0);
    }
}

static void refresh_ap_clients(void)
{
    wireless_status_t status;
    lock_state();
    status = s_published_network;
    unlock_state();

    status.connected_client_count = 0;
    memset(status.clients, 0, sizeof(status.clients));

    wifi_sta_list_t list = {0};
    if (esp_wifi_ap_get_sta_list(&list) == ESP_OK)
    {
        const size_t stored = list.num < MAX_CLIENTS_COUNT ? (size_t)list.num : MAX_CLIENTS_COUNT;
        status.connected_client_count = (uint8_t)list.num;
        for (size_t i = 0; i < stored; i++)
        {
            memcpy(status.clients[i].mac_address, list.sta[i].mac, sizeof(status.clients[i].mac_address));
            status.clients[i].rssi = list.sta[i].rssi;
        }
    }

    lock_state();
    s_published_network = status;
    unlock_state();
    (void)system_status_set_network_status(&status);
}

static void schedule_sta_attempt(void)
{
    wifi_manager_work_t work = {0};
    bool should_queue = false;
    lock_state();
    if (s_sta_enabled &&
        !s_ap_fallback_active &&
        !s_ap_fallback_pending &&
        !s_provisioning_active &&
        !s_sta_attempt_pending)
    {
        s_sta_attempt_pending = true;
        if (s_sta_connect_attempts >= WIFI_STA_MAX_ATTEMPTS)
        {
            s_ap_fallback_pending = true;
            work.type = WIFI_MANAGER_WORK_ENTER_AP_FALLBACK;
        }
        else
        {
            work.type = WIFI_MANAGER_WORK_CONNECT_STA;
        }
        should_queue = true;
    }
    unlock_state();

    if (should_queue && !enqueue_wifi_manager_work(&work))
    {
        lock_state();
        s_sta_attempt_pending = false;
        s_ap_fallback_pending = false;
        unlock_state();
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_START)
    {
        bool ap_enabled;
        char ap_ip[sizeof(s_status.ap_ip)];
        lock_state();
        ap_enabled = s_ap_enabled;
        if (ap_enabled)
        {
            s_status.ap_started = true;
            update_ap_ip();
        }
        strlcpy(ap_ip, s_status.ap_ip, sizeof(ap_ip));
        unlock_state();

        if (ap_enabled)
        {
            ESP_LOGI(TAG, "AP started at %s", ap_ip);
        }
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        request_ap_client_refresh();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        request_ap_client_refresh();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STOP)
    {
        lock_state();
        s_status.ap_started = false;
        s_status.ap_ip[0] = '\0';
        unlock_state();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_STOP)
    {
        lock_state();
        s_status.sta_connected = false;
        s_status.sta_ip[0] = '\0';
        unlock_state();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        schedule_sta_attempt();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        bool provisioning_active;
        bool sta_enabled;
        lock_state();
        s_status.sta_connected = false;
        s_status.sta_ip[0] = '\0';
        provisioning_active = s_provisioning_active;
        sta_enabled = s_sta_enabled;
        unlock_state();

        if (provisioning_active)
        {
            if (s_wifi_events)
            {
                xEventGroupSetBits(s_wifi_events, WIFI_PROVISION_DISCONNECTED_BIT);
            }
        }
        else if (sta_enabled)
        {
            schedule_sta_attempt();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        const ip_event_got_ip_t *event = (const ip_event_got_ip_t *)event_data;
        if (!event)
        {
            return;
        }

        esp_err_t ps_err = esp_wifi_set_ps(WIFI_PS_NONE);
        if (ps_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to disable STA power-save after IP: %s", esp_err_to_name(ps_err));
        }

        char connected_ssid[sizeof(s_sta_ssid)];
        char connected_ip[sizeof(s_status.sta_ip)];
        lock_state();
        s_status.sta_connected = true;
        s_sta_connect_attempts = 0;
        s_sta_attempt_pending = false;
        s_ap_fallback_pending = false;
        update_sta_ip(event);
        strlcpy(connected_ssid, s_sta_ssid, sizeof(connected_ssid));
        strlcpy(connected_ip, s_status.sta_ip, sizeof(connected_ip));
        unlock_state();

        publish_network_status(WIFI_STATE_STA_CONNECTED, connected_ssid, connected_ip);
        ESP_LOGI(TAG, "STA got IP: %s", connected_ip);
        if (s_wifi_events)
        {
            xEventGroupSetBits(s_wifi_events, WIFI_PROVISION_GOT_IP_BIT);
        }
    }
}

static esp_err_t wait_for_candidate_connection(unsigned attempts)
{
    for (unsigned attempt = 0; attempt < attempts; attempt++)
    {
        esp_err_t connect_err = esp_wifi_connect();
        if (connect_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Wi-Fi provisioning connect attempt %u returned %s",
                     attempt + 1, esp_err_to_name(connect_err));
        }

        EventBits_t bits = xEventGroupWaitBits(
            s_wifi_events,
            WIFI_PROVISION_GOT_IP_BIT | WIFI_PROVISION_DISCONNECTED_BIT,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(WIFI_PROVISION_WAIT_MS));
        if ((bits & WIFI_PROVISION_GOT_IP_BIT) != 0)
        {
            return ESP_OK;
        }
    }

    return ESP_ERR_TIMEOUT;
}

static void publish_ap_fallback_status(void)
{
    lock_state();
    update_ap_ip();
    char ap_ip[sizeof(s_status.ap_ip)];
    strlcpy(ap_ip, s_status.ap_ip, sizeof(ap_ip));
    unlock_state();

    publish_network_status(WIFI_STATE_AP_FALLBACK, "HotTub-Controller", ap_ip);
}

static void enter_ap_fallback(void)
{
    lock_state();
    bool should_enter = s_ap_fallback_pending && !s_ap_fallback_active;
    s_ap_fallback_pending = false;
    s_sta_attempt_pending = false;
    if (should_enter)
    {
        s_ap_fallback_active = true;
        s_sta_enabled = false;
        s_ap_enabled = true;
        s_status.sta_connected = false;
        s_status.sta_ip[0] = '\0';
    }
    unlock_state();

    if (!should_enter)
    {
        return;
    }

    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK && stop_err != ESP_ERR_WIFI_NOT_STARTED)
    {
        ESP_LOGE(TAG, "Failed to stop STA before AP fallback: %s", esp_err_to_name(stop_err));
    }

    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to switch to AP fallback mode: %s", esp_err_to_name(err));
        publish_network_status(WIFI_STATE_DISCONNECTED, "", "");
        return;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start AP fallback: %s", esp_err_to_name(err));
        publish_network_status(WIFI_STATE_DISCONNECTED, "", "");
        return;
    }

    ESP_LOGW(TAG, "STA connection attempts exhausted; serving recovery AP at 192.168.4.1");
    publish_ap_fallback_status();
}

static void process_sta_attempt(void)
{
    vTaskDelay(pdMS_TO_TICKS(WIFI_STA_RETRY_DELAY_MS));

    lock_state();
    s_sta_attempt_pending = false;
    bool should_connect = s_sta_enabled &&
        !s_ap_fallback_active &&
        !s_provisioning_active &&
        !s_status.sta_connected &&
        s_sta_connect_attempts < WIFI_STA_MAX_ATTEMPTS;
    uint8_t attempt = 0;
    char ssid[sizeof(s_sta_ssid)];
    strlcpy(ssid, s_sta_ssid, sizeof(ssid));
    if (should_connect)
    {
        s_sta_connect_attempts++;
        attempt = s_sta_connect_attempts;
    }
    unlock_state();

    if (!should_connect)
    {
        return;
    }

    publish_network_status(WIFI_STATE_STA_CONNECTING, ssid, "");
    ESP_LOGI(TAG, "Starting STA connection attempt %u/%u", (unsigned)attempt, (unsigned)WIFI_STA_MAX_ATTEMPTS);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "STA connection attempt failed to start: %s", esp_err_to_name(err));
        schedule_sta_attempt();
    }
}

static esp_err_t restore_previous_configuration(const wifi_credentials_t *previous_credentials, bool had_previous_credentials, bool restore_ap_fallback)
{
    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_STARTED)
    {
        ESP_LOGE(TAG, "Failed to stop Wi-Fi before rollback: %s", esp_err_to_name(err));
    }

    if (had_previous_credentials && !restore_ap_fallback)
    {
        wifi_config_t previous_config;
        fill_sta_config(previous_credentials, &previous_config);

        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to restore STA mode: %s", esp_err_to_name(err));
        }
        esp_err_t config_err = esp_wifi_set_config(WIFI_IF_STA, &previous_config);
        if (config_err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to restore prior Wi-Fi configuration: %s", esp_err_to_name(config_err));
            if (err == ESP_OK)
            {
                err = config_err;
            }
        }

        lock_state();
        s_sta_enabled = true;
        s_ap_enabled = false;
        s_active_credentials = *previous_credentials;
        s_active_credentials_valid = true;
        strlcpy(s_sta_ssid, previous_credentials->ssid, sizeof(s_sta_ssid));
        s_provisioning_active = true;
        s_ap_fallback_active = false;
        s_ap_fallback_pending = false;
        s_sta_connect_attempts = 0;
        s_sta_attempt_pending = false;
        unlock_state();

        esp_err_t start_err = esp_wifi_start();
        if (start_err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to restart Wi-Fi with previous credentials: %s", esp_err_to_name(start_err));
            if (err == ESP_OK)
            {
                err = start_err;
            }
        }

        lock_state();
        s_provisioning_active = false;
        unlock_state();
        publish_network_status(WIFI_STATE_STA_CONNECTING, previous_credentials->ssid, "");
        schedule_sta_attempt();
        return err;
    }

    err = esp_wifi_set_mode(WIFI_MODE_AP);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to restore AP fallback mode: %s", esp_err_to_name(err));
    }

    lock_state();
    s_sta_enabled = false;
    s_ap_enabled = true;
    s_ap_fallback_active = true;
    s_ap_fallback_pending = false;
    s_sta_connect_attempts = 0;
    s_sta_attempt_pending = false;
    if (had_previous_credentials)
    {
        s_active_credentials = *previous_credentials;
        s_active_credentials_valid = true;
        strlcpy(s_sta_ssid, previous_credentials->ssid, sizeof(s_sta_ssid));
    }
    else
    {
        memset(&s_active_credentials, 0, sizeof(s_active_credentials));
        s_active_credentials_valid = false;
        s_sta_ssid[0] = '\0';
    }
    s_provisioning_active = true;
    s_status.sta_connected = false;
    s_status.sta_ip[0] = '\0';
    unlock_state();

    esp_err_t start_err = err == ESP_OK ? esp_wifi_start() : err;
    if (start_err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to restart AP fallback: %s", esp_err_to_name(start_err));
    }

    lock_state();
    s_provisioning_active = false;
    unlock_state();

    if (err != ESP_OK || start_err != ESP_OK)
    {
        publish_network_status(WIFI_STATE_DISCONNECTED, "", "");
        return err != ESP_OK ? err : start_err;
    }

    publish_ap_fallback_status();
    return ESP_OK;
}

static void finish_provisioning(uint32_t request_id, esp_err_t result)
{
    wifi_manager_provision_result_cb_t callback;
    lock_state();
    s_provisioning_active = false;
    callback = s_provision_result_cb;
    unlock_state();

    if (callback)
    {
        callback(request_id, result);
    }
}

static void process_provision_request(const wifi_provision_request_data_t *request)
{
    wifi_credentials_t previous_credentials = {0};
    bool had_previous_credentials;
    bool was_ap_fallback;
    lock_state();
    had_previous_credentials = s_active_credentials_valid;
    was_ap_fallback = s_ap_fallback_active;
    if (had_previous_credentials)
    {
        previous_credentials = s_active_credentials;
    }
    s_sta_enabled = true;
    s_ap_enabled = was_ap_fallback || !had_previous_credentials;
    s_sta_connect_attempts = 0;
    s_sta_attempt_pending = false;
    s_ap_fallback_pending = false;
    strlcpy(s_sta_ssid, request->credentials.ssid, sizeof(s_sta_ssid));
    unlock_state();

    xEventGroupClearBits(s_wifi_events, WIFI_PROVISION_GOT_IP_BIT | WIFI_PROVISION_DISCONNECTED_BIT);

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to stop Wi-Fi before credential trial: %s", esp_err_to_name(err));
        goto failed;
    }

    wifi_mode_t trial_mode = had_previous_credentials && !was_ap_fallback
        ? WIFI_MODE_STA
        : WIFI_MODE_APSTA;
    err = esp_wifi_set_mode(trial_mode);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set Wi-Fi trial mode: %s", esp_err_to_name(err));
        goto failed;
    }

    wifi_config_t candidate_config;
    fill_sta_config(&request->credentials, &candidate_config);
    err = esp_wifi_set_config(WIFI_IF_STA, &candidate_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure candidate Wi-Fi network: %s", esp_err_to_name(err));
        goto failed;
    }

    err = esp_wifi_start();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to start Wi-Fi for credential trial: %s", esp_err_to_name(err));
        goto failed;
    }

    publish_network_status(WIFI_STATE_STA_CONNECTING, request->credentials.ssid, "");
    err = wait_for_candidate_connection(WIFI_PROVISION_ATTEMPTS);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Wi-Fi credential trial did not connect");
        goto failed;
    }

    if (was_ap_fallback || !had_previous_credentials)
    {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to disable AP after Wi-Fi trial: %s", esp_err_to_name(err));
            goto failed;
        }

        wifi_ap_record_t connected_ap = {0};
        if (esp_wifi_sta_get_ap_info(&connected_ap) != ESP_OK)
        {
            ESP_LOGW(TAG, "STA link dropped while finalizing credentials; retrying");
            err = wait_for_candidate_connection(WIFI_PROVISION_ATTEMPTS);
            if (err != ESP_OK)
            {
                goto failed;
            }
        }
    }

    err = nvs_storage_set_wifi_credentials(request->credentials.ssid, request->credentials.password);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to persist Wi-Fi credentials: %s", esp_err_to_name(err));
        goto failed;
    }

    lock_state();
    s_active_credentials = request->credentials;
    s_active_credentials_valid = true;
    s_sta_enabled = true;
    s_ap_enabled = false;
    s_ap_fallback_active = false;
    s_ap_fallback_pending = false;
    s_sta_connect_attempts = 0;
    s_sta_attempt_pending = false;
    strlcpy(s_sta_ssid, request->credentials.ssid, sizeof(s_sta_ssid));
    unlock_state();

    ESP_LOGI(TAG, "Wi-Fi credentials updated and connection established");
    finish_provisioning(request->request_id, ESP_OK);
    return;

failed:
    {
        bool return_to_ap = was_ap_fallback || !had_previous_credentials;
        esp_err_t rollback_err = restore_previous_configuration(
            &previous_credentials,
            had_previous_credentials,
            return_to_ap);
        if (rollback_err != ESP_OK)
        {
            ESP_LOGE(TAG, "Wi-Fi rollback did not complete cleanly: %s", esp_err_to_name(rollback_err));
        }
    }
    finish_provisioning(request->request_id, err);
}

static void fallback_retry_timer_cb(void *arg)
{
    (void)arg;
    bool should_retry;
    lock_state();
    should_retry = s_ap_fallback_active &&
        s_active_credentials_valid &&
        !s_provisioning_active &&
        !s_fallback_retry_pending;
    if (should_retry)
    {
        s_fallback_retry_pending = true;
    }
    unlock_state();

    const wifi_manager_work_t work = { .type = WIFI_MANAGER_WORK_RETRY_STA };
    if (should_retry && !enqueue_wifi_manager_work(&work))
    {
        lock_state();
        s_fallback_retry_pending = false;
        unlock_state();
    }
}

/**
 * Periodically retry the saved network while serving the recovery AP, so a router that boots
 * later than the controller (for example after a power cut) is picked up without a reboot.
 */
static void process_fallback_retry(void)
{
    wifi_credentials_t credentials = {0};
    bool proceed;
    lock_state();
    proceed = s_ap_fallback_active && s_active_credentials_valid && !s_provisioning_active;
    if (proceed)
    {
        credentials = s_active_credentials;
        s_provisioning_active = true;
    }
    else
    {
        s_fallback_retry_pending = false;
    }
    unlock_state();

    if (!proceed)
    {
        return;
    }

    // Joining the router moves the AP to the router's channel, which would drop recovery clients.
    wifi_sta_list_t clients = {0};
    if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK && clients.num > 0)
    {
        ESP_LOGI(TAG, "Skipping STA retry: %u client(s) are using the recovery AP", (unsigned)clients.num);
        lock_state();
        s_provisioning_active = false;
        s_fallback_retry_pending = false;
        unlock_state();
        return;
    }

    ESP_LOGI(TAG, "Retrying saved Wi-Fi network from recovery AP mode");
    wifi_config_t sta_config;
    fill_sta_config(&credentials, &sta_config);
    esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err == ESP_OK)
    {
        err = esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    }
    if (err == ESP_OK)
    {
        xEventGroupClearBits(s_wifi_events, WIFI_PROVISION_GOT_IP_BIT | WIFI_PROVISION_DISCONNECTED_BIT);
        err = wait_for_candidate_connection(WIFI_FALLBACK_RETRY_ATTEMPTS);
    }
    if (err == ESP_OK)
    {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    }

    if (err == ESP_OK)
    {
        lock_state();
        s_sta_enabled = true;
        s_ap_enabled = false;
        s_ap_fallback_active = false;
        s_ap_fallback_pending = false;
        s_sta_connect_attempts = 0;
        s_sta_attempt_pending = false;
        s_provisioning_active = false;
        s_fallback_retry_pending = false;
        unlock_state();
        ESP_LOGI(TAG, "Recovered saved Wi-Fi connection; recovery AP stopped");
        return;
    }

    ESP_LOGW(TAG, "Saved Wi-Fi network still unavailable (%s); staying in recovery AP mode", esp_err_to_name(err));
    (void)esp_wifi_disconnect();
    (void)esp_wifi_set_mode(WIFI_MODE_AP);
    lock_state();
    s_status.sta_connected = false;
    s_status.sta_ip[0] = '\0';
    s_provisioning_active = false;
    s_fallback_retry_pending = false;
    unlock_state();
    publish_ap_fallback_status();
}

static void wifi_provisioning_task(void *arg)
{
    (void)arg;
    wifi_manager_work_t work;
    for (;;)
    {
        if (xQueueReceive(s_provision_queue, &work, portMAX_DELAY) == pdTRUE)
        {
            switch (work.type)
            {
            case WIFI_MANAGER_WORK_CONNECT_STA:
                process_sta_attempt();
                break;
            case WIFI_MANAGER_WORK_ENTER_AP_FALLBACK:
                enter_ap_fallback();
                break;
            case WIFI_MANAGER_WORK_PROVISION:
                process_provision_request(&work.provision);
                break;
            case WIFI_MANAGER_WORK_RETRY_STA:
                process_fallback_retry();
                break;
            case WIFI_MANAGER_WORK_REFRESH_AP_CLIENTS:
                refresh_ap_clients();
                break;
            default:
                ESP_LOGE(TAG, "Unknown Wi-Fi manager work item");
                break;
            }
        }
    }
}

esp_err_t wifi_manager_start(const wifi_credentials_t *sta_credentials)
{
    if (s_started)
    {
        return ESP_OK;
    }

    bool has_sta_credentials = sta_credentials && sta_credentials->ssid[0] != '\0';
    if (has_sta_credentials && !credentials_are_valid(sta_credentials))
    {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!has_sta_credentials && sta_credentials && sta_credentials->password[0] != '\0')
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_mutex)
    {
        s_mutex = xSemaphoreCreateMutex();
        if (!s_mutex)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    s_wifi_events = xEventGroupCreate();
    s_provision_queue = xQueueCreate(WIFI_MANAGER_QUEUE_LENGTH, sizeof(wifi_manager_work_t));
    if (!s_wifi_events || !s_provision_queue)
    {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop init failed");

    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();
    if (!sta_netif || !ap_netif)
    {
        return ESP_FAIL;
    }
    if (esp_netif_set_hostname(sta_netif, WIFI_HOSTNAME) != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to set STA hostname");
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "wifi storage failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL), TAG, "wifi event handler failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL), TAG, "ip event handler failed");

    wifi_config_t ap_config;
    fill_ap_config(&ap_config);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "wifi mode initialization failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap_config), TAG, "ap config failed");

    // 20 MHz channels are far more reliable than 40 MHz on a crowded 2.4 GHz band shared with BLE.
    if (esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20) != ESP_OK ||
        esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20) != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to restrict Wi-Fi to 20 MHz channels");
    }

    lock_state();
    s_sta_enabled = has_sta_credentials;
    s_ap_enabled = !has_sta_credentials;
    s_ap_fallback_active = !has_sta_credentials;
    s_ap_fallback_pending = false;
    s_sta_attempt_pending = false;
    s_sta_connect_attempts = 0;
    s_active_credentials_valid = has_sta_credentials;
    if (has_sta_credentials)
    {
        s_active_credentials = *sta_credentials;
        strlcpy(s_sta_ssid, sta_credentials->ssid, sizeof(s_sta_ssid));
    }
    else
    {
        memset(&s_active_credentials, 0, sizeof(s_active_credentials));
        s_sta_ssid[0] = '\0';
    }
    unlock_state();

    if (has_sta_credentials)
    {
        wifi_config_t sta_config;
        fill_sta_config(sta_credentials, &sta_config);
        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &sta_config), TAG, "sta config failed");
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "wifi mode failed");
    }
    else
    {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "wifi mode failed");
    }

    if (xTaskCreate(wifi_provisioning_task, "wifi_provision", 4096, NULL, 5, NULL) != pdPASS)
    {
        return ESP_ERR_NO_MEM;
    }

    const esp_timer_create_args_t retry_timer_args = {
        .callback = fallback_retry_timer_cb,
        .name = "wifi_fallback_retry",
    };
    ESP_RETURN_ON_ERROR(esp_timer_create(&retry_timer_args, &s_fallback_retry_timer), TAG, "retry timer create failed");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_fallback_retry_timer, (uint64_t)WIFI_FALLBACK_RETRY_MS * 1000ULL),
                        TAG, "retry timer start failed");

    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");
    s_started = true;

    if (!has_sta_credentials)
    {
        lock_state();
        s_status.ap_started = true;
        unlock_state();
        publish_ap_fallback_status();
    }

    return ESP_OK;
}

esp_err_t wifi_manager_set_provision_result_callback(wifi_manager_provision_result_cb_t callback)
{
    if (!callback)
    {
        return ESP_ERR_INVALID_ARG;
    }

    lock_state();
    s_provision_result_cb = callback;
    unlock_state();
    return ESP_OK;
}

esp_err_t wifi_manager_provision(const wifi_credentials_t *credentials, uint32_t request_id)
{
    if (!credentials_are_valid(credentials))
    {
        return ESP_ERR_INVALID_SIZE;
    }
    if (!s_started || !s_provision_queue)
    {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_manager_work_t work = {
        .type = WIFI_MANAGER_WORK_PROVISION,
        .provision = {
            .credentials = *credentials,
            .request_id = request_id,
        },
    };

    lock_state();
    if (s_provisioning_active)
    {
        unlock_state();
        return ESP_ERR_INVALID_STATE;
    }
    s_provisioning_active = true;
    unlock_state();

    if (xQueueSend(s_provision_queue, &work, 0) != pdTRUE)
    {
        lock_state();
        s_provisioning_active = false;
        unlock_state();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t wifi_manager_get_status(wifi_status_t *status)
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
