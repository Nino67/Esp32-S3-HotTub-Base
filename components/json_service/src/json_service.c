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
#include <ctype.h>
//------------------------------------------------------------------------------


// DEFINES
#ifndef JSON_CRC32
#define JSON_CRC32 "crc32"
#endif
//------------------------------------------------------------------------------

static const char *TAG = "json_service";

static json_command_entry_t *cmd_registry = NULL;
static size_t registered_cmd_count = 0;


/**
 * @brief  Build a JSON string with a CRC32 envelope for integrity verification
 *
 * @param json_obj The cJSON object to serialize and send
 * @param output Buffer to write the resulting JSON string with CRC32 envelope
 * @param output_size Size of the output buffer
 * @param output_len Pointer to size_t to receive the length of the resulting JSON string
 *
 * @return true if the JSON string was successfully built and fits in the output buffer, false otherwise
 */
bool crc32_json_wrapper(const cJSON *json_obj,
                        char *output,
                        size_t output_size,
                        size_t *output_len)
{
  // ESP_LOGI("json_service", "Building CRC32 enveloped JSON message");

  if (json_obj == NULL || output == NULL || output_len == NULL)
  {
    return false;
  }

  char *base_complete = cJSON_PrintUnformatted((cJSON *)json_obj);
  if (base_complete == NULL)
  {
    return false;
  }

  size_t complete_len = strlen(base_complete);
  if (complete_len < 2 || base_complete[complete_len - 1] != '}')
  {
    free(base_complete);
    return false;
  }

  size_t base_prefix_len = complete_len - 1;
  uint32_t crc = esp_crc32_le(0, (const uint8_t *)base_complete, base_prefix_len);

  // ESP_LOGI("json_service", "CRC32 value: %" PRIu32, crc);

  int written = snprintf(output,
                         output_size,
                         "%.*s,\"" JSON_CRC32 "\":%" PRIu32 "}\n",
                         (int)base_prefix_len,
                         base_complete,
                         crc);

  free(base_complete);

  if (written <= 0 || (size_t)written >= output_size)
  {
    return false;
  }

  // ESP_LOGI("json_service", "Built CRC32 enveloped JSON message: %s", output);
  *output_len = (size_t)written;
  return true;
} // end of crc32_json_wrapper()
//------------------------------------------------------------------------------









/**
* @brief  Validate a received JSON string with CRC32 envelope and reconstruct the original JSON if valid
*
* @param input The input JSON string with CRC32 envelope
* @param output Buffer to write the reconstructed original JSON string (without CRC32 envelope)
*
* @return true if the input is valid and the original JSON was successfully reconstructed, false otherwise
*/
 esp_err_t json_service_validate_crc32(const char *input,
                                      char *output,
                                      size_t output_size,
                                      uint32_t *expected_crc,
                                      uint32_t *computed_crc)
{
    if (input == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (output == NULL && output_size != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const char *ptr = input;
    while (*ptr != '\0' && isspace((unsigned char)*ptr)) {
        ptr++;
    }

    size_t len = strlen(ptr);
    if (len == 0 || ptr[len - 1] != '}') {
        return ESP_ERR_INVALID_ARG;
    }

    const char *crc_key = ",\"" JSON_CRC32 "\":";
    const char *crc_pos = strstr(ptr, crc_key);
    if (crc_pos == NULL) {
        return ESP_ERR_INVALID_CRC;
    }

    const char *number_start = crc_pos + strlen(crc_key);
    const char *number_end = number_start;
    while (*number_end != '\0' && isdigit((unsigned char)*number_end)) {
        number_end++;
    }

    if (number_end == number_start || *number_end != '}') {
        return ESP_ERR_INVALID_CRC;
    }

    uint32_t received_crc = 0;
    for (const char *q = number_start; q < number_end; ++q) {
        received_crc = received_crc * 10 + (uint32_t)(*q - '0');
    }

    if (expected_crc) {
        *expected_crc = received_crc;
    }

    size_t prefix_len = (size_t)(crc_pos - ptr);
    if (prefix_len == 0) {
        return ESP_ERR_INVALID_CRC;
    }

    uint32_t calculated_crc = esp_crc32_le(0, (const uint8_t *)ptr, prefix_len);
    if (computed_crc) {
        *computed_crc = calculated_crc;
    }

    if (calculated_crc != received_crc) {
        return ESP_ERR_INVALID_CRC;
    }

    if (output != NULL) {
        if (prefix_len + 2 > output_size) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(output, ptr, prefix_len);
        output[prefix_len] = '}';
        output[prefix_len + 1] = '\0';
    }

    return ESP_OK;
} // end of json_service_validate_crc32()
//------------------------------------------------------------------------------






esp_err_t json_service_parse_json(const char *json_str, cJSON **out_json)
{
    if (json_str == NULL || out_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *json = cJSON_Parse(json_str);
    if (json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *command_item = cJSON_GetObjectItemCaseSensitive(json, "command");





    *out_json = json;

    ESP_LOGW(TAG, "Received WS Nino data message: %s", command_item ? command_item->valuestring : "NULL");

    return ESP_OK;
} // end of json_service_parse_json()





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
 * @brief Dispatch a JSON command to the appropriate callback based on the registered command map.
 *
 * @param incoming_json The incoming JSON string containing the command and data.
 */
void ws_json_service_dispatcher_core0(const char *incoming_json) {
    cJSON *root = cJSON_Parse(incoming_json);
    if (!root) return;

    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "cmd");
    cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
    ESP_LOGW(TAG, "Received WS Nino command: %s", cmd ? cmd->valuestring : "NULL");
    ESP_LOGW(TAG, "Received WS Nino data: %s", data ? cJSON_PrintUnformatted(data) : "NULL");
    if (cJSON_IsString(cmd) && cmd->valuestring != NULL) {
        // Search the dynamic array
        for (size_t i = 0; i < registered_cmd_count; i++) {
            if (strcmp(cmd->valuestring, cmd_registry[i].cmd_string) == 0) {
                ESP_LOGW(TAG, "Found registered command '%s' for execution on Core %d", cmd_registry[i].cmd_string, cmd_registry[i].target_core);
                if (cmd_registry[i].target_core == 0) {
                    // --- Core 0 Local Execution ---
                    ESP_LOGW(TAG, "Executing command '%s' on Core 0", cmd_registry[i].cmd_string);
                    ESP_LOGW(TAG, "Address to callback: %p", cmd_registry[i].callback);
                    cmd_registry[i].callback(data);
                } 
                // else if (cmd_registry[i].target_core == 1) {
                //     // --- Core 1 Remote Execution ---
                //     // Render just the "data" sub-object back to a string to pass across cores safely
                //     char *data_str = cJSON_PrintUnformatted(data);
                    
                //     core1_generic_msg_t msg = {
                //         .callback = cmd_registry[i].callback,
                //         .json_data_string = data_str
                //     };
                    
                //     if (xQueueSend(xCore1GenericQueue, &msg, 0) != pdTRUE) {
                //         free(data_str); // Queue full, clean up string
                //     }
                // }
                break;
            }
        }
    }
    cJSON_Delete(root);

} // end of ws_json_service_dispatcher_core0()
//-----------------------------------------------------------------------------