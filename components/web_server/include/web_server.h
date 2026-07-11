#pragma once

#include "esp_err.h"
#include "cJSON.h"
#include <sys/_intsup.h>



/**
 * @brief RPC Message Types mapped from the "type" key string
 */
#ifndef RPC_TYPE_T
#define RPC_TYPE_T
 typedef enum {
    RPC_TYPE_UNKNOWN = 0,
    RPC_TYPE_REQ,      // Maps to "req"
    RPC_TYPE_RES,      // Maps to "res"
    RPC_TYPE_EVT       // Maps to "evt"
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




esp_err_t web_server_start(void);
esp_err_t web_server_broadcast_json(const char *json);
cJSON *system_status_get_json(void);