/**
 * @file hottub_callbacks.js
 * @author Gaetano (Nino) Ricca (gricca1967@gmail.com)
 * @brief  Client-side callback handlers for the Hot Tub Controller web interface.
 *
 * @details This file contains the client-side callback handlers for the Hot Tub Controller web interface.
 * It handles WebSocket communication, OTA updates, and chart management.
 *
 * @note Matching hardware:
 * - model: ESP32-S3-DevKitC-1.         SKU: ESP32-S3-DevKitC-1-N16R8
 * - mfg: RS Engineering.               date: 2026-06-22
 *
 * @version 0.1
 * @date 2026-07-30 
 *
 * @copyright Copyright (c) 2026
 *
 */
/*****************************************************************************/
/*****************************************************************************/



/*****************************************************************************/
/**
 * @brief Javascript structure to hold the state of the hot tub controller.
 */
export const hottub = {
    safetySwitch: false,
    heaterOn: false,
    autoMode: false,
    tempUnitCelsius: true,
    pumpOnLight: false,
    heaterOnLight: false,
    waterTemp: 0.0,
    filteredWaterTemp: 0.0,
    airTemp: 0.0,
    humidity: 0.0,
    setpointTemp: 0.0,
    lowPassFilterAlpha: 0.0,
    highHysteresis: 0.0,
    lowHysteresis: 0.0,
    pumpPreRunTime: 0.0,
    pumpPostRunTime: 0.0,
    pumpState: 0,
    initialStartTime: "",
    lastUpdateTime: "",
    simulationMode: 0,
    errorCode: 0
};
/*****************************************************************************/ 
/*****************************************************************************/


/*****************************************************************************/
/****** Hot Tub callback functions *******************************************/
/*****************************************************************************/
/**
 * @brief Callback function for the "hottub.status.get" command.
 *
 * This function is called when a response to the "hottub.status.get" command is received.
 * It updates the hottub state structure with the values from the response payload.
 *
 * @param {Object} payload - The response payload containing the hot tub status information.
 */
function hottub_status_get_callback(payload) {
    if (payload && payload.response) {
        const response = payload.response;
        hottub.safetySwitch = response.safetySwitch;
        hottub.heaterOn = response.heaterOn;
        hottub.autoMode = response.autoMode;
        hottub.tempUnitCelsius = response.tempUnitCelsius;
        hottub.pumpOnLight = response.pumpOnLight;
        hottub.heaterOnLight = response.heaterOnLight;
        hottub.waterTemp = response.waterTemp;
        hottub.filteredWaterTemp = response.filteredWaterTemp;
        hottub.airTemp = response.airTemp;
        hottub.humidity = response.humidity;
        hottub.setpointTemp = response.setpointTemp;
        hottub.lowPassFilterAlpha = response.lowPassFilterAlpha;
        hottub.highHysteresis = response.highHysteresis;
        hottub.lowHysteresis = response.lowHysteresis;
        hottub.pumpPreRunTime = response.pumpPreRunTime;
        hottub.pumpPostRunTime = response.pumpPostRunTime;
        hottub.pumpState = response.pumpState;
        hottub.initialStartTime = response.initialStartTime;
        hottub.lastUpdateTime = response.lastUpdateTime;
        hottub.simulationMode = response.simulationMode;
        hottub.errorCode = response.errorCode;
    }
}
//--------------------------------------------------------------------------- 

// // Asynchronous handler - works natively without changing dispatch()
// async function hottub_pump_state_set_callback(data) {
//     const ack = await sendRpcOverWebSocket("hottub.pump.state.set", data);
//     updatePumpUI(ack.state);
//     return ack.success;
// }



function hottub_auto_mode_get_callback(payload) {
    if (payload && payload.response) {
        hottub.autoMode = payload.response.autoMode;
    }
}
//---------------------------------------------------------------------------

