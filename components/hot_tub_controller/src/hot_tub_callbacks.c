
#include "cJSON.h"
#include "esp_log.h"
#include "esp_err.h"
#include "hot_tub_globals.h"
#include "hot_tub_struct_io.h"
#include "hot_tub_controller.h"
#include "hot_tub_callbacks.h"
#include "esp_http_server.h"
#include "json_service.h"
#include "web_server.h"

static const char *TAG = "hot_tub_callbacks";

void get_current_time(char *strftime_buf, size_t buf_size);


/**
 * @brief Parse the "params" object from the JSON command.
 *
 * @param root The cJSON object containing the command and its data.
 * @param key_name Pointer to a char pointer that will hold the name of the first key in "params".
 * @param key_value Pointer to a cJSON pointer that will hold the value of the first key in "params".
 *
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t parse_json_param(cJSON *root, char **key_name, cJSON **key_value) {
    cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");

    if (!cJSON_IsObject(params)) {
        ESP_LOGE(TAG, "Invalid params: not an object");
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *item = params->child;
    if (item) {
        *key_name = item->string;
        *key_value = item;
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "No items in params object");
        return ESP_ERR_INVALID_ARG;
    }
} // End of parse_json_param
//-----------------------------------------------------------------------------


/**
 * @brief Helper function to create a response JSON object for the "hot_tub_controller" command.
 *
 * @param root The cJSON object containing the original command.
 * @param response The cJSON object containing the response data.
 *
 * @note This function modifies the "type" field in the root object to indicate a response.
 */
void hottub_callback_response(cJSON *root, cJSON *response) {
    // Add status and response to the root object
    cJSON_AddStringToObject(root, "status", "ok");
    cJSON_AddItemToObject(root, "response", cJSON_Duplicate(response, 1));
    // Change the "type" field to "res" to indicate a response
    // cJSON  *type_item = cJSON_GetObjectItemCaseSensitive(root, "type");
    // cJSON_SetValuestring(type_item, "res");
    if (response) { cJSON_Delete(response); }
} // End of hottub_callback_response
//----------------------------------------------------------------------------- 


    // char *encoded_msg = json_service_crc32_envelope_encode(root);
    // int len = strlen(encoded_msg);
    // httpd_ws_frame_t out_frame = {
    //     .type = HTTPD_WS_TYPE_TEXT,
    //     .payload = (uint8_t *)encoded_msg,
    //     .len = len,
    // };
    // err = httpd_ws_send_frame(req, &out_frame);




void hottub_broadcast_status_callback(void) 
{
    char * pub_json = "{\"id\":0,\"type\":\"pub\",\"cmd\":\"hottub.status\",\"params\":\"\"}";
    cJSON *pub_root = cJSON_Parse(pub_json);
    hottub_status_get_callback(pub_root);
    char *encoded_msg = json_service_crc32_envelope_encode(pub_root);

    // ESP_LOGI(TAG, "Broadcasting hot tub status: %s", encoded_msg);
    // Broadcast the JSON string to all connected WebSocket clients
    esp_err_t err = web_server_broadcast_json(encoded_msg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to broadcast hot tub status: %s", esp_err_to_name(err));
    }


    free(encoded_msg);
    cJSON_Delete(pub_root);

} // End of hottub_broadcast_status_callback
//----------------------------------------------------------------------------- 


