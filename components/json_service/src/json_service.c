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
void json_service_dispatcher_core0(cJSON *root)       // const char *incoming_json)
{
    // cJSON *root = cJSON_Parse(incoming_json);
    if (!root) return;
    // ESP_LOGW(TAG, "Received JSON message for execution stage: %s", incoming_json);

    cJSON *id_item = cJSON_GetObjectItemCaseSensitive(root, "id");
    cJSON *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
   
    const uint32_t id = cJSON_IsNumber(id_item) ? id_item->valueint : 0;
    const char *type_str = type_item->valuestring;
    const char *cmd_str = cmd->valuestring;
    // ESP_LOGW(TAG, "RPC received envelope: id=%d, type=%s, cmd=%s", id, type_str, cmd_str);

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
        // ESP_LOGW(TAG, "RPC request response type for id=%d, type=%s and cmd=%s", id, type_str ? type_str : "null", cmd_str);
    } 
    else if (type_str != NULL && strcmp(type_str, "res") == 0) 
    {

        ESP_LOGW(TAG, "Processing RPC response with id=%d and cmd=%s", id, cmd_str);
    } 
    else if (type_str != NULL && strcmp(type_str, "evt") == 0) 
    {
        ESP_LOGW(TAG, "Processing RPC event with id=%d and cmd=%s", id, cmd_str);
    } 
    else 
    {
        ESP_LOGW(TAG, "Unknown RPC type for id=%d, type=%s and cmd=%s", id, type_str ? type_str : "null", cmd_str);
    }

} // end of json_service_dispatcher_core0() 
//-----------------------------------------------------------------------------

    // if (cmd_str != NULL) 
    // {
    //     // Search the dynamic array
    //     for (size_t i = 0; i < registered_cmd_count; i++) 
    //     {
    //         if (strcmp(cmd_str, cmd_registry[i].cmd_string) == 0) 
    //         {
    //             if (cmd_registry[i].target_core == 0) 
    //             {
    //                 // --- Core 0 Local Execution ---
    //                 // ESP_LOGW(TAG, "Executing command '%s' on Core 0", cmd_registry[i].cmd_string);
    //                 // ESP_LOGW(TAG, "Address to callback: %p", cmd_registry[i].callback);
    //                 cmd_registry[i].callback(root);
    //             } 
                // else if (cmd_registry[i].target_core == 1) {
                //     // --- Core 1 Remote Execution ---
                //     // Render just the "data" sub-object back to a string to pass across cores safely
                //     char *data_str = cJSON_PrintUnformatted(root);
                    
                //     core1_generic_msg_t msg = {
                //         .callback = cmd_registry[i].callback,
                //         .json_data_string = data_str
                //     };
                    
                //     if (xQueueSend(xCore1GenericQueue, &msg, 0) != pdTRUE) {
                //         free(data_str); // Queue full, clean up string
                    // }
                // }


    //    cJSON *system_status = system_status_get_json();
    //     cJSON *json_msg = NULL;
    //     if (system_status)
    //     {
    //         // ESP_LOGW(TAG, "System status webserver ws rec JSON: %s", cJSON_Print(system_status));
    //         json_msg = json_service_create_rpc_envelope(RPC_TYPE_REQ, 1, "system.status", system_status);
    //         if (json_msg)
    //         {
    //             char *msg = json_service_crc32_envelope_encode(json_msg);
    //             // ESP_LOGW(TAG, "Wrapped JSON message: %s", msg);
    //             free(msg);
    //             cJSON_Delete(json_msg);
    //         }
    //     }






// /**
//  * @brief Dispatch a JSON command to the appropriate callback based on the registered command map.
//  *
//  * @param incoming_json The incoming JSON string containing the command and data.
//  */
// void ws_json_service_dispatcher_core0(const char *incoming_json) {
//     cJSON *root = cJSON_Parse(incoming_json);
//     if (!root) return;

