#include "ble_service.h"

#include <inttypes.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"

#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
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

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(0xab, 0x90, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34);
static const ble_uuid128_t s_rx_uuid = BLE_UUID128_INIT(0xac, 0x90, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34);
static const ble_uuid128_t s_tx_uuid = BLE_UUID128_INIT(0xad, 0x90, 0x78, 0x56, 0x34, 0x12, 0x34, 0x12, 0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x12, 0x34);

static int rx_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static int tx_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
static void start_advertising(void);

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
            // char state[256];
            // if (hot_tub_device_state_format_json(state, sizeof(state)) == ESP_OK)
            // {
            //     struct os_mbuf *om = ble_hs_mbuf_from_flat(state, strlen(state));
            //     if (om)
            //     {
            //         ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
            //     }
            // }
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

    struct os_mbuf *om = ble_hs_mbuf_from_flat(json, strlen(json));
    if (!om)
    {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_tx_val_handle, om);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}