/**
 * @brief Callback function to handle the "hot_tub_controller" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.status.get","params":""}
 */
 void hottub_status_get_callback(cJSON *root) {
    HotTubController_t snapshot;
    cJSON *response = cJSON_CreateObject();    
    if (hot_tub_controller_snapshot_get(&snapshot) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to snapshot hot tub controller state");
        cJSON_Delete(response);
        return;
    }

    // Update the lastUpdateTime field with the current time
    get_current_time(snapshot.lastUpdateTime, sizeof(snapshot.lastUpdateTime));

    // ESP_LOGI(TAG, "Hot Tub Status Snapshot: autoMode=%d, heaterOn=%d, tempUnitCelsius=%d, waterTemp=%.2f, setpointTemp=%.2f, lowHysteresis=%.2f, highHysteresis=%.2f, pumpPreRunTime=%.2f, pumpPostRunTime=%.2f, lastUpdateTime=%s",
    //          snapshot.autoMode,
    //          snapshot.heaterOn,
    //          snapshot.tempUnitCelsius,
    //          snapshot.waterTemp,
    //          snapshot.setpointTemp,
    //          snapshot.lowHysteresis,
    //          snapshot.highHysteresis,
    //          snapshot.pumpPreRunTime,
    //          snapshot.pumpPostRunTime,
    //          snapshot.lastUpdateTime);

    esp_err_t err = hot_tub_controller_to_json(response, &snapshot);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to convert snapshot to JSON: %s", esp_err_to_name(err));
        cJSON_Delete(response);
        return;
    }

    hottub_callback_response(root, response);
} // End of hottub_status_get_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hot_tub_controller" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.heater.status.get","params":""}
 */
void hottub_heater_status_get_callback(cJSON *root) {
    bool heater_on = hot_tub_controller_is_heater_on();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "heater.on", heater_on);
    hottub_callback_response(root, response);
} // End of hottub_heater_status_get_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hot_tub_controller" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.heater.status.set","params":{"heater.on":true}}
 */
void hottub_heater_status_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *heater_on_item = param.key_value;
    bool heater_on = cJSON_IsTrue(heater_on_item);
    hot_tub_controller_set_heater_on(heater_on);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "heater.on", heater_on);
    hottub_callback_response(root, response);
} // End of hottub_heater_status_set_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hot_tub_controller" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.automode.get","params":""}
 */
void hottub_auto_mode_get_callback(cJSON *root) {
    bool auto_mode = hot_tub_controller_is_auto_mode();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "auto.mode", auto_mode);
    hottub_callback_response(root, response);
} // End of hottub_auto_mode_get_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hot_tub_controller" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.automode.set","params":{"auto.mode":true}}
 */
void hottub_auto_mode_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *auto_mode_item = param.key_value;
    bool auto_mode = cJSON_IsTrue(auto_mode_item);
    hot_tub_controller_set_auto_mode(auto_mode);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "auto.mode", auto_mode);
    hottub_callback_response(root, response);
} // End of hottub_auto_mode_set_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hot_tub_controller" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.temperature.unit.get","params":""}
 */
void hottub_temperature_unit_get_callback(cJSON *root) {
    bool temp_unit_celsius = hot_tub_controller_is_temp_unit_celsius();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "temp.unit.celsius.get", temp_unit_celsius);
    hottub_callback_response(root, response);
} // End of hottub_temperature_unit_get_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hot_tub_controller" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.temperature.unit.set","params":{"temp.unit.celsius.set":true}}
 */
void hottub_temperature_unit_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *temp_unit_item = param.key_value;
    bool temp_unit_celsius = cJSON_IsTrue(temp_unit_item);
    hot_tub_controller_set_temp_unit_celsius(temp_unit_celsius);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "temp.unit.celsius.set", temp_unit_celsius);
    hottub_callback_response(root, response);
} // End of hottub_temperature_unit_set_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hot_tub_controller" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.water.temperature.get","params":""}
 */
void hottub_water_temperature_get_callback(cJSON *root) {
    float water_temp = hot_tub_controller_get_water_temp();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "water.temperature.get", water_temp);
    hottub_callback_response(root, response);
} // End of hottub_water_temperature_get_callback
//-----------------------------------------------------------------------------


void hottub_water_temperature_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *water_temp_item = param.key_value;
    float water_temp = (float)water_temp_item->valuedouble;
    hot_tub_controller_set_water_temp(water_temp);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "water.temperature.set", water_temp);
    hottub_callback_response(root, response);
} // End of hottub_water_temperature_set_callback
//----------------------------------------------------------------------------- 


/**
 * @brief Callback function to handle the "hottub.filtered.water.temp.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.filtered.water.temp.get","params":""}
 */
