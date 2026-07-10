#include "system_status.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/temperature_sensor.h"
#include "cJSON.h"
#include "json_service.h"


static const char *TAG = "system_status";
static const char *DEFAULT_DEVICE_NAME = "HotTub";
static SystemStatus_t s_system_status;
static TaskHandle_t s_core0_task = NULL;
static temperature_sensor_handle_t s_temp_sensor = NULL;

static bool s_temp_sensor_failed = false;
static void system_status_callback(cJSON *data);
bool json_service_register_command(const char *cmd_string, 
                                   json_cmd_callback_t callback, 
                                   uint8_t target_core);


// bool crc32_json_wrapper(const cJSON *json_obj,
//                         char *output,
//                         size_t output_size,
//                         size_t *output_len);

char *json_service_crc32_envelope_encode(const cJSON *json);



static esp_err_t system_status_read_temperature(float *out_temp)
{
    if (out_temp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_temp_sensor_failed) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_temp_sensor == NULL) {
        const temperature_sensor_config_t configs[] = {
            TEMPERATURE_SENSOR_CONFIG_DEFAULT(50, 125),
            TEMPERATURE_SENSOR_CONFIG_DEFAULT(20, 100),
            TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 80),
            TEMPERATURE_SENSOR_CONFIG_DEFAULT(-30, 50),
            TEMPERATURE_SENSOR_CONFIG_DEFAULT(-40, 20),
        };

        esp_err_t last_err = ESP_ERR_INVALID_ARG;

        for (size_t i = 0; i < sizeof(configs) / sizeof(configs[0]); i++) {
            temperature_sensor_config_t tsens_config = configs[i];
            tsens_config.flags.allow_pd = 0;

            esp_err_t err = temperature_sensor_install(&tsens_config, &s_temp_sensor);
            if (err == ESP_OK) {
                err = temperature_sensor_enable(s_temp_sensor);
                if (err == ESP_OK) {
                    break;
                }
                ESP_LOGW(TAG, "temperature_sensor_enable failed: %s", esp_err_to_name(err));
                temperature_sensor_uninstall(s_temp_sensor);
                s_temp_sensor = NULL;
                last_err = err;
                continue;
            }

            last_err = err;
            if (err != ESP_ERR_INVALID_ARG) {
                ESP_LOGW(TAG, "temperature_sensor_install failed: %s", esp_err_to_name(err));
            }
        }

        if (s_temp_sensor == NULL) {
            ESP_LOGW(TAG, "temperature_sensor_install failed after trying supported ranges: %s", esp_err_to_name(last_err));
            s_temp_sensor_failed = true;
            return last_err;
        }
    }

    return temperature_sensor_get_celsius(s_temp_sensor, out_temp);
}

static void system_status_load_reset_reason(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    s_system_status.reset_reason = (uint32_t)reason;
}

static void system_status_update_core_heap(void)
{
    s_system_status.core_0_free_heap_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_system_status.core_1_free_heap_bytes = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_system_status.psram_free_bytes = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

static void system_status_update_task_stack(void)
{
    if (s_core0_task != NULL) {
        s_system_status.core_0_task_stack_high_water_bytes = uxTaskGetStackHighWaterMark(s_core0_task);
    }
}

static void system_status_update_ota_partitions(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running) {
        if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0) {
            s_system_status.firmware.running_partition_slot = 0;
        } else if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_1) {
            s_system_status.firmware.running_partition_slot = 1;
        }
    }
}

static void system_status_update_timestamp(void)
{
    int64_t now_us = esp_timer_get_time();
    if (now_us < 0) {
        now_us = 0;
    }
    s_system_status.last_update_us = (uint64_t)now_us;
    s_system_status.uptime_us = (uint64_t)esp_timer_get_time();

    time_t now = 0;
    if (time(&now) != (time_t)-1) {
        struct tm now_tm;
        if (localtime_r(&now, &now_tm) != NULL) {
            strftime(s_system_status.timestamp, sizeof(s_system_status.timestamp), "%Y-%m-%d %H:%M:%S", &now_tm);
        } else {
            strncpy(s_system_status.timestamp, "1970-01-01 00:00:00", sizeof(s_system_status.timestamp));
            s_system_status.timestamp[sizeof(s_system_status.timestamp) - 1] = '\0';
        }
    } else {
        strncpy(s_system_status.timestamp, "1970-01-01 00:00:00", sizeof(s_system_status.timestamp));
        s_system_status.timestamp[sizeof(s_system_status.timestamp) - 1] = '\0';
    }
}