function hottub_auto_mode_set_callback(payload) {
    if (payload && payload.response) {
        hottub.autoMode = payload.response.autoMode;
    }
}
//---------------------------------------------------------------------------

function hottub_heater_status_get_callback(payload) {
    if (payload && payload.response) {
        hottub.heaterOn = payload.response.heaterOn;
    }
}
//---------------------------------------------------------------------------

function hottub_heater_status_set_callback(payload) {
    if (payload && payload.response) {
        hottub.heaterOn = payload.response.heaterOn;
    }
}
//---------------------------------------------------------------------------

function hottub_temperature_unit_get_callback(payload) {
    if (payload && payload.response) {
        hottub.tempUnitCelsius = payload.response.tempUnitCelsius;
    }
}
//---------------------------------------------------------------------------

function hottub_temperature_unit_set_callback(payload) {
    if (payload && payload.response) {
        hottub.tempUnitCelsius = payload.response.tempUnitCelsius;
    }
}   
//---------------------------------------------------------------------------

function hottub_water_temperature_get_callback(payload) {
    if (payload && payload.response) {
        hottub.waterTemp = payload.response.waterTemp;
    }
}
//---------------------------------------------------------------------------

function hottub_water_temperature_set_callback(payload) {
    if (payload && payload.response) {
        hottub.waterTemp = payload.response.waterTemp;
    }
}
//---------------------------------------------------------------------------

function hottub_filtered_water_temp_get_callback(payload) {
    if (payload && payload.response) {
        hottub.filteredWaterTemp = payload.response.filteredWaterTemp;
    }
}
//---------------------------------------------------------------------------

function hottub_filtered_water_temp_set_callback(payload) {
    if (payload && payload.response) {
        hottub.filteredWaterTemp = payload.response.filteredWaterTemp;
    }
}   
//---------------------------------------------------------------------------

function hottub_simulation_mode_get_callback(payload) {
    if (payload && payload.response) {
        hottub.simulationMode = payload.response.simulationMode;
    }
}
//---------------------------------------------------------------------------

function hottub_simulation_mode_set_callback(payload) {
    if (payload && payload.response) {
        hottub.simulationMode = payload.response.simulationMode;
    }
}   

//---------------------------------------------------------------------------
function hottub_air_temperature_get_callback(payload) {
    if (payload && payload.response) {
        hottub.airTemp = payload.response.airTemp;
    }
}
//---------------------------------------------------------------------------

function hottub_air_temperature_set_callback(payload) {
    if (payload && payload.response) {
        hottub.airTemp = payload.response.airTemp;
    }
}
//---------------------------------------------------------------------------

function hottub_humidity_get_callback(payload) {
    if (payload && payload.response) {
        hottub.humidity = payload.response.humidity;
    }
}
//---------------------------------------------------------------------------

function hottub_humidity_set_callback(payload) {
    if (payload && payload.response) {
        hottub.humidity = payload.response.humidity;
    }
}
//---------------------------------------------------------------------------

function hottub_setpoint_temperature_get_callback(payload) {
    if (payload && payload.response) {
        hottub.setpointTemp = payload.response.setpointTemp;
    }
}
//---------------------------------------------------------------------------    

function hottub_setpoint_temperature_set_callback(payload) {
    if (payload && payload.response) {
        hottub.setpointTemp = payload.response.setpointTemp;
    }
}
//---------------------------------------------------------------------------

function hottub_high_hysteresis_get_callback(payload) {
    if (payload && payload.response) {
        hottub.highHysteresis = payload.response.highHysteresis;
    }
}
//---------------------------------------------------------------------------

function hottub_high_hysteresis_set_callback(payload) {
    if (payload && payload.response) {
        hottub.highHysteresis = payload.response.highHysteresis;
    }
}
//---------------------------------------------------------------------------    

function hottub_low_hysteresis_get_callback(payload) {
    if (payload && payload.response) {
        hottub.lowHysteresis = payload.response.lowHysteresis;
    }
}
//---------------------------------------------------------------------------