void hottub_filtered_water_temp_get_callback(cJSON *root) {
    float filtered_water_temp = hot_tub_controller_get_filtered_water_temp();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "filtered.water.temp.get", filtered_water_temp);
    hottub_callback_response(root, response);
} // End of hottub_filtered_water_temp_get_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.filtered.water.temp.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.filtered.water.temp.set","params":{"filtered.water.temp.set":37.5}}
 */
void hottub_simulation_mode_get_callback(cJSON *root) {
    sim_mode_t sim_mode = hot_tub_controller_get_simulation_mode();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "simulation.mode.get", (int)sim_mode);
    hottub_callback_response(root, response);
} // End of hottub_simulation_mode_get_callback
//----------------------------------------------------------------------------- 


/**
 * @brief Callback function to handle the "hottub.simulation.mode.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.simulation.mode.set","params":{"simulation.mode.set":1}}
 */
void hottub_simulation_mode_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *sim_mode_item = param.key_value;
    sim_mode_t sim_mode = (sim_mode_t)sim_mode_item->valueint;
    hot_tub_controller_set_simulation_mode(sim_mode);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "simulation.mode.set", (int)sim_mode);
    hottub_callback_response(root, response);
} // End of hottub_simulation_mode_set_callback
//----------------------------------------------------------------------------- 


/**
 * @brief Callback function to handle the "hottub.filtered.water.temp.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.filtered.water.temp.set","params":{"filtered.water.temp.set":37.5}}
 */
void hottub_filtered_water_temp_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *filtered_water_temp_item = param.key_value;
    float filtered_water_temp = (float)filtered_water_temp_item->valuedouble;
    hot_tub_controller_set_filtered_water_temp(filtered_water_temp);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "filtered.water.temp.set", filtered_water_temp);
    hottub_callback_response(root, response);
} // End of hottub_filtered_water_temp_set_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.air.temperature.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.air.temperature.get","params":""}
 */
void hottub_air_temperature_get_callback(cJSON *root) {
    float air_temp = hot_tub_controller_get_air_temp();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "air.temperature.get", air_temp);
    hottub_callback_response(root, response);
} // End of hottub_air_temperature_get_callback
//----------------------------------------------------------------------------- 


void hottub_air_temperature_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *air_temp_item = param.key_value;
    float air_temp = (float)air_temp_item->valuedouble;
    hot_tub_controller_set_air_temp(air_temp);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "air.temperature.set", air_temp);
    hottub_callback_response(root, response);
} // End of hottub_air_temperature_set_callback
//----------------------------------------------------------------------------- 


/**
 * @brief Callback function to handle the "hottub.humidity.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.humidity.get","params":""}
 */
void hottub_humidity_get_callback(cJSON *root) {
    float humidity = hot_tub_controller_get_humidity();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "humidity.get", humidity);
    hottub_callback_response(root, response);
} // End of hottub_humidity_get_callback
//----------------------------------------------------------------------------- 


void hottub_humidity_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *humidity_item = param.key_value;
    float humidity = (float)humidity_item->valuedouble;
    hot_tub_controller_set_humidity(humidity);

    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "humidity.set", humidity);
    hottub_callback_response(root, response);
} // End of hottub_humidity_set_callback


/**
 * @brief Callback function to handle the "hottub.setpoint.temperature.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.setpoint.temperature.get","params":""}
 */
void hottub_setpoint_temperature_get_callback(cJSON *root) {
    float setpoint_temp = hot_tub_controller_get_setpoint_temp();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "setpoint.temperature.get", setpoint_temp);
    hottub_callback_response(root, response);
} // End of hottub_setpoint_temperature_get_callback
//----------------------------------------------------------------------------- 


/**
 * @brief Callback function to handle the "hottub.setpoint.temperature.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.setpoint.temperature.set","params":{"setpoint.temperature.set":37.5}}
 */
void hottub_setpoint_temperature_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *setpoint_temp_item = param.key_value;
    float setpoint_temp = (float)setpoint_temp_item->valuedouble;
    hot_tub_controller_set_setpoint_temp(setpoint_temp);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "setpoint.temperature.set", setpoint_temp);
    hottub_callback_response(root, response);
} // End of hottub_setpoint_temperature_set_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.high.hysteresis.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.high.hysteresis.get","params":""}
 */