esp_err_t system_status_init(TaskHandle_t core0_task)
{
    if (core0_task == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_system_status, 0, sizeof(s_system_status));
    s_core0_task = core0_task;
    s_system_status.initialized = true;
    strncpy(s_system_status.device_name, DEFAULT_DEVICE_NAME, sizeof(s_system_status.device_name));
    s_system_status.device_name[sizeof(s_system_status.device_name) - 1] = '\0';
    system_status_load_reset_reason();
    system_status_update_core_heap();
    system_status_update_task_stack();
    system_status_update_ota_partitions();
    system_status_update_timestamp();

    float temp_c = 0.0f;
    if (system_status_read_temperature(&temp_c) == ESP_OK) {
        s_system_status.internal_temperature = temp_c;
    } else {
        s_system_status.internal_temperature = 0.0f;
    }

    s_system_status.firmware.current_ota_state = OTA_READY;

    // Register the "system_status" command with the JSON service
    json_service_register_command("system.status.get", system_status_callback, 0);

    return ESP_OK;
}

esp_err_t system_status_snapshot(void)
{
    if (!s_system_status.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    system_status_update_timestamp();
    system_status_update_core_heap();
    system_status_update_task_stack();
    system_status_update_ota_partitions();

    float temp_c = 0.0f;
    if (system_status_read_temperature(&temp_c) == ESP_OK) {
        s_system_status.internal_temperature = temp_c;
    }

    return ESP_OK;
}

esp_err_t system_status_update(void)
{
    if (!s_system_status.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    system_status_update_timestamp();
    system_status_update_core_heap();
    system_status_update_task_stack();

    float temp_c = 0.0f;
    if (system_status_read_temperature(&temp_c) == ESP_OK) {
        s_system_status.internal_temperature = temp_c;
    }

    return ESP_OK;
}

SystemStatus_t *system_status_get(void)
{
    return &s_system_status;
}

esp_err_t system_status_set_wireless_status(const wireless_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_system_status.network = *status;
    return ESP_OK;
}

esp_err_t system_status_set_network_status(const wireless_status_t *status)
{
    return system_status_set_wireless_status(status);
}

esp_err_t system_status_set_firmware_status(const firmware_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_system_status.firmware = *status;
    return ESP_OK;
}

esp_err_t system_status_set_filesystem_status(const filesystem_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_system_status.storage = *status;
    return ESP_OK;
}

esp_err_t system_status_set_webserver_status(const webserver_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_system_status.web_server = *status;
    return ESP_OK;
}


static const char *ota_state_to_string(ota_state_t state)
{
    switch (state) {
    case OTA_READY:
        return "OTA_READY";
    case OTA_DOWNLOADING:
        return "OTA_DOWNLOADING";
    case OTA_VERIFYING:
        return "OTA_VERIFYING";
    case OTA_FLASHING:
        return "OTA_FLASHING";
    case OTA_FAILED:
        return "OTA_FAILED";
    case OTA_PENDING_REBOOT:
        return "OTA_PENDING_REBOOT";
    default:
        return "OTA_UNKNOWN";
    }
} // End of ota_state_to_string
//-----------------------------------------------------------------------------



cJSON *system_status_get_json()
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "id", s_system_status.id);
    cJSON_AddStringToObject(root, "device_name", s_system_status.device_name);
    cJSON_AddStringToObject(root, "timestamp", s_system_status.timestamp);
    cJSON_AddBoolToObject(root, "initialized", s_system_status.initialized);
    cJSON_AddNumberToObject(root, "uptime_us", (double)s_system_status.uptime_us);
    cJSON_AddNumberToObject(root, "last_update_us", (double)s_system_status.last_update_us);
    cJSON_AddNumberToObject(root, "core_0_free_heap_bytes", (double)s_system_status.core_0_free_heap_bytes);
    cJSON_AddNumberToObject(root, "core_1_free_heap_bytes", (double)s_system_status.core_1_free_heap_bytes);
    cJSON_AddNumberToObject(root, "psram_free_bytes", (double)s_system_status.psram_free_bytes);
    cJSON_AddNumberToObject(root, "internal_temperature", s_system_status.internal_temperature);
    cJSON_AddNumberToObject(root, "crash_counter", (double)s_system_status.crash_counter);
    cJSON_AddNumberToObject(root, "reset_reason", (double)s_system_status.reset_reason);
    cJSON_AddNumberToObject(root, "core_0_task_stack_high_water_bytes", (double)s_system_status.core_0_task_stack_high_water_bytes);

    cJSON *network = cJSON_AddObjectToObject(root, "network");
    if (network) {
        cJSON_AddStringToObject(network, "active_ssid", s_system_status.network.active_ssid);
        cJSON_AddBoolToObject(network, "internet_connected", s_system_status.network.internet_connected);
        cJSON_AddNumberToObject(network, "bytes_received", (double)s_system_status.network.bytes_received);
        cJSON_AddNumberToObject(network, "bytes_transmitted", (double)s_system_status.network.bytes_transmitted);
        cJSON_AddNumberToObject(network, "connected_client_count", (double)s_system_status.network.connected_client_count);

        cJSON *clients = cJSON_AddArrayToObject(network, "clients");
        if (clients) {
            for (size_t i = 0; i < MAX_CLIENTS_COUNT; ++i) {
                cJSON *client = cJSON_CreateObject();
                if (client == NULL) {
                    continue;
                }
                char mac[18];
                snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
                         s_system_status.network.clients[i].mac_address[0],
                         s_system_status.network.clients[i].mac_address[1],
                         s_system_status.network.clients[i].mac_address[2],
                         s_system_status.network.clients[i].mac_address[3],
                         s_system_status.network.clients[i].mac_address[4],
                         s_system_status.network.clients[i].mac_address[5]);
                cJSON_AddStringToObject(client, "mac_address", mac);
                cJSON_AddStringToObject(client, "ip_address", s_system_status.network.clients[i].ip_address);
                cJSON_AddNumberToObject(client, "rssi", (double)s_system_status.network.clients[i].rssi);
                cJSON_AddItemToArray(clients, client);
            }
        }
    }

    cJSON *bluetooth = cJSON_AddObjectToObject(root, "bluetooth");
    if (bluetooth) {
        cJSON_AddBoolToObject(bluetooth, "is_advertising", s_system_status.bluetooth.is_advertising);
        cJSON_AddBoolToObject(bluetooth, "is_paired", s_system_status.bluetooth.is_paired);
        cJSON_AddNumberToObject(bluetooth, "mtu_size", (double)s_system_status.bluetooth.mtu_size);
        cJSON_AddNumberToObject(bluetooth, "bonded_devices_count", (double)s_system_status.bluetooth.bonded_devices_count);
        cJSON_AddNumberToObject(bluetooth, "last_credential_update_time", (double)s_system_status.bluetooth.last_credential_update_time);
    }

    cJSON *storage = cJSON_AddObjectToObject(root, "storage");
    if (storage) {
        cJSON_AddBoolToObject(storage, "is_mounted", s_system_status.storage.is_mounted);
        cJSON_AddNumberToObject(storage, "total_space_bytes", (double)s_system_status.storage.total_space_bytes);
        cJSON_AddNumberToObject(storage, "used_space_bytes", (double)s_system_status.storage.used_space_bytes);
        cJSON_AddNumberToObject(storage, "open_file_handles", (double)s_system_status.storage.open_file_handles);
        cJSON_AddBoolToObject(storage, "write_error_flag", s_system_status.storage.write_error_flag);
    }

    cJSON *web_server = cJSON_AddObjectToObject(root, "web_server");
    if (web_server) {
        cJSON_AddBoolToObject(web_server, "is_running", s_system_status.web_server.is_running);
        cJSON_AddNumberToObject(web_server, "active_connections", (double)s_system_status.web_server.active_connections);
        cJSON_AddNumberToObject(web_server, "total_requests_served", (double)s_system_status.web_server.total_requests_served);
        cJSON_AddNumberToObject(web_server, "internal_500_errors", (double)s_system_status.web_server.internal_500_errors);
        cJSON_AddNumberToObject(web_server, "file_not_found_404s", (double)s_system_status.web_server.file_not_found_404s);
    }

    cJSON *firmware = cJSON_AddObjectToObject(root, "firmware");
    if (firmware) {
        cJSON_AddStringToObject(firmware, "current_version", s_system_status.firmware.current_version);
        cJSON_AddStringToObject(firmware, "compile_date", s_system_status.firmware.compile_date);
        cJSON_AddStringToObject(firmware, "compile_time", s_system_status.firmware.compile_time);
        cJSON_AddNumberToObject(firmware, "active_partition_slot", (double)s_system_status.firmware.active_partition_slot);
        cJSON_AddNumberToObject(firmware, "running_partition_slot", (double)s_system_status.firmware.running_partition_slot);
        cJSON_AddStringToObject(firmware, "current_ota_state", ota_state_to_string(s_system_status.firmware.current_ota_state));
        cJSON_AddNumberToObject(firmware, "ota_bytes_written", (double)s_system_status.firmware.ota_bytes_written);
        cJSON_AddNumberToObject(firmware, "ota_total_expected_bytes", (double)s_system_status.firmware.ota_total_expected_bytes);
    }

    cJSON *parameters = cJSON_AddObjectToObject(root, "parameters");
    if (parameters) {
        cJSON_AddNumberToObject(parameters, "loop_frequency_hz", s_system_status.parameters.loop_frequency_hz);
        cJSON_AddBoolToObject(parameters, "telemetry_enabled", s_system_status.parameters.telemetry_enabled);
        cJSON_AddNumberToObject(parameters, "debug_level", (double)s_system_status.parameters.debug_level);
    }

     
    // char* json_string = cJSON_Print(root);
    // cJSON_Delete(root);
    // return json_string;
    return root;
} // End of system_status_get_json
//-----------------------------------------------------------------------------





