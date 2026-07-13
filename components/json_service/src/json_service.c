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
#include "json_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "cJSON.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_crc.h"

#include <stdbool.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
// #include <ctype.h>
#include "web_server.h"
//------------------------------------------------------------------------------


// DEFINES
#ifndef BROADCAST_ID 
#define BROADCAST_ID 0
#endif

#ifndef MASTER_ID 
#define MASTER_ID 1
#endif




//------------------------------------------------------------------------------

static const char *TAG = "json_service";

static json_command_entry_t *cmd_registry = NULL;
static size_t registered_cmd_count = 0;


/**
 * @brief  Encode a cJSON object into a string with a 
 *         CRC32 envelope for integrity verification.
 *
 * @param json The cJSON object to encode.
 * @return A dynamically allocated string containing the CRC32 
 *         and JSON, or NULL on failure. 
 *         Caller must free the returned string.
 *
 * @note The format of the returned string is: "CRC32:JSON_STRING"
 *       where CRC32 is the computed CRC32 of the JSON_STRING.
 *       2315783988:{"cmd":"system_status","data":"request"}   
 */
char *json_service_crc32_envelope_encode(const cJSON *json)
{
    if (!json)
        return NULL;

    char *json_str = cJSON_PrintUnformatted(json);
    if (!json_str)
        return NULL;

    uint32_t crc = esp_crc32_le(0,
                                (const uint8_t *)json_str,
                                strlen(json_str));

    int len = snprintf(NULL, 0, "%" PRIu32 ":%s", crc, json_str);

    char *packet = malloc(len + 1);
    if (!packet)
    {
        cJSON_free(json_str);
        return NULL;
    }

    snprintf(packet, len + 1, "%" PRIu32 ":%s", crc, json_str);

    cJSON_free(json_str);

    return packet;
} // end of json_service_crc32_envelope_encode()
//------------------------------------------------------------------------------



/**
 * @brief  Decode a string with a CRC32 envelope and verify its integrity.
 *
 * @param packet The input string containing the CRC32 and JSON.
 * @return A cJSON object if the CRC32 is valid and the JSON is 
 *         parsed successfully, NULL otherwise.
 *         Caller must free the returned cJSON object.
 */
cJSON *json_service_crc32_envelope_decode(const char *packet)
{
    if (!packet)
        return NULL;

    const char *colon = strchr(packet, ':');
    if (!colon)
        return NULL;

    // Parse transmitted CRC
    uint32_t received_crc = (uint32_t)strtoul(packet, NULL, 10);

    // JSON begins after ':'
    const char *json = colon + 1;

    // ESP_LOGW(TAG, "Received CRC32: %" PRIu32 ", JSON: %s", received_crc, json);
    uint32_t calculated_crc = esp_crc32_le(
        0,
        (const uint8_t *)json,
        strlen(json));

    if (received_crc != calculated_crc)
    {
        return NULL;
    }

    return cJSON_Parse(json);
} // end of json_service_crc32_envelope_decode()
//------------------------------------------------------------------------------



/**
 * @brief Create an RPC envelope JSON object.
 *
 * @param json_obj The cJSON object to populate.
 * @param type The RPC type.
 * @param id The RPC ID.
 * @param cmd The command string.
 * @param params The parameters object.
 * @return The created cJSON object, or NULL on failure.
 */
cJSON * json_service_create_rpc_envelope(rpc_type_t type, 
                                        uint32_t id, 
                                        const char *cmd, 
                                        cJSON *params)
{
    cJSON *json_obj = cJSON_CreateObject();
    if (!json_obj)
        return NULL;

    cJSON_AddNumberToObject(json_obj, "id", id);

    const char *type_str = NULL;
    switch (type)
    {
    case RPC_TYPE_REQ:
        type_str = "req";
        break;
    case RPC_TYPE_RES:
        type_str = "res";
        break;
    case RPC_TYPE_EVT:
        type_str = "evt";
        break;
    default:
        type_str = "unknown";
        break;
    }
    cJSON_AddStringToObject(json_obj, "type", type_str);

    if (cmd)
        cJSON_AddStringToObject(json_obj, "cmd", cmd);

    if (params)
        cJSON_AddItemToObject(json_obj, "params", params);

    char * msg = cJSON_PrintUnformatted(json_obj);
    // ESP_LOGW(TAG, "Created RPC envelope: %s", msg);
    free(msg);

    return json_obj;
} // end of json_service_create_rpc_envelope()
//------------------------------------------------------------------------------



/**
 * @brief Parses an RPC envelope JSON string.
 *
 * @param json_str The JSON string to parse.
 * @param out_type Pointer to store the RPC type (optional).
 * @param out_id Pointer to store the RPC ID (optional).
 * @param out_cmd Pointer to store the command string (optional, caller must free).
 * @param out_params Pointer to store the params object (optional, caller must free).
 * @return The root cJSON object if parsing is successful, NULL otherwise.
 *         Caller must free the returned cJSON object.
 */