void hottub_high_hysteresis_get_callback(cJSON *root) {
    float high_hysteresis = hot_tub_controller_get_high_hysteresis();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "high.hysteresis.get", high_hysteresis);
    hottub_callback_response(root, response);
} // End of hottub_high_hysteresis_get_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.high.hysteresis.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.high.hysteresis.set","params":{"high.hysteresis.set":2.0}}
 */
void hottub_high_hysteresis_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *high_hysteresis_item = param.key_value;
    float high_hysteresis = (float)high_hysteresis_item->valuedouble;
    hot_tub_controller_set_high_hysteresis(high_hysteresis);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "high.hysteresis.set", high_hysteresis);
    hottub_callback_response(root, response);
} // End of hottub_high_hysteresis_set_callback
//-----------------------------------------------------------------------------

/**
 * @brief Callback function to handle the "hottub.low.hysteresis.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.low.hysteresis.get","params":""}
 */
void hottub_low_hysteresis_get_callback(cJSON *root) {
    float low_hysteresis = hot_tub_controller_get_low_hysteresis();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "low.hysteresis.get", low_hysteresis);
    hottub_callback_response(root, response);
} // End of hottub_low_hysteresis_get_callback
//-----------------------------------------------------------------------------     


/**
 * @brief Callback function to handle the "hottub.low.hysteresis.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.low.hysteresis.set","params":{"low.hysteresis.set":1.0}}
 */
void hottub_low_hysteresis_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *low_hysteresis_item = param.key_value;
    float low_hysteresis = (float)low_hysteresis_item->valuedouble;
    hot_tub_controller_set_low_hysteresis(low_hysteresis);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "low.hysteresis.set", low_hysteresis);
    hottub_callback_response(root, response);
} // End of hottub_low_hysteresis_set_callback
//-----------------------------------------------------------------------------     


/**
 * @brief Callback function to handle the "hottub.pump.state.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.pump.state.get","params":""}
 */
void hottub_pump_state_get_callback(cJSON *root) {
    pump_state_t pump_state;
    hot_tub_controller_pump_state_get(&pump_state);
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "pump.state.get", (int)pump_state);
    hottub_callback_response(root, response);
} // End of hottub_pump_state_get_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.pump.state.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.pump.state.set","params":{"pump.state.set":1}}
 */
void hottub_pump_state_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *pump_state_item = param.key_value;
    pump_state_t pump_state = (pump_state_t)pump_state_item->valueint;
    hot_tub_controller_set_pump(pump_state);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "pump.state.set", (int)pump_state);
    hottub_callback_response(root, response);
} // End of hottub_pump_state_set_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.low.pass.filter.alpha.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.low.pass.filter.alpha.get","params":""}
 */
void hottub_low_pass_filter_alpha_get_callback(cJSON *root) {
    float low_pass_filter_alpha = hot_tub_controller_get_low_pass_filter_alpha();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "low.pass.filter.alpha.get", low_pass_filter_alpha);
    hottub_callback_response(root, response);
} // End of hottub_low_pass_filter_alpha_get_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.low.pass.filter.alpha.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.low.pass.filter.alpha.set","params":{"low.pass.filter.alpha.set":0.5}}
 */
void hottub_low_pass_filter_alpha_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *low_pass_filter_alpha_item = param.key_value;
    float low_pass_filter_alpha = (float)low_pass_filter_alpha_item->valuedouble;
    hot_tub_controller_set_low_pass_filter_alpha(low_pass_filter_alpha);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "low.pass.filter.alpha.set", low_pass_filter_alpha);
    hottub_callback_response(root, response);
} // End of hottub_low_pass_filter_alpha_set_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.safety.switch.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.safety.switch.get","params":""}
 */
void hottub_safety_switch_get_callback(cJSON *root) {
    bool safety_switch = hot_tub_controller_get_safety_switch();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "safety.switch.get", safety_switch);
    hottub_callback_response(root, response);
} // End of hottub_safety_switch_get_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.safety.switch.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.safety.switch.set","params":{"safety.switch.set":true}}
 */