/**
 * @brief Callback function to handle the "system_status" command received via JSON service.
 *
 * @param data The cJSON object containing the command data.
 */
 static void system_status_callback(cJSON *data) {
    
    cJSON  *id_item = cJSON_GetObjectItemCaseSensitive(data, "id");
    cJSON  *type_item = cJSON_GetObjectItemCaseSensitive(data, "type");
    cJSON  *cmd = cJSON_GetObjectItemCaseSensitive(data, "cmd");

    const uint32_t id = cJSON_IsNumber(id_item) ? id_item->valueint : 0;
    const char *type_str = type_item->valuestring;
    const char *cmd_str = cmd->valuestring;
    
    ESP_LOGW(TAG, "System status envelope: id=%d, type=%s, cmd=%s", 
             id,
             type_str ? type_str : "null",
             cmd_str ? cmd_str : "null");
             
    cJSON *status_snapshot_current = system_status_get_json();
    cJSON *rpc_envelope = json_service_create_rpc_envelope(RPC_TYPE_RES, id, cmd_str, status_snapshot_current);

    char * encoded_msg = json_service_crc32_envelope_encode(rpc_envelope);
    ESP_LOGW(TAG, "Received WS Nino message: %s", encoded_msg);
    if (encoded_msg == NULL) {
        ESP_LOGW(TAG, "Failed to wrap system status JSON with CRC32");
        cJSON_Delete(status_snapshot_current);
        return;
    }
}

    //     if (status_snapshot_current) {
    //         cJSON_Delete(status_snapshot_current);
    //     } else {
    //         ESP_LOGW(TAG, "Failed to get system status JSON");          
    //     }
    
    //     if (encoded_msg) {
    //         // hot_tub_web_server_broadcast_json(encoded_msg);
    //         ESP_LOGW(TAG, "Freeing encoded JSON memory");
    //         free(encoded_msg);
    //     }
    

    

    
    // // Check if the data is a string and matches "request"
    // if (!cJSON_IsString(data)) {
        //     return;
    // }
    // if (data->valuestring == NULL) {
    //     return;
    // }
    


    

    // // If the command is "request", 
    // // send the current system status JSON back to the requester
    // if (strcmp(data->valuestring, "request") == 0) {

    //     cJSON *status_snapshot_current = system_status_get_json();
    //     char wrapped[2500];
    //     size_t wrapped_len = 0;
    //     char * encoded_msg = json_service_crc32_envelope_encode(status_snapshot_current);
    //     if (encoded_msg == NULL) {
    //         ESP_LOGW(TAG, "Failed to wrap system status JSON with CRC32");
    //         cJSON_Delete(status_snapshot_current);
    //         return;
    //     }
    //     ESP_LOGW(TAG, "Received WS Nino message: %s", encoded_msg);
    
    //     if (status_snapshot_current) {
    //         cJSON_Delete(status_snapshot_current);
    //     } else {
    //         ESP_LOGW(TAG, "Failed to get system status JSON");          
    //     }
    
    //     if (encoded_msg) {
    //         // hot_tub_web_server_broadcast_json(encoded_msg);
    //         ESP_LOGW(TAG, "Freeing encoded JSON memory");
    //         free(encoded_msg);
    //     }
    // }

