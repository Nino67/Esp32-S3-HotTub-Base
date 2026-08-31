
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

/**
 * @brief Safely updates the Hottub state object with new values from a payload.
 * @param {Object} newState - An object containing new state values to merge into the hottub state.     
 * @param {HTMLElement} element - The DOM element to update.
 * @param {string} value - The text value to set.
 */








// Helper Functions

/**
 * 
 * @param {HTMLElement} element
 * @param {string} value
 */
export const safeSetText = (element, value) => {
  if (element) {
    element.textContent = value;
  }
};
//-----------------------------------------------------------------------------


/**
 * 
 * @param {HTMLElement} element
 * @param {string} property
 * @param {string} value
 */ 
export const safeSetStyle = (element, property, value) => {
  if (element) {
    element.style[property] = value;
  }
};
//-----------------------------------------------------------------------------