void hottub_safety_switch_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *safety_switch_item = param.key_value;
    bool safety_switch = cJSON_IsTrue(safety_switch_item);
    hot_tub_controller_set_safety_switch(safety_switch);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddBoolToObject(response, "safety.switch.set", safety_switch);
    hottub_callback_response(root, response);
} // End of hottub_safety_switch_set_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.error.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.error.get","params":""}
 */
void hottub_error_get_callback(cJSON *root) {
    int error = hot_tub_controller_get_error_code();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "error.get", (int)error);
    hottub_callback_response(root, response);
} // End of hottub_error_get_callback
//-----------------------------------------------------------------------------     


/**
 * @brief Callback function to handle the "hottub.error.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.error.set","params":{"error.set":1}}
 */
void hottub_error_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *error_item = param.key_value;
    int error = error_item->valueint;
    hot_tub_controller_set_error_code(error);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "error.set", (int)error);
    hottub_callback_response(root, response);
} // End of hottub_error_set_callback
//----------------------------------------------------------------------------- 


/**
 * @brief Callback function to handle the "hottub.pump.pre.run.time.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.pump.pre.run.time.get","params":""}
 */
void hottub_pump_pre_run_time_get_callback(cJSON *root) {
    float pre_run_time = hot_tub_controller_get_pump_pre_run_time();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "pump.pre.run.time.get", pre_run_time);
    hottub_callback_response(root, response);
} // End of hottub_pump_pre_run_time_get_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.pump.pre.run.time.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.pump.pre.run.time.set","params":{"pump.pre.run.time.set":30.0}}
 */
void hottub_pump_pre_run_time_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *pre_run_time_item = param.key_value;
    float pre_run_time = (float)pre_run_time_item->valuedouble;
    hot_tub_controller_set_pump_pre_run_time(pre_run_time);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "pump.pre.run.time.set", pre_run_time);
    hottub_callback_response(root, response);
} // End of hottub_pump_pre_run_time_set_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.pump.post.run.time.get" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.pump.post.run.time.get","params":""}
 */
void hottub_pump_post_run_time_get_callback(cJSON *root) {
    float post_run_time = hot_tub_controller_get_pump_post_run_time();
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "pump.post.run.time.get", post_run_time);
    hottub_callback_response(root, response);
} // End of hottub_pump_post_run_time_get_callback
//-----------------------------------------------------------------------------


/**
 * @brief Callback function to handle the "hottub.pump.post.run.time.set" command received via JSON service.
 *
 * @param root The cJSON object containing the command and its data.
 *
 * @note Ex: command received: {"id":1,"type":"req","cmd":"hottub.pump.post.run.time.set","params":{"pump.post.run.time.set":30.0}}
 */
void hottub_pump_post_run_time_set_callback(cJSON *root) {
    param_tupple_t param;

    if (parse_json_param(root, &param.key_name, &param.key_value) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to parse JSON params");
        return;
    }
    cJSON *post_run_time_item = param.key_value;
    float post_run_time = (float)post_run_time_item->valuedouble;
    hot_tub_controller_set_pump_post_run_time(post_run_time);

    // Create a response JSON object
    cJSON *response = cJSON_CreateObject();
    cJSON_AddNumberToObject(response, "pump.post.run.time.set", post_run_time);
    hottub_callback_response(root, response);
} // End of hottub_pump_post_run_time_set_callback
//-----------------------------------------------------------------------------


/**
 * @brief Get the current local time and format it as a string.
 *
 * @param strftime_buf Buffer to hold the formatted time string.
 * @param buf_size Size of the buffer.
 *
 * @note The time is formatted as "YYYY-MM-DD HH:MM:SS".
 */
void get_current_time(char *strftime_buf, size_t buf_size)
{
    time_t now;
    struct tm timeinfo;

    time(&now);                          // seconds since Unix epoch
    localtime_r(&now, &timeinfo);        // convert to broken-down local time

    strftime(strftime_buf, buf_size, "%Y-%m-%d %H:%M:%S", &timeinfo);
    // ESP_LOGI(TAG, "Current time: %s", strftime_buf);
}
//-----------------------------------------------------------------------------



