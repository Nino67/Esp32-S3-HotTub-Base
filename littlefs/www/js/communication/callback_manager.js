/**
 * @file callback_manager.js
 * @author Gaetano (Nino) Ricca (gricca1967@gmail.com)
 * @brief This file manages the registration and dispatching of command handlers for the web interface.
 *
 * @details This file provides functions to register and dispatch command handlers.
 * It supports both synchronous and asynchronous callbacks seamlessly.
 * 
 * @note Matching hardware:
 * - model: ESP32-S3-DevKitC-1.         SKU: ESP32-S3-DevKitC-1-N16R8
 * - mfg: RS Engineering.               date: 2026-06-22
 *
 * @version 0.1
 * @date 2026-08-21 
 *
 * @copyright Copyright (c) 2026
 *
 */



/**
 * @brief A Map array to hold the callbacks for each command.
 * This allows for dynamic registration of command handlers.
 */
const handlers = new Map();



/**
 * @brief Registers a single command handler.
 * @param {string} command - The command string to register.
 * @param {function} callback - The callback function to handle the command.
 */
function registerHandler(command, callback) {
    handlers.set(command, callback);
    // console.log(`[CallbackManager] Registered handler for: ${command}`);
}
//-----------------------------------------------------------------------------


/**
 * @brief Registers multiple command handlers.
 * @param {Object} routes - An object where keys are command strings and values are callback functions.
 */
function registerHandlers(routes) {
    const entries = Object.entries(routes);
    // console.log(`[CallbackManager] Registering ${entries.length} routes`);
    entries.forEach(([command, callback]) => {
        if (command && typeof callback === 'function') {
            registerHandler(command, callback);
        }
    });
}
//-----------------------------------------------------------------------------


/**
 * @brief Dispatches a command to the appropriate handler.
 * @param {string} command - The command string to dispatch.
 * @param {any} data - The data to pass to the callback function.
 * @returns {Promise<any>} - The result of the callback function.
 */
async function dispatch(command, data) {
    const callback = handlers.get(command);
    if (!callback) {
        console.warn(`[CallbackManager] No handler registered for: ${command}`);
        return null;
    }

    // console.log(`[CallbackManager] Dispatching command: ${command}`);
    try {
        return await callback(data);
    } catch (error) {
        console.error(`[CallbackManager] Failed executing '${command}':`, error);
        throw error;
    }
}
//-----------------------------------------------------------------------------


export const callback_manager = {
    registerHandler,
    registerHandlers,
    dispatch
};










// Example usage of the dispatch function with both synchronous and asynchronous callbacks
// because i never remeber the uses after i write the dam things.. heh
//
// // Pure synchronous handler - works out of the box
// function hottub_water_temperature_get_callback(data) {
//     updateTemperatureGauge(data.celsius);
//     return data.celsius;
// }
//
// // Asynchronous handler - works natively without changing dispatch()
// async function hottub_pump_state_set_callback(data) {
//     const ack = await sendRpcOverWebSocket("hottub.pump.state.set", data);
//     updatePumpUI(ack.state);
//     return ack.success;
// }
// 
// // Define routes as a standard JS object literal
// const hotTubRoutes = {
//     "system.status.get": system_status_get_callback,
//     "hottub.status.get": hottub_status_get_callback,
//     "hottub.automode.get": hottub_auto_mode_get_callback,
//     "hottub.automode.set": hottub_auto_mode_set_callback,
// };

// // Register them all in one shot
// registerHandlers(hotTubRoutes);