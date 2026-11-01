#include "ble_service.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "cJSON.h"
#include "json_service.h"
#include "ota_manager.h"
#include "system_status.h"
#include "wifi_manager.h"

#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_store.h"


static const char *TAG = "ble_service";

#define BLE_DEBUG_OTA_URL "https://raw.githubusercontent.com/Nino67/Esp32-S3-HotTub-Base/main/firmware/hot_tub_controller.bin"
#define BLE_ADV_INTERVAL_MS 1000

static int BLE_PASSKEY = 123456;

extern void ble_store_config_init(void);

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_rx_val_handle;
static uint16_t s_tx_val_handle;
static uint32_t s_tx_msg_id;
static bool s_status_compact_mode;
static portMUX_TYPE s_ble_ota_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_ble_ota_request_pending;

typedef enum
{
    BLE_OTA_UPDATE_GITHUB,
    BLE_OTA_UPDATE_MANIFEST,
} ble_ota_update_type_t;

typedef struct
{
    uint32_t request_id;
    ble_ota_update_type_t type;
    char *url;
    char *storage_url;
} ble_ota_request_t;

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(0xab, 0x90, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(0xac, 0x90, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(0xad, 0x90, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34);

static int rx_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int tx_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static void start_advertising(void);
esp_err_t ble_service_send_json(const char *json);
static esp_err_t ble_service_notify_bytes(const char *payload, size_t payload_len);
static esp_err_t ble_service_send_chunked(const char *payload, size_t payload_len);
static esp_err_t ble_service_send_wrapped_cjson(cJSON *json);
static esp_err_t ble_service_send_system_status_json(bool compact_mode);
static esp_err_t ble_service_send_connect_network_json(void);
static esp_err_t ble_service_send_status_for_current_mode(void);
static esp_err_t ble_service_send_wifi_credentials_result(uint32_t request_id, const char *status, const char *reason);
static esp_err_t ble_service_send_ota_result(uint32_t request_id, const char *status, const char *reason);
static const char *wifi_mode_to_string(wifi_mode_state_t mode);
static void trim_in_place(char *s);
static bool str_equals_ignore_case(const char *a, const char *b);
static bool ble_service_connection_is_secure(uint16_t conn_handle);
static int ble_service_handle_rx_command(const char *payload, uint16_t conn_handle);
static int ble_service_handle_rx_json_request(const char *payload, uint16_t conn_handle, bool *handled);
static int ble_service_queue_ota_request(cJSON *root, const char *command);
static int ble_service_queue_debug_github_ota(void);
static void ble_service_ota_update_task(void *arg);
static void ble_service_on_wifi_provision_result(uint32_t request_id, esp_err_t result);


/**/
static void nimble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_reset(int reason)
{
    ESP_LOGE(TAG, "Resetting BLE host, reason=%d", reason);
}

static void on_sync(void)
{
    ESP_LOGI(TAG, "BLE host synced");
    start_advertising();
}

static void ble_service_publish_status(bool advertising)
{
    ble_config_status_t status = {0};
    status.is_advertising = advertising;

    struct ble_gap_conn_desc conn_desc = {0};
    if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE && ble_gap_conn_find(s_conn_handle, &conn_desc) == 0)
    {
        status.is_paired = conn_desc.sec_state.encrypted && conn_desc.sec_state.bonded;
        status.mtu_size = ble_att_mtu(s_conn_handle);
    }

    int bonded_count = 0;
    if (ble_store_util_count(BLE_STORE_OBJ_TYPE_PEER_SEC, &bonded_count) == 0 && bonded_count > 0)
    {
        status.bonded_devices_count = bonded_count > UINT8_MAX ? UINT8_MAX : (uint8_t)bonded_count;
    }

    (void)system_status_set_ble_status(&status);
} // end of ble_service_publish_status()


/**
 * @brief Callback function for GATT registration events.
 *
 * This function is called whenever a GATT service, characteristic, or descriptor is registered.
 *
 * @param ctxt The GATT registration context.
 * @param arg User-defined argument.
 */
static void gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char buf[BLE_UUID_STR_LEN];

    switch (ctxt->op)
    {
    case BLE_GATT_REGISTER_OP_SVC:
        ESP_LOGI(TAG, "registered service %s with handle=%d",
                 ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                 ctxt->svc.handle);
        break;
    case BLE_GATT_REGISTER_OP_CHR:
        ESP_LOGI(TAG, "registering characteristic %s def_handle=%d val_handle=%d",
                 ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                 ctxt->chr.def_handle,
                 ctxt->chr.val_handle);
        break;
    case BLE_GATT_REGISTER_OP_DSC:
        ESP_LOGI(TAG, "registering descriptor %s handle=%d",
                 ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                 ctxt->dsc.handle);
        break;
    default:
        break;
    }
} // end of gatt_register_cb()
/******************************************************************************/