function hottub_low_hysteresis_set_callback(payload) {
    if (payload && payload.response) {
        hottub.lowHysteresis = payload.response.lowHysteresis;
    }
}   
//---------------------------------------------------------------------------

function hottub_pump_state_get_callback(payload) {
    if (payload && payload.response) {
        hottub.pumpState = payload.response.pumpState;
    }
}
//---------------------------------------------------------------------------

function hottub_pump_state_set_callback(payload) {
    if (payload && payload.response) {
        hottub.pumpState = payload.response.pumpState;
    }
}   
//---------------------------------------------------------------------------

function hottub_low_pass_filter_alpha_get_callback(payload) {
    if (payload && payload.response) {
        hottub.lowPassFilterAlpha = payload.response.lowPassFilterAlpha;
    }
}
//---------------------------------------------------------------------------

function hottub_low_pass_filter_alpha_set_callback(payload) {
    if (payload && payload.response) {
        hottub.lowPassFilterAlpha = payload.response.lowPassFilterAlpha;
    }
}   
//---------------------------------------------------------------------------

function hottub_safety_switch_get_callback(payload) {
    if (payload && payload.response) {
        hottub.safetySwitch = payload.response.safetySwitch;
    }
}
//---------------------------------------------------------------------------

function hottub_safety_switch_set_callback(payload) {
    if (payload && payload.response) {
        hottub.safetySwitch = payload.response.safetySwitch;
    }
}
//---------------------------------------------------------------------------

function hottub_pump_pre_run_time_get_callback(payload) {
    if (payload && payload.response) {
        hottub.pumpPreRunTime = payload.response.pumpPreRunTime;
    }
}
//---------------------------------------------------------------------------

function hottub_pump_pre_run_time_set_callback(payload) {
    if (payload && payload.response) {
        hottub.pumpPreRunTime = payload.response.pumpPreRunTime;
    }
}
//---------------------------------------------------------------------------

function hottub_pump_post_run_time_get_callback(payload) {
    if (payload && payload.response) {
        hottub.pumpPostRunTime = payload.response.pumpPostRunTime;
    }
}
//---------------------------------------------------------------------------

function hottub_pump_post_run_time_set_callback(payload) {
    if (payload && payload.response) {
        hottub.pumpPostRunTime = payload.response.pumpPostRunTime;
    }
}
//---------------------------------------------------------------------------

function hottub_error_get_callback(payload) {
    if (payload && payload.response) {
        hottub.errorCode = payload.response.errorCode;
    }
}
//---------------------------------------------------------------------------

function hottub_error_set_callback(payload) {
    if (payload && payload.response) {
        hottub.errorCode = payload.response.errorCode;
    }
}
//---------------------------------------------------------------------------   
/*****************************************************************************/
/*****************************************************************************/
function system_status_get_callback(payload) {
    const statusView = document.getElementById('systemStatusView');
    // console.log("System Status Callback Invoked");

    if (payload && payload.response) {
        const response = payload.response;
        // console.log("System Status Response:", response);
    }
    statusView.textContent = JSON.stringify(payload, null, 2);
}


/**
 * @brief Object of callback functions for the hot tub controller commands.
 */