/**
 * @brief Array of callback functions for the hot tub controller commands.
 *
 * Each entry in the array consists of a command string and its corresponding callback function.
 * The array is terminated with a sentinel value (NULL, NULL).
 */
callbacks_t hot_tub_callbacks[] = {
    {"hottub.status.get", hottub_status_get_callback},
    {"hottub.automode.get", hottub_auto_mode_get_callback},
    {"hottub.automode.set", hottub_auto_mode_set_callback},
    {"hottub.heater.status.get", hottub_heater_status_get_callback},
    {"hottub.heater.status.set", hottub_heater_status_set_callback},
    {"hottub.temperature.unit.get", hottub_temperature_unit_get_callback},
    {"hottub.temperature.unit.set", hottub_temperature_unit_set_callback},
    {"hottub.water.temperature.get", hottub_water_temperature_get_callback},
    {"hottub.water.temperature.set", hottub_water_temperature_set_callback},
    {"hottub.filtered.water.temp.get", hottub_filtered_water_temp_get_callback},
    {"hottub.filtered.water.temp.set", hottub_filtered_water_temp_set_callback},
    {"hottub.simulation.mode.get", hottub_simulation_mode_get_callback},
    {"hottub.simulation.mode.set", hottub_simulation_mode_set_callback},
    {"hottub.air.temperature.get", hottub_air_temperature_get_callback},
    {"hottub.air.temperature.set", hottub_air_temperature_set_callback},
    {"hottub.humidity.get", hottub_humidity_get_callback},
    {"hottub.humidity.set", hottub_humidity_set_callback},
    {"hottub.setpoint.temperature.get", hottub_setpoint_temperature_get_callback},
    {"hottub.setpoint.temperature.set", hottub_setpoint_temperature_set_callback},
    {"hottub.high.hysteresis.get", hottub_high_hysteresis_get_callback},
    {"hottub.high.hysteresis.set", hottub_high_hysteresis_set_callback},
    {"hottub.low.hysteresis.get", hottub_low_hysteresis_get_callback},
    {"hottub.low.hysteresis.set", hottub_low_hysteresis_set_callback},
    {"hottub.pump.state.get", hottub_pump_state_get_callback},
    {"hottub.pump.state.set", hottub_pump_state_set_callback},
    {"hottub.low.pass.filter.alpha.get", hottub_low_pass_filter_alpha_get_callback},
    {"hottub.low.pass.filter.alpha.set", hottub_low_pass_filter_alpha_set_callback},
    {"hottub.safety.switch.get", hottub_safety_switch_get_callback},
    {"hottub.safety.switch.set", hottub_safety_switch_set_callback},
    {"hottub.pump.pre.run.time.get", hottub_pump_pre_run_time_get_callback},
    {"hottub.pump.pre.run.time.set", hottub_pump_pre_run_time_set_callback},
    {"hottub.pump.post.run.time.get", hottub_pump_post_run_time_get_callback},
    {"hottub.pump.post.run.time.set", hottub_pump_post_run_time_set_callback},
    {"hottub.error.get", hottub_error_get_callback},
    {"hottub.error.set", hottub_error_set_callback},
    {NULL, NULL} // Sentinel value to mark the end of the array
};
//-----------------------------------------------------------------------------


/**
 * @brief Registry of callback functions for the hot tub controller commands.
 *
 * This structure holds the array of callbacks and the number of callbacks.
 */
static callbacks_registry_t hot_tub_controller_callbacks_registry = {
    .callbacks = hot_tub_callbacks,
    .num_callbacks = sizeof(hot_tub_callbacks) / sizeof(hot_tub_callbacks[0]) - 1 // Exclude the sentinel
};
//-----------------------------------------------------------------------------

// Pointer to the registry of callback functions for the hot tub controller commands.
callbacks_registry_t *hot_tub_controller_callbacks = &hot_tub_controller_callbacks_registry;

/**
 * @brief Register the callback functions for the,
 * hot tub controller commands with the JSON service. 
 */