/**
 * @brief Callback function for GAP events.
 *
 * This function is called whenever a GAP event occurs, such as connection, disconnection,
 * subscription, advertising completion, or passkey actions.
 *
 * @param event The GAP event.
 * @param arg User-defined argument.
 * @return 0 on success, or an appropriate error code on failure.
 */
static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            s_conn_handle = event->connect.conn_handle;
            ble_service_publish_status(false);
            ESP_LOGI(TAG, "BLE connected");

            esp_err_t send_err = ble_service_send_connect_network_json();
            if (send_err != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to send BLE network summary on connect: %s", esp_err_to_name(send_err));
            }
        }
        else
        {
            ESP_LOGW(TAG, "BLE connection failed, restarting advertising");
            start_advertising();
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "BLE disconnected");
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
    case BLE_GAP_EVENT_MTU:
        ble_service_publish_status(false);
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == s_tx_val_handle && event->subscribe.cur_notify)
        {
            esp_err_t send_err = ble_service_send_connect_network_json();
            if (send_err != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to send BLE network summary on subscribe: %s", esp_err_to_name(send_err));
            }
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_DISP)
        {
            struct ble_sm_io pkey = {0};
            pkey.action = BLE_SM_IOACT_DISP;
            pkey.passkey = BLE_PASSKEY; // esp_random() % 1000000;
            ESP_LOGI(TAG, "BLE passkey: %06" PRIu32, pkey.passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }
        return 0;

    default:
        return 0;
    }
} // end of gap_event()
/******************************************************************************/


/**
 * @brief Start BLE advertising with the specified parameters.
 *
 * This function sets up the advertising fields and starts BLE advertising.
 */
static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_hs_adv_fields rsp_fields = {0};
    const char *device_name = "HotTub";

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    rsp_fields.name = (uint8_t *)device_name;
    rsp_fields.name_len = strlen(device_name);
    rsp_fields.name_is_complete = 1;
    rsp_fields.uuids128 = (ble_uuid128_t *)&s_service_uuid;
    rsp_fields.num_uuids128 = 1;
    rsp_fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: %d", rc);
        return;
    }

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_fields failed: %d", rc);
        return;
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    // Advertise at 1 s intervals: Wi-Fi and BLE share one radio, so frequent adverts cost Wi-Fi airtime.
    adv_params.itvl_min = BLE_GAP_ADV_ITVL_MS(BLE_ADV_INTERVAL_MS);
    adv_params.itvl_max = BLE_GAP_ADV_ITVL_MS(BLE_ADV_INTERVAL_MS);
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
        ble_service_publish_status(false);
        return;
    }
    ble_service_publish_status(true);
} // end of start_advertising()
/******************************************************************************/


/**
 * @brief Send a JSON payload over BLE, wrapped in a CRC envelope if necessary.
 *
 * @param json The cJSON object to send.
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
static esp_err_t ble_service_send_wrapped_cjson(cJSON *json)
{
    if (json == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // BLE terminal testing mode: bypass CRC envelope and send plain JSON.
    char *plain_json = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (plain_json == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ble_service_send_json(plain_json);
    free(plain_json);
    return err;
} // end of ble_service_send_wrapped_cjson()
/******************************************************************************/


/**
 * @brief Send the system status as a JSON payload over BLE.
 *
 * @param compact_mode If true, send a compact version of the system status.
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
static esp_err_t ble_service_send_system_status_json(bool compact_mode)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t snapshot_err = system_status_snapshot();
    if (snapshot_err != ESP_OK)
    {
        ESP_LOGW(TAG, "system_status_snapshot failed: %s", esp_err_to_name(snapshot_err));
    }

    cJSON *status_json = NULL;
    if (compact_mode)
    {
        SystemStatus_t status_copy;
        (void)system_status_get_copy(&status_copy);
        const SystemStatus_t *status = &status_copy;
        status_json = cJSON_CreateObject();
        if (status_json == NULL)
        {
            return ESP_ERR_NO_MEM;
        }

        cJSON_AddNumberToObject(status_json, "id", status->id);
        cJSON_AddStringToObject(status_json, "timestamp", status->timestamp);
        cJSON_AddNumberToObject(status_json, "uptime_us", (double)status->uptime_us);
        cJSON_AddNumberToObject(status_json, "internal_temperature", status->internal_temperature);

        cJSON *network = cJSON_AddObjectToObject(status_json, "network");
        if (network)
        {
            cJSON_AddStringToObject(network, "mode", wifi_mode_to_string(status->network.current_mode));
            cJSON_AddStringToObject(network, "active_ssid", status->network.active_ssid);
            cJSON_AddNumberToObject(network, "connected_client_count", (double)status->network.connected_client_count);
        }

        cJSON *firmware = cJSON_AddObjectToObject(status_json, "firmware");
        if (firmware)
        {
            cJSON_AddNumberToObject(firmware, "running_partition_slot", (double)status->firmware.running_partition_slot);
            cJSON_AddNumberToObject(firmware, "active_partition_slot", (double)status->firmware.active_partition_slot);
        }
    }
    else
    {
        status_json = system_status_get_json();
    }

    if (status_json == NULL)
    {
        return ESP_FAIL;
    }

    return ble_service_send_wrapped_cjson(status_json);
} // end of ble_service_send_system_status_json()
/******************************************************************************/


