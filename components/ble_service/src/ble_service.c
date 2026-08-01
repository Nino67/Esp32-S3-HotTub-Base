#include "ble_service.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"

#include "cJSON.h"
#include "json_service.h"
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

extern void ble_store_config_init(void);

static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_rx_val_handle;
static uint16_t s_tx_val_handle;
static uint32_t s_tx_msg_id;
static bool s_status_compact_mode;

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
static const char *wifi_mode_to_string(wifi_mode_state_t mode);
static void trim_in_place(char *s);
static bool str_equals_ignore_case(const char *a, const char *b);
static void ble_service_handle_rx_command(const char *payload);
static bool ble_service_handle_rx_json_request(const char *payload);

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
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type)
    {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0)
        {
            s_conn_handle = event->connect.conn_handle;
            // hot_tub_device_state_set_ble_connected(true);
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
        // hot_tub_device_state_set_ble_connected(false);
        ESP_LOGI(TAG, "BLE disconnected");
        start_advertising();
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
            pkey.passkey = esp_random() % 1000000;
            ESP_LOGI(TAG, "BLE passkey: %06" PRIu32, pkey.passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &pkey);
        }
        return 0;

    default:
        return 0;
    }
}

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
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, gap_event, NULL);
    if (rc != 0)
    {
        ESP_LOGE(TAG, "ble_gap_adv_start failed: %d", rc);
    }
}

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
}

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
        SystemStatus_t *status = system_status_get();
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
}

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

    SystemStatus_t *status = system_status_get();
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
}

static esp_err_t ble_service_send_status_for_current_mode(void)
{
    return ble_service_send_system_status_json(s_status_compact_mode);
}

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
}

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
}

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
}

static void ble_service_handle_rx_command(const char *payload)
{
    if (payload == NULL)
    {
        return;
    }

    if (ble_service_handle_rx_json_request(payload))
    {
        return;
    }

    char cmd[256];
    strlcpy(cmd, payload, sizeof(cmd));
    trim_in_place(cmd);

    if (cmd[0] == '\0')
    {
        return;
    }

    if (str_equals_ignore_case(cmd, "STATUS") || str_equals_ignore_case(cmd, "STATUS GET") || str_equals_ignore_case(cmd, "GET STATUS"))
    {
        (void)ble_service_send_status_for_current_mode();
        return;
    }

    if (str_equals_ignore_case(cmd, "STATUS FULL"))
    {
        s_status_compact_mode = false;
        (void)ble_service_send_system_status_json(false);
        return;
    }

    if (str_equals_ignore_case(cmd, "STATUS COMPACT"))
    {
        s_status_compact_mode = true;
        (void)ble_service_send_system_status_json(true);
        return;
    }

    if (str_equals_ignore_case(cmd, "MODE FULL"))
    {
        s_status_compact_mode = false;
        (void)ble_service_send_status_for_current_mode();
        return;
    }

    if (str_equals_ignore_case(cmd, "MODE COMPACT"))
    {
        s_status_compact_mode = true;
        (void)ble_service_send_status_for_current_mode();
        return;
    }
}

static bool ble_service_handle_rx_json_request(const char *payload)
{
    cJSON *root = cJSON_Parse(payload);
    if (root == NULL)
    {
        return false;
    }

    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    bool matched = cJSON_IsString(type_item)
        && type_item->valuestring != NULL
        && strcmp(type_item->valuestring, "req") == 0
        && cJSON_IsString(cmd_item)
        && cmd_item->valuestring != NULL
        && strcmp(cmd_item->valuestring, "system.status.get") == 0;

    cJSON_Delete(root);

    if (!matched)
    {
        return false;
    }

    (void)ble_service_send_system_status_json(false);
    return true;
}

static int rx_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR)
    {
        char payload[256];
        int len = OS_MBUF_PKTLEN(ctxt->om);
        if (len >= (int)sizeof(payload))
        {
            len = sizeof(payload) - 1;
        }
        os_mbuf_copydata(ctxt->om, 0, len, payload);
        payload[len] = '\0';

        // hot_tub_device_state_set_last_command(payload);
        ESP_LOGI(TAG, "BLE RX: %s", payload);

        if (s_conn_handle != BLE_HS_CONN_HANDLE_NONE)
        {
            ble_service_handle_rx_command(payload);
        }
        return 0;
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
}

static int tx_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)ctxt;
    (void)arg;
    return 0;
}

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
};

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

    nimble_port_freertos_init(nimble_host_task);

    // Log the UUIDs for debugging
    // ESP_LOGI(TAG, "s_service_uuid: %s", s_service_uuid.u);
    // ESP_LOGI(TAG, "s_rx_uuid: %s", s_rx_uuid.u);
    // ESP_LOGI(TAG, "s_tx_uuid: %s", s_tx_uuid.u);

    return ESP_OK;
}

esp_err_t ble_service_send_json(const char *json)
{
    if (!json || s_conn_handle == BLE_HS_CONN_HANDLE_NONE)
    {
        return ESP_ERR_INVALID_STATE;
    }

    return ble_service_send_chunked(json, strlen(json));
}

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
}

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
}