//     cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
//     cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
//     if (cJSON_IsString(cmd) && cmd->valuestring != NULL) {
//         // Search the dynamic array
//         for (size_t i = 0; i < registered_cmd_count; i++) {
//             if (strcmp(cmd->valuestring, cmd_registry[i].cmd_string) == 0) {
//                 if (cmd_registry[i].target_core == 0) {
//                     // --- Core 0 Local Execution ---
//                     // ESP_LOGW(TAG, "Executing command '%s' on Core 0", cmd_registry[i].cmd_string);
//                     // ESP_LOGW(TAG, "Address to callback: %p", cmd_registry[i].callback);
//                     cmd_registry[i].callback(data);
//                 } 
//                 else if (cmd_registry[i].target_core == 1) {
//                     // // --- Core 1 Remote Execution ---
//                     // // Render just the "data" sub-object back to a string to pass across cores safely
//                     // char *data_str = cJSON_PrintUnformatted(data);
                    
//                     // core1_generic_msg_t msg = {
//                     //     .callback = cmd_registry[i].callback,
//                     //     .json_data_string = data_str
//                     // };
                    
//                     // if (xQueueSend(xCore1GenericQueue, &msg, 0) != pdTRUE) {
//                     //     free(data_str); // Queue full, clean up string
//                     // }
//                 }
//                 break;
//             }
//         }
//     }
//     cJSON_Delete(root);

// } // end of ws_json_service_dispatcher_core0()
//-----------------------------------------------------------------------------





















/**
 * Decodes and verifies:
 *    CRC32:JSON_STRING
 *
 * Returns NULL if:
 *   - invalid format
 *   - CRC mismatch
 *   - JSON parse error
 *
 * Caller owns returned cJSON object.
 */
// cJSON *rpc_decode_crc32(const char *packet);

// /**
// * @brief  Wrap a object with a CRC32 envelope for integrity verification,
// *         example: 1655482432:{"id":1,"type":"req","cmd":"hottub.set_temp","params":{"temp":100}} 
// * 
// * @param json_obj The cJSON object to serialize and send
// * @param output Buffer to write the resulting JSON string with CRC32 envelope
// * @param output_size Size of the output buffer
// * @param output_len Pointer to size_t to receive the length of the resulting JSON string    
// */
// esp_err_t json_service_build_crc32_envelope(const cJSON *json_obj,
//                                       char *output,
//                                       size_t output_size,
//                                       size_t *output_len)
// {
//     if (json_obj == NULL || output == NULL || output_len == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     char *base_complete = cJSON_PrintUnformatted((cJSON *)json_obj);
//     if (base_complete == NULL) {
//         return ESP_ERR_NO_MEM;
//     }

//     size_t complete_len = strlen(base_complete);
//     if (complete_len < 2 || base_complete[complete_len - 1] != '}') {
//         free(base_complete);
//         return ESP_ERR_INVALID_ARG;
//     }

//     size_t base_prefix_len = complete_len - 1;
//     uint32_t crc = esp_crc32_le(0, (const uint8_t *)base_complete, base_prefix_len);

//     int written = snprintf(output,
//                            output_size,
//                            "%.*s,\"" JSON_CRC32 "\":%" PRIu32 "}\n",
//                            (int)base_prefix_len,
//                            base_complete,
//                            crc);

//     free(base_complete);

//     if (written <= 0 || (size_t)written >= output_size) {
//         return ESP_ERR_NO_MEM;
//     }

//     *output_len = (size_t)written;
//     return ESP_OK;
// } // end of json_service_build_crc32_envelope()



// /**
// * @brief  Validate a received JSON string with CRC32 envelope,
// *         and reconstruct the original JSON if valid. 
// *         example: 1655482432:{"id":1,"type":"req","cmd":"hottub.set_temp","params":{"temp":100}}    
// *
// * @param input The input JSON string with CRC32 envelope
// * @param output Buffer to write the reconstructed original JSON string (without CRC32 envelope)
// * @param output_size Size of the output buffer
// * @param expected_crc Pointer to uint32_t to receive the expected CRC32 value from the input
// * @param computed_crc Pointer to uint32_t to receive the computed CRC32 value from the input
// * @return ESP_OK if the input is valid and the original JSON was successfully reconstructed, or an error
// */
// esp_err_t json_service_validate_crc32(const char *input,
//                                       char *output,
//                                       size_t output_size,
//                                       uint32_t *expected_crc,
//                                       uint32_t *computed_crc) 
// {
//     if (input == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     if (output == NULL && output_size != 0) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     const char *ptr = input;
//     while (*ptr != '\0' && isspace((unsigned char)*ptr)) {
//         ptr++;
//     }

//     size_t len = strlen(ptr);
//     if (len == 0 || ptr[len - 1] != '}') {
//         return ESP_ERR_INVALID_ARG;
//     }