cJSON *json_service_parse_rpc_envelope(const char *json_str, rpc_type_t *out_type, uint32_t *out_id, char **out_cmd, cJSON **out_params)
{
    if (!json_str)
        return NULL;

    cJSON *root = cJSON_Parse(json_str);
    if (!root)
        return NULL;

    if (out_type)
    {
        cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
        if (cJSON_IsString(type_item) && type_item->valuestring != NULL)
        {
            if (strcmp(type_item->valuestring, "req") == 0)
                *out_type = RPC_TYPE_REQ;
            else if (strcmp(type_item->valuestring, "res") == 0)
                *out_type = RPC_TYPE_RES;
            else if (strcmp(type_item->valuestring, "evt") == 0)
                *out_type = RPC_TYPE_EVT;
            else
                *out_type = RPC_TYPE_UNKNOWN;
        }
    }

    if (out_id)
    {
        cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
        if (cJSON_IsNumber(id_item))
            *out_id = id_item->valueint;
    }

    if (out_cmd)
    {
        cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(root, "cmd");
        if (cJSON_IsString(cmd_item) && cmd_item->valuestring != NULL)
            *out_cmd = strdup(cmd_item->valuestring);
    }

    if (out_params)
    {
        cJSON *params_item = cJSON_GetObjectItemCaseSensitive(root, "params");
        if (params_item)
            *out_params = cJSON_Duplicate(params_item, 1); // Deep copy
    }

    return root; // Caller must free this root object
} // end of json_service_parse_rpc_envelope()
//------------------------------------------------------------------------------



/**
 * @brief Register a JSON command with its associated callback and target core.
 *
 * @param cmd_string The command string to register (e.g., "SET_TEMP").
 * @param callback The callback function to execute when the command is received.
 * @param target_core The target core (0 or 1) where the callback should be executed.
 *
 * @return true if the command was successfully registered, false otherwise.
 */
bool json_service_register_command(const char *cmd_string, json_cmd_callback_t callback, uint8_t target_core) {
    // Expand the registry array by 1 slot
    json_command_entry_t *temp = realloc(cmd_registry, (registered_cmd_count + 1) * sizeof(json_command_entry_t));
    if (temp == NULL) {
        return false; // Out of memory!
    }
    cmd_registry = temp;

    // Store the command map
    cmd_registry[registered_cmd_count].cmd_string = cmd_string;
    cmd_registry[registered_cmd_count].callback = callback;
    cmd_registry[registered_cmd_count].target_core = target_core;
    
    registered_cmd_count++;
    return true;

} // end of json_service_register_command()
//-----------------------------------------------------------------------------



/**
 * @brief Dispatch incoming JSON messages to the appropriate command handler on Core 0.
 *
 * @param incoming_json The incoming JSON string.
 */
void json_service_dispatcher_core0(cJSON *root)       
{
    if (!root) return;

    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    const uint32_t id = cJSON_IsNumber(id_item) ? id_item->valueint : 0;
    if (!cJSON_IsString(type_item) || type_item->valuestring == NULL) {
        return;
    }
    const char *type_str = type_item->valuestring;
   
    if (!cJSON_IsString(cmd) || cmd->valuestring == NULL) {
        return;
    }
    const char *cmd_str = cmd->valuestring;
    ESP_LOGW(TAG, "RPC received envelope: id=%d, type=%s, cmd=%s, params=%s", id, type_str, cmd_str, cJSON_PrintUnformatted(params));

    if (type_str != NULL && strcmp(type_str, "req") == 0) 
    {
        // Search the dynamic array
        for (size_t i = 0; i < registered_cmd_count; i++) 
        {
            // Check if the command is for core 0 or core 1 and dispatch accordingly
            if (strcmp(cmd_str, cmd_registry[i].cmd_string) == 0) 
            {
                if (cmd_registry[i].target_core == 0) 
                {
                    // --- Core 0 Local Execution ---
                    cmd_registry[i].callback(root);
                } 
                // else if (cmd_registry[i].target_core == 1) {
                    // --- Core 1 Remote Execution ---
                    
            }
        }
        ESP_LOGD(TAG, "RPC request response type for id=%d, type=%s and cmd=%s", id, type_str ? type_str : "null", cmd_str);
    } 
    else if (type_str != NULL && strcmp(type_str, "res") == 0) 
    {

        ESP_LOGD(TAG, "Processing RPC response with id=%d and cmd=%s", id, cmd_str);
    } 
    else if (type_str != NULL && strcmp(type_str, "evt") == 0) 
    {
        ESP_LOGD(TAG, "Processing RPC event with id=%d and cmd=%s", id, cmd_str);
    } 
    else 
    {
        ESP_LOGD(TAG, "Unknown RPC type for id=%d, type=%s and cmd=%s", id, type_str ? type_str : "null", cmd_str);
    }

} // end of json_service_dispatcher_core0() 
//-----------------------------------------------------------------------------