/**
 * @brief Send the network connection summary as a JSON payload over BLE.
 *
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
static esp_err_t ble_service_send_connect_network_json(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t snapshot_err = system_status_snapshot();
    if (snapshot_err != ESP_OK)
    {
        ESP_LOGW(TAG, "system_status_snapshot failed for connect summary: %s", esp_err_to_name(snapshot_err));
    }

    SystemStatus_t status_copy;
    (void)system_status_get_copy(&status_copy);
    const SystemStatus_t *status = &status_copy;
    wifi_status_t wifi = {0};
    (void)wifi_manager_get_status(&wifi);

    const char *ip = "";
    if (status->network.current_mode == WIFI_STATE_STA_CONNECTED)
    {
        ip = wifi.sta_ip;
    }
    else if (status->network.current_mode == WIFI_STATE_AP_FALLBACK)
    {
        ip = wifi.ap_ip;
    }

    cJSON *summary = cJSON_CreateObject();
    if (summary == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(summary, "type", "evt");
    cJSON_AddStringToObject(summary, "cmd", "network.summary");
    cJSON_AddStringToObject(summary, "mode", wifi_mode_to_string(status->network.current_mode));
    cJSON_AddStringToObject(summary, "ssid", status->network.active_ssid);
    cJSON_AddStringToObject(summary, "ip", ip);

    return ble_service_send_wrapped_cjson(summary);
} // end of ble_service_send_connect_network_json()
/******************************************************************************/

static esp_err_t ble_service_send_wifi_credentials_result(uint32_t request_id, const char *status, const char *reason)
{
    cJSON *result = cJSON_CreateObject();
    if (!result)
    {
        return ESP_ERR_NO_MEM;
    }

    if (!cJSON_AddNumberToObject(result, "id", request_id) ||
        !cJSON_AddStringToObject(result, "type", "evt") ||
        !cJSON_AddStringToObject(result, "cmd", "wifi.credentials.result") ||
        !cJSON_AddStringToObject(result, "status", status) ||
        (reason && !cJSON_AddStringToObject(result, "reason", reason)))
    {
        cJSON_Delete(result);
        return ESP_ERR_NO_MEM;
    }

    return ble_service_send_wrapped_cjson(result);
}

static esp_err_t ble_service_send_ota_result(uint32_t request_id, const char *status, const char *reason)
{
    cJSON *result = cJSON_CreateObject();
    if (!result)
    {
        return ESP_ERR_NO_MEM;
    }

    if (!cJSON_AddNumberToObject(result, "id", request_id) ||
        !cJSON_AddStringToObject(result, "type", "evt") ||
        !cJSON_AddStringToObject(result, "cmd", "ota.manager.update.result") ||
        !cJSON_AddStringToObject(result, "status", status) ||
        (reason && !cJSON_AddStringToObject(result, "reason", reason)))
    {
        cJSON_Delete(result);
        return ESP_ERR_NO_MEM;
    }

    return ble_service_send_wrapped_cjson(result);
}

static bool ble_service_reserve_ota_request(void)
{
    bool reserved = false;
    portENTER_CRITICAL(&s_ble_ota_mux);
    if (!s_ble_ota_request_pending && !ota_manager_is_update_in_progress())
    {
        s_ble_ota_request_pending = true;
        reserved = true;
    }
    portEXIT_CRITICAL(&s_ble_ota_mux);
    return reserved;
}

static void ble_service_release_ota_request(void)
{
    portENTER_CRITICAL(&s_ble_ota_mux);
    s_ble_ota_request_pending = false;
    portEXIT_CRITICAL(&s_ble_ota_mux);
}

static void ble_service_ota_update_task(void *arg)
{
    ble_ota_request_t *request = arg;
    if (request == NULL)
    {
        ESP_LOGE(TAG, "BLE OTA task received a null request");
        ble_service_release_ota_request();
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "BLE OTA task created; waiting for request acknowledgement");
    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    esp_err_t err;
    if (request->type == BLE_OTA_UPDATE_MANIFEST)
    {
        ESP_LOGI(TAG, "Starting manifest OTA requested over BLE (request id=%" PRIu32 ")", request->request_id);
        err = ota_manager_trigger_manifest_ota(request->url);
    }
    else
    {
        ESP_LOGI(TAG, "Starting GitHub OTA requested over BLE (request id=%" PRIu32 ")", request->request_id);
        err = ota_manager_trigger_github_ota(request->url, request->storage_url);
    }

    const char *status = err == ESP_OK ? "completed" : "failed";
    const char *reason = err == ESP_OK ? NULL : esp_err_to_name(err);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "BLE OTA request failed (request id=%" PRIu32 "): %s", request->request_id, reason);
    }
    else
    {
        ESP_LOGI(TAG, "BLE OTA routine returned successfully (request id=%" PRIu32 ")", request->request_id);
    }

    esp_err_t send_err = ble_service_send_ota_result(request->request_id, status, reason);
    if (send_err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to send BLE OTA result: %s", esp_err_to_name(send_err));
    }
    else
    {
        ESP_LOGI(TAG, "BLE OTA result event sent (request id=%" PRIu32 ", status=%s)", request->request_id, status);
    }

    free(request->url);
    free(request->storage_url);
    free(request);
    ble_service_release_ota_request();
    vTaskDelete(NULL);
}