//     const char *crc_key = ",\"" JSON_CRC32 "\":";
//     const char *crc_pos = strstr(ptr, crc_key);
//     if (crc_pos == NULL) {
//         return ESP_ERR_INVALID_CRC;
//     }

//     const char *number_start = crc_pos + strlen(crc_key);
//     const char *number_end = number_start;
//     while (*number_end != '\0' && isdigit((unsigned char)*number_end)) {
//         number_end++;
//     }

//     if (number_end == number_start || *number_end != '}') {
//         return ESP_ERR_INVALID_CRC;
//     }

//     uint32_t received_crc = 0;
//     for (const char *q = number_start; q < number_end; ++q) {
//         received_crc = received_crc * 10 + (uint32_t)(*q - '0');
//     }

//     if (expected_crc) {
//         *expected_crc = received_crc;
//     }

//     size_t prefix_len = (size_t)(crc_pos - ptr);
//     if (prefix_len == 0) {
//         return ESP_ERR_INVALID_CRC;
//     }

//     uint32_t calculated_crc = esp_crc32_le(0, (const uint8_t *)ptr, prefix_len);
//     if (computed_crc) {
//         *computed_crc = calculated_crc;
//     }

//     if (calculated_crc != received_crc) {
//         return ESP_ERR_INVALID_CRC;
//     }  

// } // end of json_service_validate_crc32()
// //------------------------------------------------------------------------------




// /**
//  * @brief  Build a JSON string with a CRC32 envelope for integrity verification
//  *
//  * @param json_obj The cJSON object to serialize and send
//  * @param output Buffer to write the resulting JSON string with CRC32 envelope
//  * @param output_size Size of the output buffer
//  * @param output_len Pointer to size_t to receive the length of the resulting JSON string
//  *
//  * @return true if the JSON string was successfully built and fits in the output buffer, false otherwise
//  */
// bool crc32_json_wrapper(const cJSON *json_obj,
//                         char *output,
//                         size_t output_size,
//                         size_t *output_len)
// {
//   if (json_obj == NULL || output == NULL || output_len == NULL)
//   {
//     return false;
//   }

// //   char *base_complete = cJSON_PrintUnformatted((cJSON *)json_obj);
//   char *base_complete = cJSON_Print((cJSON *)json_obj);
//   if (base_complete == NULL)
//   {
//     return false;
//   }
  
//   size_t complete_len = strlen(base_complete);
// //   ESP_LOGW(TAG, "Base JSON string length : %zu", complete_len);

//   if (complete_len < 2 || base_complete[complete_len - 1] != '}')
//   {
//     free(base_complete);
//     return false;
//   }

//   size_t base_prefix_len = complete_len - 1;
//   uint32_t crc = esp_crc32_le(0, (const uint8_t *)base_complete, base_prefix_len);

//   // ESP_LOGI("json_service", "CRC32 value: %" PRIu32, crc);

//   int written = snprintf(output,
//                          output_size,
//                          "%.*s,\"" JSON_CRC32 "\":%" PRIu32 "}\n",
//                          (int)base_prefix_len,
//                          base_complete,
//                          crc);

//   free(base_complete);

//   if (written <= 0 || (size_t)written >= output_size)
//   {
//     return false;
//   }

//   *output_len = (size_t)written;
//   ESP_LOGW(TAG, "Built CRC32 enveloped JSON message: %s", output);
//   ESP_LOGW(TAG, "Built CRC32 enveloped JSON message length: %d", written - complete_len);
//   return true;
// } // end of crc32_json_wrapper()
// //------------------------------------------------------------------------------


// /**
// * @brief  Validate a received JSON string with CRC32 envelope and reconstruct the original JSON if valid
// *
// * @param input The input JSON string with CRC32 envelope
// * @param output Buffer to write the reconstructed original JSON string (without CRC32 envelope)
// *
// * @return true if the input is valid and the original JSON was successfully reconstructed, false otherwise
// */
//  esp_err_t json_service_validate_crc32(const char *input,
//                                       char *output,
//                                       size_t output_size,
//                                       uint32_t *expected_crc,
//                                       uint32_t *computed_crc)
// {
//     if (input == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     if (output == NULL && output_size != 0) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     const char *ptr = input;
//     while (*ptr != '\0' && isspace((unsigned char)*ptr)) {
//         ptr++;
//     }