export const callbacks = {
    "system.status.get": system_status_get_callback,
    "hottub.status.get": hottub_status_get_callback,
    "hottub.automode.get": hottub_auto_mode_get_callback,
    "hottub.automode.set": hottub_auto_mode_set_callback,
    "hottub.heater.status.get": hottub_heater_status_get_callback,
    "hottub.heater.status.set": hottub_heater_status_set_callback,
    "hottub.temperature.unit.get": hottub_temperature_unit_get_callback,
    "hottub.temperature.unit.set": hottub_temperature_unit_set_callback,
    "hottub.water.temperature.get": hottub_water_temperature_get_callback,
    "hottub.water.temperature.set": hottub_water_temperature_set_callback,
    "hottub.filtered.water.temp.get": hottub_filtered_water_temp_get_callback,
    "hottub.filtered.water.temp.set": hottub_filtered_water_temp_set_callback,
    "hottub.simulation.mode.get": hottub_simulation_mode_get_callback,
    "hottub.simulation.mode.set": hottub_simulation_mode_set_callback,
    "hottub.air.temperature.get": hottub_air_temperature_get_callback,
    "hottub.air.temperature.set": hottub_air_temperature_set_callback,
    "hottub.humidity.get": hottub_humidity_get_callback,
    "hottub.humidity.set": hottub_humidity_set_callback,
    "hottub.setpoint.temperature.get": hottub_setpoint_temperature_get_callback,
    "hottub.setpoint.temperature.set": hottub_setpoint_temperature_set_callback,
    "hottub.high.hysteresis.get": hottub_high_hysteresis_get_callback,
    "hottub.high.hysteresis.set": hottub_high_hysteresis_set_callback,
    "hottub.low.hysteresis.get": hottub_low_hysteresis_get_callback,
    "hottub.low.hysteresis.set": hottub_low_hysteresis_set_callback,
    "hottub.pump.state.get": hottub_pump_state_get_callback,
    "hottub.pump.state.set": hottub_pump_state_set_callback,
    "hottub.low.pass.filter.alpha.get": hottub_low_pass_filter_alpha_get_callback,
    "hottub.low.pass.filter.alpha.set": hottub_low_pass_filter_alpha_set_callback,
    "hottub.safety.switch.get": hottub_safety_switch_get_callback,
    "hottub.safety.switch.set": hottub_safety_switch_set_callback,
    "hottub.pump.pre.run.time.get": hottub_pump_pre_run_time_get_callback,
    "hottub.pump.pre.run.time.set": hottub_pump_pre_run_time_set_callback,
    "hottub.pump.post.run.time.get": hottub_pump_post_run_time_get_callback,
    "hottub.pump.post.run.time.set": hottub_pump_post_run_time_set_callback,
    "hottub.error.get": hottub_error_get_callback,
    "hottub.error.set": hottub_error_set_callback,
};
//-----------------------------------------------------------------------------




export default {
    hottub,
    callbacks,
    system_status_get_callback,
    hottub_status_get_callback,
    hottub_auto_mode_get_callback,
    hottub_auto_mode_set_callback,
    hottub_heater_status_get_callback,
    hottub_heater_status_set_callback,
    hottub_temperature_unit_get_callback,
    hottub_temperature_unit_set_callback,
    hottub_water_temperature_get_callback,
    hottub_water_temperature_set_callback,
    hottub_filtered_water_temp_get_callback,
    hottub_filtered_water_temp_set_callback,
    hottub_simulation_mode_get_callback,
    hottub_simulation_mode_set_callback,
    hottub_air_temperature_get_callback,
    hottub_air_temperature_set_callback,
    hottub_humidity_get_callback,
    hottub_humidity_set_callback,
    hottub_setpoint_temperature_get_callback,
    hottub_setpoint_temperature_set_callback,
    hottub_high_hysteresis_get_callback,
    hottub_high_hysteresis_set_callback,
    hottub_low_hysteresis_get_callback,
    hottub_low_hysteresis_set_callback,
    hottub_pump_state_get_callback,
    hottub_pump_state_set_callback,
    hottub_low_pass_filter_alpha_get_callback,
    hottub_low_pass_filter_alpha_set_callback,
    hottub_safety_switch_get_callback,
    hottub_safety_switch_set_callback,
    hottub_pump_pre_run_time_get_callback,
    hottub_pump_pre_run_time_set_callback,
    hottub_pump_post_run_time_get_callback,
    hottub_pump_post_run_time_set_callback,
    hottub_error_get_callback,
    hottub_error_set_callback   
};