static void ble_service_on_wifi_provision_result(uint32_t request_id, esp_err_t result)
{
    const char *status = result == ESP_OK ? "connected" : "failed";
    const char *reason = result == ESP_OK
        ? NULL
        : (result == ESP_ERR_TIMEOUT ? "connection_failed" : "credentials_not_applied");

    esp_err_t send_err = ble_service_send_wifi_credentials_result(request_id, status, reason);
    if (send_err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to send Wi-Fi provisioning result: %s", esp_err_to_name(send_err));
    }
}


/**
 * @brief Send the system status for the current mode as a JSON payload over BLE.
 *
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
static esp_err_t ble_service_send_status_for_current_mode(void)
{
    return ble_service_send_system_status_json(s_status_compact_mode);
} // end of ble_service_send_status_for_current_mode()
/******************************************************************************/


/**
 * @brief Convert a Wi-Fi mode state to a human-readable string.
 *
 * @param mode The Wi-Fi mode state.
 * @return A string representation of the Wi-Fi mode.
 */
static const char *wifi_mode_to_string(wifi_mode_state_t mode)
{
    switch (mode)
    {
    case WIFI_STATE_STA_CONNECTED:
        return "STA";
    case WIFI_STATE_AP_FALLBACK:
        return "AP";
    case WIFI_STATE_STA_CONNECTING:
        return "STA_CONNECTING";
    case WIFI_STATE_DISCONNECTED:
    default:
        return "DISCONNECTED";
    }
} // end of wifi_mode_to_string()
/******************************************************************************/


/**
 * @brief Trim leading and trailing whitespace characters from a string in place.
 *
 * @param s The string to trim.
 */