esp_err_t hot_tub_controller_register_callbacks()
{
    // loop through the hot_tub_controller_callbacks and register each command with the JSON service
    for (size_t i = 0; i < hot_tub_controller_callbacks->num_callbacks; i++) 
    {
        const char *command = hot_tub_controller_callbacks->callbacks[i].command;
        json_cmd_callback_t callback = hot_tub_controller_callbacks->callbacks[i].callback;

        if (!json_service_register_command(command, callback, CORE_0)) {
            return ESP_FAIL;
        }
    }

    return ESP_OK;
} // end of hot_tub_controller_register_callbacks()
//-----------------------------------------------------------------------------


// json_service_register_command("hottub.status.get", hottub_status_get_callback, 0);
// json_service_register_command("hottub.automode.get", hottub_auto_mode_get_callback, 0);
// json_service_register_command("hottub.automode.set", hottub_auto_mode_set_callback, 0);
// json_service_register_command("hottub.heater.status.get", hottub_heater_status_get_callback, 0);    
// json_service_register_command("hottub.heater.status.set", hottub_heater_status_set_callback, 0);    
// json_service_register_command("hottub.temperature.unit.get", hottub_temperature_unit_get_callback, 0);
// json_service_register_command("hottub.temperature.unit.set", hottub_temperature_unit_set_callback, 0);
// json_service_register_command("hottub.water.temperature.get", hottub_water_temperature_get_callback, 0);
// json_service_register_command("hottub.water.temperature.set", hottub_water_temperature_set_callback, 0);
// json_service_register_command("hottub.filtered.water.temp.get", hottub_filtered_water_temp_get_callback, 0);
// json_service_register_command("hottub.filtered.water.temp.set", hottub_filtered_water_temp_set_callback, 0);
// json_service_register_command("hottub.simulation.mode.get", hottub_simulation_mode_get_callback, 0);
// json_service_register_command("hottub.simulation.mode.set", hottub_simulation_mode_set_callback, 0);
// json_service_register_command("hottub.air.temperature.get", hottub_air_temperature_get_callback, 0);
// json_service_register_command("hottub.air.temperature.set", hottub_air_temperature_set_callback, 0);
// json_service_register_command("hottub.humidity.get", hottub_humidity_get_callback, 0);
// json_service_register_command("hottub.humidity.set", hottub_humidity_set_callback, 0);
// json_service_register_command("hottub.setpoint.temperature.get", hottub_setpoint_temperature_get_callback, 0);
// json_service_register_command("hottub.setpoint.temperature.set", hottub_setpoint_temperature_set_callback, 0);
// json_service_register_command("hottub.high.hysteresis.get", hottub_high_hysteresis_get_callback, 0);
// json_service_register_command("hottub.high.hysteresis.set", hottub_high_hysteresis_set_callback, 0);
// json_service_register_command("hottub.low.hysteresis.get", hottub_low_hysteresis_get_callback, 0);
// json_service_register_command("hottub.low.hysteresis.set", hottub_low_hysteresis_set_callback, 0);
// json_service_register_command("hottub.pump.state.get", hottub_pump_state_get_callback, 0);
// json_service_register_command("hottub.pump.state.set", hottub_pump_state_set_callback, 0);
// json_service_register_command("hottub.low.pass.filter.alpha.get", hottub_low_pass_filter_alpha_get_callback, 0);
// json_service_register_command("hottub.low.pass.filter.alpha.set", hottub_low_pass_filter_alpha_set_callback, 0);
// json_service_register_command("hottub.safety.switch.get", hottub_safety_switch_get_callback, 0);
// json_service_register_command("hottub.safety.switch.set", hottub_safety_switch_set_callback, 0);
// json_service_register_command("hottub.pump.pre.run.time.get", hottub_pump_pre_run_time_get_callback, 0);
// json_service_register_command("hottub.pump.pre.run.time.set", hottub_pump_pre_run_time_set_callback, 0);
// json_service_register_command("hottub.pump.post.run.time.get", hottub_pump_post_run_time_get_callback, 0);
// json_service_register_command("hottub.pump.post.run.time.set", hottub_pump_post_run_time_set_callback, 0);
// json_service_register_command("hottub.status.get", hottub_status_get_callback, 0);
