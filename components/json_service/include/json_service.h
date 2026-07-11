#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "cJSON.h"




// Generic callback signature every component will implement
typedef void (*json_cmd_callback_t)(cJSON *data);


// The registry entry structure
typedef struct {
    const char *cmd_string;          // e.g., "SET_TEMP"
    json_cmd_callback_t callback;    // The component's execution function
    uint8_t target_core;             // 0 or 1 (tells which core it needs to execute)
} json_command_entry_t;


// Generalized container to pass unparsed string payloads to Core 1
typedef struct {
    json_cmd_callback_t callback; // Pass the exact function address directly!
    char *json_data_string;       // The inner data string copy
} core1_generic_msg_t;


/**
 * @brief RPC Message Types mapped from the "type" key string
 */
#ifndef RPC_TYPE_T
#define RPC_TYPE_T
 typedef enum {
    RPC_TYPE_UNKNOWN = 0,
    RPC_TYPE_REQ,      // Maps to "req"
    RPC_TYPE_RES,      // Maps to "res"
    RPC_TYPE_EVT,      // Maps to "evt"
    RPC_TYPE_PUB       // Maps to "pub"
} rpc_type_t;
#endif



/**
 * @brief Standardized Error Codes for the "error.code" block
 */
#ifndef RPC_ERROR_CODE_T
#define RPC_ERROR_CODE_T
 typedef enum {
    RPC_ERR_OK                 = 0,
    RPC_ERR_BAD_REQUEST        = 400,  // Malformed JSON envelope
    RPC_ERR_NOT_FOUND          = 404,  // Unknown command namespace
    RPC_ERR_INVALID_PARAMS     = 422,  // Params missing or out of range
    RPC_ERR_INTERNAL_HARDWARE  = 500,  // Sensor/Relay/SPI link failed
    RPC_ERR_SAFETY_INTERLOCK   = 503   // Hardware safety trip active
} rpc_error_code_t;
#endif



extern QueueHandle_t xCore1GenericQueue;


// void ws_json_service_dispatcher_core0(const char *incoming_json);
// void json_service_dispatcher_core0(const char *incoming_json);
void json_service_dispatcher_core0(cJSON *root); 


/**
 * @brief Create an RPC envelope JSON object.
 *
 * @param json_obj The cJSON object to populate.
 * @param type The RPC type.
 * @param id The RPC ID.
 * @param cmd The command string.
 * @param params The parameters object.
 */
cJSON * json_service_create_rpc_envelope(rpc_type_t type, 
                                        uint32_t id, 
                                        const char *cmd, 
                                        cJSON *params);


//  void json_service_create_rpc_envelope(cJSON *json_obj, rpc_type_t type, uint32_t id, const char *cmd, cJSON *params);





cJSON *json_service_parse_rpc_envelope(const char *json_str, rpc_type_t *out_type, uint32_t *out_id, char **out_cmd, cJSON **out_params);


// bool json_service_register_command(const char *cmd_string, 
//                                     json_cmd_callback_t callback, 
//                                     uint8_t target_core);




char *json_service_crc32_envelope_encode(const cJSON *json);
cJSON *json_service_crc32_envelope_decode(const char *packet);