static void trim_in_place(char *s)
{
    if (s == NULL)
    {
        return;
    }

    size_t len = strlen(s);
    size_t start = 0;
    while (start < len && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n'))
    {
        start++;
    }

    size_t end = len;
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n'))
    {
        end--;
    }

    if (start > 0)
    {
        memmove(s, s + start, end - start);
    }
    s[end - start] = '\0';
} // end of trim_in_place()
/******************************************************************************/


/**
 * @brief Compare two strings for equality, ignoring case.
 *
 * @param a The first string.
 * @param b The second string.
 * @return true if the strings are equal (ignoring case), false otherwise.
 */
static bool str_equals_ignore_case(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
    {
        return false;
    }

    while (*a != '\0' && *b != '\0')
    {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z')
        {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z')
        {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb)
        {
            return false;
        }
        a++;
        b++;
    }

    return *a == '\0' && *b == '\0';
} // end of str_equals_ignore_case()
/******************************************************************************/


/**
 * @brief Handle a received BLE command.
 *
 * @param payload The received command payload.
 */
static bool ble_service_connection_is_secure(uint16_t conn_handle)
{
    struct ble_gap_conn_desc conn_desc = {0};
    if (ble_gap_conn_find(conn_handle, &conn_desc) != 0)
    {
        return false;
    }

    return conn_desc.sec_state.encrypted &&
           conn_desc.sec_state.authenticated &&
           conn_desc.sec_state.bonded;
}

static int ble_service_queue_debug_github_ota(void)
{
    ESP_LOGI(TAG, "Preparing BLE debug OTA request using the configured GitHub firmware URL");
    cJSON *root = cJSON_Parse("{\"params\":{\"url\":\"" BLE_DEBUG_OTA_URL "\"}}");
    if (root == NULL)
    {
        ESP_LOGE(TAG, "Failed to create BLE debug OTA request");
        return BLE_ATT_ERR_UNLIKELY;
    }

    int att_err = ble_service_queue_ota_request(root, "ota.manager.update.github");
    cJSON_Delete(root);
    return att_err;
}

static int ble_service_handle_rx_command(const char *payload, uint16_t conn_handle)
{
    if (!payload)
    {
        return 0;
    }

    bool json_handled = false;
    int att_err = ble_service_handle_rx_json_request(payload, conn_handle, &json_handled);
    if (json_handled || att_err != 0)
    {
        return att_err;
    }

    char cmd[256];
    strlcpy(cmd, payload, sizeof(cmd));
    trim_in_place(cmd);

    if (cmd[0] == '\0')
    {
        return 0;
    }

    if (str_equals_ignore_case(cmd, "ota-update"))
    {
        struct ble_gap_conn_desc conn_desc = {0};
        int conn_err = ble_gap_conn_find(conn_handle, &conn_desc);
        if (conn_err != 0)
        {
            ESP_LOGE(TAG, "Received ota-update but could not inspect BLE connection %u (rc=%d)",
                     (unsigned)conn_handle, conn_err);
            return BLE_ATT_ERR_UNLIKELY;
        }

        ESP_LOGI(TAG,
                 "Received BLE ota-update (conn=%u, encrypted=%d, authenticated=%d, bonded=%d)",
                 (unsigned)conn_handle,
                 conn_desc.sec_state.encrypted ? 1 : 0,
                 conn_desc.sec_state.authenticated ? 1 : 0,
                 conn_desc.sec_state.bonded ? 1 : 0);

        if (!conn_desc.sec_state.encrypted ||
            !conn_desc.sec_state.authenticated ||
            !conn_desc.sec_state.bonded)
        {
            ESP_LOGW(TAG, "Rejecting ota-update: pair and bond this BLE connection before requesting OTA");
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        }

        ESP_LOGI(TAG, "BLE ota-update connection passed security checks");
        int att_err = ble_service_queue_debug_github_ota();
        if (att_err != 0)
        {
            ESP_LOGE(TAG, "Failed to queue BLE ota-update (ATT error=0x%02x)", att_err);
        }
        return att_err;
    }

    if (str_equals_ignore_case(cmd, "STATUS") || str_equals_ignore_case(cmd, "STATUS GET") || str_equals_ignore_case(cmd, "GET STATUS"))
    {
        (void)ble_service_send_status_for_current_mode();
        return 0;
    }

    if (str_equals_ignore_case(cmd, "STATUS FULL"))
    {
        s_status_compact_mode = false;
        (void)ble_service_send_system_status_json(false);
        return 0;
    }

    if (str_equals_ignore_case(cmd, "STATUS COMPACT"))
    {
        s_status_compact_mode = true;
        (void)ble_service_send_system_status_json(true);
        return 0;
    }

    if (str_equals_ignore_case(cmd, "MODE FULL"))
    {
        s_status_compact_mode = false;
        (void)ble_service_send_status_for_current_mode();
        return 0;
    }

    if (str_equals_ignore_case(cmd, "MODE COMPACT"))
    {
        s_status_compact_mode = true;
        (void)ble_service_send_status_for_current_mode();
        return 0;
    }

    return 0;
} // end of ble_service_handle_rx_command()
/******************************************************************************/

static int ble_service_queue_ota_request(cJSON *root, const char *command)
{
    const bool is_manifest = strcmp(command, "ota.manager.update.manifest") == 0;
    ESP_LOGI(TAG, "Queueing BLE OTA request: command=%s", command);
    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    uint32_t request_id = 0;
    if (id_item)
    {
        if (!cJSON_IsNumber(id_item) ||
            id_item->valuedouble < 0 ||
            id_item->valuedouble > UINT32_MAX ||
            id_item->valuedouble != (double)(uint32_t)id_item->valuedouble)
        {
            esp_err_t send_err = ble_service_send_ota_result(0, "rejected", "invalid_request");
            if (send_err != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to send BLE OTA validation result: %s", esp_err_to_name(send_err));
            }
            return 0;
        }
        request_id = (uint32_t)id_item->valuedouble;
    }

    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    cJSON *url_item = cJSON_IsObject(params)
        ? cJSON_GetObjectItemCaseSensitive(params, is_manifest ? "manifest_url" : "url")
        : NULL;
    cJSON *storage_url_item = is_manifest || !cJSON_IsObject(params)
        ? NULL
        : cJSON_GetObjectItemCaseSensitive(params, "storage_url");
    bool request_valid = cJSON_IsString(url_item) &&
        url_item->valuestring != NULL &&
        strncmp(url_item->valuestring, "https://", sizeof("https://") - 1) == 0 &&
        url_item->valuestring[sizeof("https://") - 1] != '\0';

    if (storage_url_item &&
        (!cJSON_IsString(storage_url_item) ||
         storage_url_item->valuestring == NULL ||
         (storage_url_item->valuestring[0] != '\0' &&
          (strncmp(storage_url_item->valuestring, "https://", sizeof("https://") - 1) != 0 ||
           storage_url_item->valuestring[sizeof("https://") - 1] == '\0'))))
    {
        request_valid = false;
    }

    if (!request_valid)
    {
        esp_err_t send_err = ble_service_send_ota_result(request_id, "rejected", "invalid_request");
        if (send_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to send BLE OTA validation result: %s", esp_err_to_name(send_err));
        }
        return 0;
    }

    ble_ota_request_t *request = calloc(1, sizeof(*request));
    if (request == NULL)
    {
        esp_err_t send_err = ble_service_send_ota_result(request_id, "rejected", "internal_error");
        if (send_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to send BLE OTA allocation result: %s", esp_err_to_name(send_err));
        }
        return 0;
    }

    request->request_id = request_id;
    request->type = is_manifest ? BLE_OTA_UPDATE_MANIFEST : BLE_OTA_UPDATE_GITHUB;
    request->url = strdup(url_item->valuestring);
    if (storage_url_item && storage_url_item->valuestring[0] != '\0')
    {
        request->storage_url = strdup(storage_url_item->valuestring);
    }

    if (request->url == NULL || (storage_url_item &&
        storage_url_item->valuestring[0] != '\0' && request->storage_url == NULL))
    {
        free(request->url);
        free(request->storage_url);
        free(request);
        esp_err_t send_err = ble_service_send_ota_result(request_id, "rejected", "internal_error");
        if (send_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to send BLE OTA allocation result: %s", esp_err_to_name(send_err));
        }
        return 0;
    }

    if (!ble_service_reserve_ota_request())
    {
        ESP_LOGW(TAG, "BLE OTA request rejected: another OTA update is active or queued");
        free(request->url);
        free(request->storage_url);
        free(request);
        esp_err_t send_err = ble_service_send_ota_result(request_id, "rejected", "busy");
        if (send_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to send BLE OTA busy result: %s", esp_err_to_name(send_err));
        }
        return 0;
    }

    TaskHandle_t ota_task = NULL;
    BaseType_t task_result = xTaskCreatePinnedToCore(
        ble_service_ota_update_task,
        "ble_ota_update",
        8192,
        request,
        5,
        &ota_task,
        0);
    if (task_result != pdPASS)
    {
        ESP_LOGE(TAG, "Could not create BLE OTA task");
        ble_service_release_ota_request();
        free(request->url);
        free(request->storage_url);
        free(request);
        esp_err_t send_err = ble_service_send_ota_result(request_id, "rejected", "task_start_failed");
        if (send_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to send BLE OTA task error: %s", esp_err_to_name(send_err));
        }
        return 0;
    }

    esp_err_t send_err = ble_service_send_ota_result(request_id, "accepted", NULL);
    if (send_err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to send BLE OTA acknowledgement: %s", esp_err_to_name(send_err));
    }
    else
    {
        ESP_LOGI(TAG, "BLE OTA request accepted (request id=%" PRIu32 ")", request_id);
    }
    xTaskNotifyGive(ota_task);
    ESP_LOGI(TAG, "BLE OTA task notified to start");
    return 0;
}


/**
 * @brief Handle a received JSON request over BLE.
 *
 * @param payload The received JSON payload.
 * @return true if the request was handled, false otherwise.
 */
static int ble_service_handle_rx_json_request(const char *payload, uint16_t conn_handle, bool *handled)
{
    *handled = false;
    cJSON *root = cJSON_Parse(payload);
    if (root == NULL)
    {
        return 0;
    }

    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    bool is_request = cJSON_IsString(type_item)
        && type_item->valuestring != NULL
        && strcmp(type_item->valuestring, "req") == 0
        && cJSON_IsString(cmd_item)
        && cmd_item->valuestring != NULL;

    if (!is_request)
    {
        cJSON_Delete(root);
        return 0;
    }

    if (strcmp(cmd_item->valuestring, "system.status.get") == 0)
    {
        *handled = true;
        cJSON_Delete(root);
        (void)ble_service_send_system_status_json(false);
        return 0;
    }

    if (strcmp(cmd_item->valuestring, "ota.manager.update.github") == 0 ||
        strcmp(cmd_item->valuestring, "ota.manager.update.manifest") == 0)
    {
        *handled = true;
        if (!ble_service_connection_is_secure(conn_handle))
        {
            cJSON_Delete(root);
            return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
        }

        int ota_err = ble_service_queue_ota_request(root, cmd_item->valuestring);
        cJSON_Delete(root);
        return ota_err;
    }

    if (strcmp(cmd_item->valuestring, "wifi.credentials.set") != 0)
    {
        cJSON_Delete(root);
        return 0;
    }

    *handled = true;
    if (!ble_service_connection_is_secure(conn_handle))
    {
        cJSON_Delete(root);
        return BLE_ATT_ERR_INSUFFICIENT_AUTHEN;
    }

    uint32_t request_id = 0;
    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    bool request_id_valid = true;
    if (id_item)
    {
        if (!cJSON_IsNumber(id_item) ||
            id_item->valuedouble < 0 ||
            id_item->valuedouble > UINT32_MAX ||
            id_item->valuedouble != (double)(uint32_t)id_item->valuedouble)
        {
            request_id_valid = false;
        }
        else
        {
            request_id = (uint32_t)id_item->valuedouble;
        }
    }

    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
    cJSON *ssid_item = cJSON_IsObject(params) ? cJSON_GetObjectItemCaseSensitive(params, "ssid") : NULL;
    cJSON *password_item = cJSON_IsObject(params) ? cJSON_GetObjectItemCaseSensitive(params, "password") : NULL;
    bool credentials_valid = request_id_valid &&
        cJSON_IsString(ssid_item) && ssid_item->valuestring &&
        cJSON_IsString(password_item) && password_item->valuestring;

    wifi_credentials_t credentials = {0};
    if (credentials_valid)
    {
        size_t ssid_length = strnlen(ssid_item->valuestring, sizeof(credentials.ssid));
        size_t password_length = strnlen(password_item->valuestring, sizeof(credentials.password));
        credentials_valid = ssid_length > 0 &&
            ssid_length < sizeof(credentials.ssid) &&
            password_length < sizeof(credentials.password);
        if (credentials_valid)
        {
            memcpy(credentials.ssid, ssid_item->valuestring, ssid_length + 1);
            memcpy(credentials.password, password_item->valuestring, password_length + 1);
        }
    }

    cJSON_Delete(root);

    if (!credentials_valid)
    {
        esp_err_t send_err = ble_service_send_wifi_credentials_result(0, "rejected", "invalid_request");
        if (send_err != ESP_OK)
        {
            ESP_LOGW(TAG, "Failed to send Wi-Fi request validation result: %s", esp_err_to_name(send_err));
        }
        return 0;
    }

    esp_err_t provision_err = wifi_manager_provision(&credentials, request_id);
    const char *status = provision_err == ESP_OK ? "accepted" : "rejected";
    const char *reason = provision_err == ESP_ERR_INVALID_STATE ? "busy" :
        (provision_err == ESP_OK ? NULL : "invalid_credentials");
    esp_err_t send_err = ble_service_send_wifi_credentials_result(request_id, status, reason);
    if (send_err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to send Wi-Fi provisioning acknowledgement: %s", esp_err_to_name(send_err));
    }
    return 0;
} // end of ble_service_handle_rx_json_request()
/******************************************************************************/


/**
 * @brief Callback function for handling received BLE write and read requests.
 *
 * @param conn_handle The connection handle.
 * @param attr_handle The attribute handle.
 * @param ctxt The GATT access context.
 * @param arg User-defined argument.
 * @return 0 on success, or an appropriate BLE_ATT_ERR code on failure.
 */
static int rx_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        char payload[256];
        uint16_t payload_len = OS_MBUF_PKTLEN(ctxt->om);
        ESP_LOGI(TAG, "BLE RX write received (conn=%u, bytes=%u)",
                 (unsigned)conn_handle, (unsigned)payload_len);
        if (payload_len >= sizeof(payload))
        {
            ESP_LOGW(TAG, "BLE RX write rejected: payload exceeds %u-byte limit", (unsigned)(sizeof(payload) - 1));
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        if (os_mbuf_copydata(ctxt->om, 0, payload_len, payload) != 0)
        {
            ESP_LOGE(TAG, "BLE RX write rejected: failed to copy write data");
            return BLE_ATT_ERR_UNLIKELY;
        }
        payload[payload_len] = '\0';

        int att_err = ble_service_handle_rx_command(payload, conn_handle);
        if (att_err != 0)
        {
            ESP_LOGW(TAG, "BLE RX write handler returned ATT error 0x%02x", att_err);
        }
        return att_err;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR)
    {
        // char state[256];
        // if (hot_tub_device_state_format_json(state, sizeof(state)) != ESP_OK)
        // {
        //     return BLE_ATT_ERR_UNLIKELY;
        // }

        // int rc = os_mbuf_append(ctxt->om, state, strlen(state));
        // return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
} // end of rx_access_cb()
/******************************************************************************/


/**
 * @brief Callback function for handling BLE TX access.
 *
 * @param conn_handle The connection handle.
 * @param attr_handle The attribute handle.
 * @param ctxt The GATT access context.
 * @param arg User-defined argument.
 * @return 0 on success, or an appropriate BLE_ATT_ERR code on failure.
 */
static int tx_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return 0;
} // end of tx_access_cb()
/******************************************************************************/


/**
 * @brief BLE GATT characteristic definitions.
 */
static const struct ble_gatt_chr_def s_chrs[] = {
    {
        .uuid = &s_rx_uuid.u,
        .access_cb = rx_access_cb,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_READ,
        .val_handle = &s_rx_val_handle,
    },
    {
        .uuid = &s_tx_uuid.u,
        .access_cb = tx_access_cb,
        .flags = BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &s_tx_val_handle,
    },
    {0},
};

static const struct ble_gatt_svc_def s_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_service_uuid.u,
        .characteristics = s_chrs,
    },
    {0},
}; // end of s_svcs[]
/******************************************************************************/


