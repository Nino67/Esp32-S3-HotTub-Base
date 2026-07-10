#include "cJSON.h"

/**
 * @brief Parsed RPC Metadata Envelope
 * Pointers inside reference the underlying cJSON tree directly (Zero Copy).
 */
typedef struct {
    uint32_t id;                // Unique message transaction ID
    rpc_type_t type;            // Enum mapped from "type"
    const char *cmd;            // Pointer to "cmd" string block
    const char *status;         // Pointer to "status" string ("ok" / "error")
    
    // Extracted payload tree handles 
    cJSON *params;              // Points to root of request parameters (if type == REQ)
    cJSON *result;              // Points to root of success payload (if type == RES && status == "ok")
    cJSON *error_obj;           // Points to error root sub-object (if type == RES && status == "error")
    cJSON *data;                // Points to event payload root (if type == EVT)
} rpc_envelope_t;

/**
 * @brief Signature standard for self-registering framework component callbacks
 */
typedef esp_err_t (*rpc_callback_t)(const cJSON *params, cJSON *result_out, char *error_msg_out, size_t max_err_len);

/**
 * @brief Command Registry Entry node
 */
typedef struct {
    const char *cmd_name;       // Namespace matching string (e.g., "hottub.set_temp")
    rpc_callback_t callback;    // Function pointer to dispatch to execution layer
} rpc_command_t;






esp_err_t json_rpc_parse_envelope(cJSON *root, rpc_envelope_t *env) {
    if (!root || !env) return ESP_ERR_INVALID_ARG;
    memset(env, 0, sizeof(rpc_envelope_t));

    // 1. Extract ID
    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    env->id = id_item ? (uint32_t)id_item->valueint : 0;

    // 2. Extract Type
    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (type_item && type_item->valuestring) {
        if (strcmp(type_item->valuestring, "req") == 0) env->type = RPC_TYPE_REQ;
        else if (strcmp(type_item->valuestring, "res") == 0) env->type = RPC_TYPE_RES;
        else if (strcmp(type_item->valuestring, "evt") == 0) env->type = RPC_TYPE_EVT;
    }

    // 3. Extract Command Namespace
    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    env->cmd = cmd_item ? cmd_item->valuestring : NULL;

    // 4. Extract Status
    cJSON *status_item = cJSON_GetObjectItem(root, "status");
    env->status = status_item ? status_item->valuestring : NULL;

    // 5. Route Payloads based on identified Type
    if (env->type == RPC_TYPE_REQ) {
        env->params = cJSON_GetObjectItem(root, "params");
    } else if (env->type == RPC_TYPE_RES) {
        if (env->status && strcmp(env->status, "error") == 0) {
            env->error_obj = cJSON_GetObjectItem(root, "error");
        } else {
            env->result = cJSON_GetObjectItem(root, "result");
        }
    } else if (env->type == RPC_TYPE_EVT) {
        env->data = cJSON_GetObjectItem(root, "data");
    }

    return ESP_OK;
}