//     size_t len = strlen(ptr);
//     if (len == 0 || ptr[len - 1] != '}') {
//         return ESP_ERR_INVALID_ARG;
//     }

//     const char *crc_key = ",\"" JSON_CRC32 "\":";
//     const char *crc_pos = strstr(ptr, crc_key);
//     if (crc_pos == NULL) {
//         return ESP_ERR_INVALID_CRC;
//     }

//     const char *number_start = crc_pos + strlen(crc_key);
//     const char *number_end = number_start;
//     while (*number_end != '\0' && isdigit((unsigned char)*number_end)) {
//         number_end++;
//     }

//     if (number_end == number_start || *number_end != '}') {
//         return ESP_ERR_INVALID_CRC;
//     }

//     uint32_t received_crc = 0;
//     for (const char *q = number_start; q < number_end; ++q) {
//         received_crc = received_crc * 10 + (uint32_t)(*q - '0');
//     }

//     if (expected_crc) {
//         *expected_crc = received_crc;
//     }

//     size_t prefix_len = (size_t)(crc_pos - ptr);
//     if (prefix_len == 0) {
//         return ESP_ERR_INVALID_CRC;
//     }

//     uint32_t calculated_crc = esp_crc32_le(0, (const uint8_t *)ptr, prefix_len);
//     if (computed_crc) {
//         *computed_crc = calculated_crc;
//     }

//     if (calculated_crc != received_crc) {
//         return ESP_ERR_INVALID_CRC;
//     }

//     if (output != NULL) {
//         if (prefix_len + 2 > output_size) {
//             return ESP_ERR_NO_MEM;
//         }
//         memcpy(output, ptr, prefix_len);
//         output[prefix_len] = '}';
//         output[prefix_len + 1] = '\0';
//     }

//     return ESP_OK;
// } // end of json_service_validate_crc32()
// //------------------------------------------------------------------------------






// esp_err_t json_service_parse_json(const char *json_str, cJSON **out_json)
// {
//     if (json_str == NULL || out_json == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     cJSON *json = cJSON_Parse(json_str);
//     if (json == NULL) {
//         return ESP_ERR_INVALID_ARG;
//     }

//     cJSON *command_item = cJSON_GetObjectItemCaseSensitive(json, "command");





//     *out_json = json;

//     ESP_LOGW(TAG, "Received WS Nino data message: %s", command_item ? command_item->valuestring : "NULL");

//     return ESP_OK;
// } // end of json_service_parse_json()




// /**
//  * @brief Dispatch a JSON command to the appropriate callback based on the registered command map.
//  *
//  * @param incoming_json The incoming JSON string containing the command and data.
//  */
// void ws_json_service_dispatcher_core0(const char *incoming_json) {
//     cJSON *root = cJSON_Parse(incoming_json);
//     if (!root) return;

//     cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
//     cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
//     if (cJSON_IsString(cmd) && cmd->valuestring != NULL) {
//         // Search the dynamic array
//         for (size_t i = 0; i < registered_cmd_count; i++) {
//             if (strcmp(cmd->valuestring, cmd_registry[i].cmd_string) == 0) {
//                 if (cmd_registry[i].target_core == 0) {
//                     // --- Core 0 Local Execution ---
//                     // ESP_LOGW(TAG, "Executing command '%s' on Core 0", cmd_registry[i].cmd_string);
//                     // ESP_LOGW(TAG, "Address to callback: %p", cmd_registry[i].callback);
//                     cmd_registry[i].callback(data);
//                 } 
//                 else if (cmd_registry[i].target_core == 1) {
//                     // // --- Core 1 Remote Execution ---
//                     // // Render just the "data" sub-object back to a string to pass across cores safely
//                     // char *data_str = cJSON_PrintUnformatted(data);
                    
//                     // core1_generic_msg_t msg = {
//                     //     .callback = cmd_registry[i].callback,
//                     //     .json_data_string = data_str
//                     // };
                    
//                     // if (xQueueSend(xCore1GenericQueue, &msg, 0) != pdTRUE) {
//                     //     free(data_str); // Queue full, clean up string
//                     // }
//                 }
//                 break;
//             }
//         }
//     }
//     cJSON_Delete(root);

// } // end of ws_json_service_dispatcher_core0()
//-----------------------------------------------------------------------------