/**
 * @brief Initialize the BLE service.
 *
 * This function initializes the NimBLE stack, configures the BLE GAP and GATT services,
 * and registers the custom BLE service and characteristics.
 *
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
esp_err_t ble_service_init(void)
{
    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble init failed");

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.gatts_register_cb = gatt_register_cb;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_store_config_init();

    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(s_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return esp_err_to_name(rc) ? ESP_FAIL : ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_svcs);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(
        wifi_manager_set_provision_result_callback(ble_service_on_wifi_provision_result),
        TAG,
        "wifi provisioning callback registration failed");

    nimble_port_freertos_init(nimble_host_task);

    // Log the UUIDs for debugging
    // ESP_LOGI(TAG, "s_service_uuid: %s", s_service_uuid.u);
    // ESP_LOGI(TAG, "s_rx_uuid: %s", s_rx_uuid.u);
    // ESP_LOGI(TAG, "s_tx_uuid: %s", s_tx_uuid.u);

    return ESP_OK;
} // end of ble_service_init(void)
/******************************************************************************/


/**
 * @brief Send a JSON payload over the BLE service.
 *
 * This function sends the provided JSON string over the BLE service using chunked notifications.
 *
 * @param json The JSON string to send.
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
esp_err_t ble_service_send_json(const char *json)
{
    if (!json || s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return ble_service_send_chunked(json, strlen(json));
} // end of ble_service_send_json()
/******************************************************************************/


