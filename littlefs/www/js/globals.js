
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
export const updateHotTubState = (newState) => {
    if (typeof newState !== 'object' || newState === null) {
        console.warn('[updateHotTubState] Invalid state object:', newState);
        return;
    }

    // Update the hottub state with new values
    Object.keys(newState).forEach(key => {
        console.log(`[updateHotTubState] Processing key: ${key}, value: ${newState[key]}`);
        if (key in hottub) {
            hottub[key] = newState[key];
        } else {
            console.warn(`[updateHotTubState] Unknown property: ${key}`);
        }
    });

    // Update the UI elements based on the new state    
    const filteredTemperatureDisplay = document.getElementById('filteredTemperature');
    if (filteredTemperatureDisplay) {
        const tempUnit = hottub.tempUnitCelsius ? '°C' : '°F';
        safeSetText(filteredTemperatureDisplay, `${hottub.filteredWaterTemp.toFixed(1)} ${tempUnit}`);
    } else {
        console.warn('[updateHotTubState] filteredTemperature element not found');
    }   

};
/*****************************************************************************/







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