/**
 * @brief Send raw bytes over the BLE service.
 *
 * This function sends the provided raw byte payload over the BLE service using notifications.
 *
 * @param payload The raw byte payload to send.
 * @param payload_len The length of the payload in bytes.
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
static esp_err_t ble_service_notify_bytes(const char *payload, size_t payload_len)
{
    if (!payload || payload_len == 0 || s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return ESP_ERR_INVALID_ARG;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(payload, payload_len);
    if (!om)
    {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
} // end of ble_service_notify_bytes()
/******************************************************************************/


/**
 * @brief Send a large payload over the BLE service using chunked notifications.
 *
 * This function splits the provided payload into smaller chunks and sends them sequentially
 * over the BLE service. It handles the framing and chunking automatically.
 *
 * @param payload The payload to send.
 * @param payload_len The length of the payload in bytes.
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
static esp_err_t ble_service_send_chunked(const char *payload, size_t payload_len)
{
    uint16_t mtu = ble_att_mtu(s_conn_handle);
    size_t max_payload = mtu > 3 ? (size_t)(mtu - 3) : 20;
    if (max_payload < 20)
    {
        max_payload = 20;
    }

    if (payload_len <= max_payload)
    {
        return ble_service_notify_bytes(payload, payload_len);
    }

    const uint32_t message_id = ++s_tx_msg_id;
    char begin_frame[64];
    int begin_len = snprintf(begin_frame, sizeof(begin_frame), "BEGIN:%" PRIu32 ":%u:%u", message_id, (unsigned)payload_len, (unsigned)max_payload);
    if (begin_len <= 0)
    {
        return ESP_FAIL;
    }
    esp_err_t begin_err = ble_service_notify_bytes(begin_frame, (size_t)begin_len);
    if (begin_err != ESP_OK)
    {
        return begin_err;
    }

    const size_t reserved_header = 32;
    if (max_payload <= reserved_header)
    {
        return ESP_FAIL;
    }

    const size_t chunk_data_size = max_payload - reserved_header;
    const size_t total_chunks = (payload_len + chunk_data_size - 1) / chunk_data_size;
    size_t offset = 0;

    for (size_t chunk_idx = 0; chunk_idx < total_chunks; chunk_idx++)
    {
        char header[32] = {0};
        int header_len = snprintf(header, sizeof(header), "CHUNK:%" PRIu32 ":%u/%u:", message_id, (unsigned)(chunk_idx + 1), (unsigned)total_chunks);
        if (header_len <= 0)
        {
            return ESP_FAIL;
        }

        size_t header_len_u = (size_t)header_len;
        size_t available_for_data = max_payload - header_len_u;
        size_t remaining = payload_len - offset;
        size_t current_chunk_len = remaining < available_for_data ? remaining : available_for_data;

        size_t frame_len = header_len_u + current_chunk_len;
        char *frame = malloc(frame_len + 1);
        if (!frame)
        {
            return ESP_ERR_NO_MEM;
        }

        memcpy(frame, header, header_len_u);
        memcpy(frame + header_len_u, payload + offset, current_chunk_len);
        frame[frame_len] = '\0';

        esp_err_t send_err = ble_service_notify_bytes(frame, frame_len);
        free(frame);
        if (send_err != ESP_OK)
        {
            ESP_LOGW(TAG, "BLE chunk send failed at chunk %u/%u", (unsigned)(chunk_idx + 1), (unsigned)total_chunks);
            return send_err;
        }

        offset += current_chunk_len;
    }

    char end_frame[32];
    int end_len = snprintf(end_frame, sizeof(end_frame), "END:%" PRIu32, message_id);
    if (end_len <= 0)
    {
        return ESP_FAIL;
    }
    return ble_service_notify_bytes(end_frame, (size_t)end_len);
} // end of ble_service_send_large_payload()
/******************************************************************************/