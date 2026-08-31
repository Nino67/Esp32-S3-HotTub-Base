/**
 * @file app.js
 * @author Gaetano (Nino) Ricca (gricca1967@gmail.com)
 * @brief   Main application logic for the Hot Tub Controller web interface.
 *
 * @details This file contains the main application logic for the Hot Tub Controller web interface.
 * It handles WebSocket communication, OTA updates, and chart management.
 *
 * @note Matching hardware:
 * - model: ESP32-S3-DevKitC-1.         SKU: ESP32-S3-DevKitC-1-N16R8
 * - mfg: RS Engineering.               date: 2026-06-22
 *
 * @version 0.1
 * @date 2026-06-22 
 *
 * @copyright Copyright (c) 2026
 *
 */



import { createCrc32JsonWrapper } from '/js/communication/crc32_wrapper.js';
// import { createWebSocketClient } from '/js/communication/ws_client.js';
import { parseMessage } from '/js/communication/message_parser.js';
import { createAppState } from '/js/app_state.js';
import { createChartManager } from '/js/chart_manager.js';
import { hottub } from '/js/globals.js';
import { safeSetText, safeSetStyle } from '/js/globals.js';
import { ws_manager } from '/js/communication/ws_manager.js';

// UI Elements
const badge = document.getElementById('connBadge');
const stateView = document.getElementById('stateView');
const statusView = document.getElementById('systemStatusView');
const receiveView = document.getElementById('receiveView');
const sendView = document.getElementById('sendView');
const commandInput = document.getElementById('commandInput');
const otaStatus = document.getElementById('otaStatus');
const otaProgressBar = document.getElementById('otaProgressBar');
const sendBtn = document.getElementById('sendBtn');
const systemStatusBtn = document.getElementById('sendSystemStatusBtn');
const clearSystemStatusBtn = document.getElementById('clearSystemStatusBtn');
const otaBtn = document.getElementById('otaBtn');
const otaManifestBtn = document.getElementById('otaManifestBtn');
const chartContainer = document.getElementById('chartContainer');
const filteredTemperatureDisplay = document.getElementById('filteredTemperature');
const heatToggle = document.getElementById('heatToggle');
const autoModeToggle = document.getElementById('autoModeToggle');
const pumpModeInputs = Array.from(document.querySelectorAll('input[name="pumpMode"]'));
const pumpTrack = document.querySelector('.pump-track');
// const temperatureLabel = document.getElementById('temperature-label');


// Application State
// let client = null;
let otaPollingInterval = null;
let statusPollingInterval = null;
let requestId = 1;
const appState = createAppState({ latestRawMessage: null, latestPayload: null, latestHotTubState: null });
const chartManager = createChartManager();
let temperatureChartId = null;



/**
 * @brief Main application loop running at 1Hz.
 */
setInterval(() => {
  // Update the latest hot tub state in the app state
  appState.latestHotTubState = { ...hottub };
  
  // Update the UI elements based on the new state    
  // const filteredTemperatureDisplay = document.getElementById('filteredTemperature');
  if (filteredTemperatureDisplay) {
      const tempUnit = hottub.tempUnitCelsius ? '°C' : '°F';
      safeSetText(filteredTemperatureDisplay, `${hottub.filteredWaterTemp.toFixed(1)} ${tempUnit}`);
  } else {
      console.warn('[ws_client] filteredTemperature element not found');
  }

  // Update the temperature chart with the latest hot tub state
  updateTemperatureChart(appState.latestHotTubState);

  // Update the UI elements based on the latest hot tub state
  syncControlStateFromPayload(appState.latestHotTubState);
}, 1000);
//-----------------------------------------------------------------------------


// function setAutoModeControlState(isOn, pending = false) {
//   if (!autoModeToggle) {
//     return;
//   }

//   autoModeToggle.checked = Boolean(isOn);
//   autoModeToggle.disabled = Boolean(pending);

//   const row = autoModeToggle.closest('.toggle-row');
//   if (row) {
//     row.classList.toggle('is-pending', Boolean(pending));
//   }
//   sendHotTubCommand('hottub.automode.set', { 'autoMode': Boolean(isOn) });
// }




function setHeatControlState(isOn, pending = false) {
  if (!heatToggle) {
    return;
  }

  heatToggle.checked = Boolean(isOn);
  heatToggle.disabled = Boolean(pending);

  const row = heatToggle.closest('.toggle-row');
  if (row) {
    row.classList.toggle('is-pending', Boolean(pending));
  }
}



function setPumpControlState(level, pending = false) {
  const pumpLevel = Number(level) || 0;
  const positionMap = { 0: 0, 1: 1, 3: 2 };
  const position = positionMap[pumpLevel] ?? 0;

  pumpModeInputs.forEach((input) => {
    if (!input) {
      return;
    }

    const isSelected = Number(input.value) === pumpLevel;
    input.checked = isSelected;
    input.disabled = Boolean(pending);
  });

  if (pumpTrack) {
    pumpTrack.style.setProperty('--pump-position', String(position));
    pumpTrack.classList.toggle('is-pending', Boolean(pending));
  }
}

function syncControlStateFromPayload(response = {}) {
  if (typeof response.heaterOn === 'boolean' && heatToggle) {
    setHeatControlState(response.heaterOn, false);
  }

  // if (typeof response.autoMode === 'boolean' && autoModeToggle) {
  //   setAutoModeControlState(response.autoMode, false);
  // }

  if (typeof response.pumpState !== 'undefined' && pumpModeInputs.length) {
    setPumpControlState(response.pumpState, false);
  }
}



function sendHotTubCommand(command, params) {
  if (!ws_manager || ws_manager.readyState !== WebSocket.OPEN) {
    safeSetText(sendView, 'Socket is not open. Waiting for connection...');
    return;
  }

  const payload = {
    id: requestId += 1,
    type: 'req',
    cmd: command,
    params,
  };

  try {
    ws_manager.send(createCrc32JsonWrapper(payload));
    safeSetText(sendView, JSON.stringify(payload));
    // console.log('Sending:', payload);
  } catch (err) {
    safeSetText(sendView, `Invalid payload: ${err.message}`);
    console.error('Failed to send command:', err);
  }
}

// function setBadge(text, status) {
//   badge.textContent = text;
//   badge.dataset.status = status;
// }

function setOtaProgress(value) {
  const pct = Math.max(0, Math.min(100, Number(value) || 0));
  safeSetStyle(otaProgressBar, 'width', `${pct}%`);
  safeSetText(otaProgressBar, `${pct}%`);
}

function stopOtaPolling() {
  if (otaPollingInterval !== null) {
    clearInterval(otaPollingInterval);
    otaPollingInterval = null;
  }
}

function stopStatusPolling() {
  if (statusPollingInterval !== null) {
    clearInterval(statusPollingInterval);
    statusPollingInterval = null;
  }
}

function requestStatusSnapshot() {
  if (!ws_manager || ws_manager.readyState !== WebSocket.OPEN) {
    return;
  }

  const payload = {
    id: requestId = 1,
    type: 'req',
    cmd: 'hottub.status.get',
    params: '',
  };

  try {
    ws_manager.send(createCrc32JsonWrapper(payload));
  } catch (err) {
    console.error('Failed to request status snapshot:', err);
  }
}

function startStatusPolling() {
  if (statusPollingInterval !== null) {
    return;
  }

  requestStatusSnapshot();
  statusPollingInterval = setInterval(requestStatusSnapshot, 1000);
}

function startOtaPolling() {
  if (otaPollingInterval !== null) {
    return;
  }

  otaPollingInterval = setInterval(() => {
    if (ws_manager && ws_manager.readyState === WebSocket.OPEN) {
      // reserved for future polling commands
    } else {
      stopOtaPolling();
    }
  }, 500);
}

function updateOtaState(state) {
  safeSetText(otaStatus, state.ota_status || 'idle');
  setOtaProgress(state.ota_progress ?? 0);

  if (state.ota_pending) {
    startOtaPolling();
  } else {
    stopOtaPolling();
  }
}

export function renderParsedMessage(parsed, raw) {
  safeSetText(receiveView, raw);
  if (parsed.valid) {
    safeSetText(stateView, JSON.stringify(parsed.payload, null, 2));
  } else {
    safeSetText(stateView, `CRC invalid: ${parsed.reason || `${parsed.computed} != ${parsed.expected}`}`);
  }
}

function updateAppState(parsed, raw) {
  if (!parsed.valid) {
    return;
  }

  appState.setState({
    latestRawMessage: raw,
    latestPayload: parsed.payload,
    latestHotTubState: parsed.state,
  });
}

function initializeCharts() {
  if (!chartContainer) {
    return;
  }

  try {
    temperatureChartId = chartManager.createChart({
      id: 'temperature-chart',
      container: chartContainer,
      title: '',
      seriesLabels: ['waterTemp', 'filteredWaterTemp', 'heaterOn', 'pumpState'],
      maxPoints: 100,
    });
  } catch (err) {
    temperatureChartId = null;
    console.error('Chart initialization failed:', err);
  }
}

function ensureUPlotLoaded() {
  return new Promise((resolve) => {
    if (window.uPlot) {
      resolve(true);
      return;
    }

    const existing = document.querySelector('script[data-uplot-dynamic="1"]');
    if (existing) {
      existing.addEventListener('load', () => resolve(Boolean(window.uPlot)), { once: true });
      existing.addEventListener('error', () => resolve(false), { once: true });
      return;
    }

    const script = document.createElement('script');
    script.src = '/vendor/uPlot.iife.min.js';
    script.async = true;
    script.dataset.uplotDynamic = '1';
    script.addEventListener('load', () => resolve(Boolean(window.uPlot)), { once: true });
    script.addEventListener('error', () => resolve(false), { once: true });
    document.head.appendChild(script);
  });
}

function deriveStorageUrl(otaUrl, storageLabel) {
  try {
    const url = new URL(otaUrl);
    const storageFilename = storageLabel === 'storage_0' ? 'storage_0.bin' : 'storage_1.bin';
    const segments = url.pathname.split('/');
    segments[segments.length - 1] = storageFilename;
    url.pathname = segments.join('/');
    return url.toString();
  } catch (err) {
    console.error('Cannot derive storage URL from OTA URL:', err);
    return null;
  }
}


// Main Initialization
async function hardwareInit() {
  // setBadge('initializing', 'warn');
  sendBtn.disabled = true;
  safeSetText(otaStatus, 'idle');
  setOtaProgress(0);

  if (!window.uPlot) {
    const loaded = await ensureUPlotLoaded();
    if (!loaded) {
      console.error('uPlot script failed to load from /vendor/uPlot.iife.min.js');
    }
  }

  initializeCharts();

  if (heatToggle) {
    heatToggle.addEventListener('change', () => {
      const desiredState = heatToggle.checked;
      setHeatControlState(desiredState, true);
      sendHotTubCommand('hottub.heater.status.set', {
        'heaterOn': desiredState,
      });
    });
  }

  pumpModeInputs.forEach((input) => {
    input.addEventListener('change', () => {
      if (!input.checked) {
        return;
      }

      const desiredState = Number(input.value);
      setPumpControlState(desiredState, true);
      sendHotTubCommand('hottub.pump.state.set', {
        'pumpState': desiredState,
      });
    });
  });

  setHeatControlState(false, false);
  setPumpControlState(0, false);

  clearSystemStatusBtn.addEventListener('click', () => {
    safeSetText(systemStatusView, '');
    // console.log('Clearing system status...');
  }); 



  systemStatusBtn.addEventListener('click', () => {
    // console.log('Requesting system status...');

    const payload = {
      id: requestId = 1,
      type: 'req',
      cmd: 'system.status.get',
      params: '',
    };

    // console.log('Requesting system status:', payload);

    const socketClient = ws_manager && typeof ws_manager.send === 'function' ? ws_manager : null;
    if (!socketClient || socketClient.readyState !== WebSocket.OPEN) {
      safeSetText(statusView, 'Socket is not open. Waiting for connection...');
      return;
    }

    try {
      socketClient.send(createCrc32JsonWrapper(payload));
      safeSetText(statusView, JSON.stringify(payload));
      // console.log('Sending system status request:', payload);
    } catch (err) {
      safeSetText(statusView, `Invalid payload: ${err.message}`);
      console.error('Failed to send system status request:', err);
    }
  }); 
  
  
  sendBtn.addEventListener('click', () => {
    if (!ws_manager || ws_manager.readyState !== WebSocket.OPEN) {
      safeSetText(sendView, 'Socket is not open. Waiting for connection...');
      return;
    }

    try {
      const wrapped = createCrc32JsonWrapper(commandInput.value);
      ws_manager.send(wrapped);
      safeSetText(sendView, wrapped);
      // console.log('Sending:', wrapped);
    } catch (err) {
      safeSetText(sendView, `Invalid JSON: ${err.message}`);
      console.error('Failed to wrap JSON:', err);
    }
  });

  otaBtn.addEventListener('click', () => {
    if (!ws_manager || ws_manager.readyState !== WebSocket.OPEN) {
      safeSetText(sendView, 'Socket is not open. Waiting for connection...');
      return;
    }

    const url = prompt('Nino Enter OTA binary URL (GitHub raw/release asset URL):',
      'https://raw.githubusercontent.com/Nino67/Esp32-S3-HotTub-Base/main/firmware/hot_tub_controller.bin');
    if (!url) {
      return;
    }

    const storage_url = deriveStorageUrl(url, 'storage_1');

    const payload = {
      id: 1,
      type: 'req',
      cmd: 'ota.manager.update.github',
      params: { url, storage_url },
    };

    safeSetText(otaStatus, 'requested');
    setOtaProgress(0);

    try {
      const wrapped = createCrc32JsonWrapper(payload);
      ws_manager.send(wrapped);
      safeSetText(sendView, wrapped);
      console.log('Sending OTA update request:', wrapped);
    } catch (err) {
      safeSetText(sendView, `Invalid OTA payload: ${err.message}`);
      safeSetText(otaStatus, 'failed');
      console.error('Failed to wrap OTA payload:', err);
    }
  });

  if (otaManifestBtn) {
    otaManifestBtn.addEventListener('click', () => {
      if (!ws_manager || ws_manager.readyState !== WebSocket.OPEN) {
        safeSetText(sendView, 'Socket is not open. Waiting for connection...');
        return;
      }

      const manifestUrl = prompt('Enter OTA manifest URL:',
        'https://raw.githubusercontent.com/Nino67/Esp32-S3-HotTub-Base/main/firmware/ota_manifest.json');
      if (!manifestUrl) {
        return;
      }

      const payload = {
        id: 1,
        type: 'req',
        cmd: 'ota.manager.update.manifest',
        params: { manifest_url: manifestUrl },
      };

      safeSetText(otaStatus, 'requested');
      setOtaProgress(0);

      try {
        const wrapped = createCrc32JsonWrapper(payload);
        ws_manager.send(wrapped);
        safeSetText(sendView, wrapped);
        console.log('Sending OTA manifest request:', wrapped);
      } catch (err) {
        safeSetText(sendView, `Invalid OTA payload: ${err.message}`);
        safeSetText(otaStatus, 'failed');
        console.error('Failed to wrap OTA payload:', err);
      }
    });
  }

  ws_manager.connect();
  
} // end of hardwareInit()
//-----------------------------------------------------------------------------


function updateTemperatureChart(state) {
  if (!temperatureChartId || !state) {
    return;
  }

  const src = state.response && typeof state.response === 'object' && Object.keys(state.response).length > 0
    ? state.response
    : state;
  const parsedTimestampMs = src.lastUpdateTime ? Date.parse(src.lastUpdateTime) : NaN;
  const timestamp = Number.isFinite(parsedTimestampMs)
    ? parsedTimestampMs / 1000
    : Math.floor(Date.now() / 1000);

  const STATUS_BASELINE_TEMP = 25;

  const heaterOnValue = STATUS_BASELINE_TEMP + (src.heaterOn === true ? 2 : 0);
  const pumpStateValue = (() => {
    switch (src.pumpState) {
      case 1:
        return STATUS_BASELINE_TEMP + 5; // low pump state at 25°C relative baseline
      case 2:
        return STATUS_BASELINE_TEMP + 7; // high pump state at 27°C relative baseline
      default:
        return STATUS_BASELINE_TEMP; // off pump state at 25°C baseline
    }
  })();

  chartManager.addPoint(temperatureChartId, timestamp, {
    waterTemp: src.waterTemp,
    filteredWaterTemp: src.filteredWaterTemp,
    heaterOn: heaterOnValue,
    pumpState: pumpStateValue,
  });
}


hardwareInit();







// /**
//  * @brief Array of callback functions for the hot tub controller commands.
//  *
//  * Each entry in the array consists of a command string and its corresponding callback function.
//  * The array is terminated with a sentinel value (NULL, NULL).
//  */
// const hot_tub_callbacks = {
//     {"hottub.status.get", hottub_status_get_callback},
//     {"hottub.automode.get", hottub_auto_mode_get_callback},
//     {"hottub.automode.set", hottub_auto_mode_set_callback},
//     {"hottub.heater.status.get", hottub_heater_status_get_callback},
//     {"hottub.heater.status.set", hottub_heater_status_set_callback},
//     {"hottub.temperature.unit.get", hottub_temperature_unit_get_callback},
//     {"hottub.temperature.unit.set", hottub_temperature_unit_set_callback},
//     {"hottub.water.temperature.get", hottub_water_temperature_get_callback},
//     {"hottub.water.temperature.set", hottub_water_temperature_set_callback},
//     {"hottub.filtered.water.temp.get", hottub_filtered_water_temp_get_callback},
//     {"hottub.filtered.water.temp.set", hottub_filtered_water_temp_set_callback},
//     {"hottub.simulation.mode.get", hottub_simulation_mode_get_callback},
//     {"hottub.simulation.mode.set", hottub_simulation_mode_set_callback},
//     {"hottub.air.temperature.get", hottub_air_temperature_get_callback},
//     {"hottub.air.temperature.set", hottub_air_temperature_set_callback},
//     {"hottub.humidity.get", hottub_humidity_get_callback},
//     {"hottub.humidity.set", hottub_humidity_set_callback},
//     {"hottub.setpoint.temperature.get", hottub_setpoint_temperature_get_callback},
//     {"hottub.setpoint.temperature.set", hottub_setpoint_temperature_set_callback},
//     {"hottub.high.hysteresis.get", hottub_high_hysteresis_get_callback},
//     {"hottub.high.hysteresis.set", hottub_high_hysteresis_set_callback},
//     {"hottub.low.hysteresis.get", hottub_low_hysteresis_get_callback},
//     {"hottub.low.hysteresis.set", hottub_low_hysteresis_set_callback},
//     {"hottub.pump.state.get", hottub_pump_state_get_callback},
//     {"hottub.pump.state.set", hottub_pump_state_set_callback},
//     {"hottub.low.pass.filter.alpha.get", hottub_low_pass_filter_alpha_get_callback},
//     {"hottub.low.pass.filter.alpha.set", hottub_low_pass_filter_alpha_set_callback},
//     {"hottub.safety.switch.get", hottub_safety_switch_get_callback},
//     {"hottub.safety.switch.set", hottub_safety_switch_set_callback},
//     {"hottub.pump.pre.run.time.get", hottub_pump_pre_run_time_get_callback},
//     {"hottub.pump.pre.run.time.set", hottub_pump_pre_run_time_set_callback},
//     {"hottub.pump.post.run.time.get", hottub_pump_post_run_time_get_callback},
//     {"hottub.pump.post.run.time.set", hottub_pump_post_run_time_set_callback},
//     {"hottub.error.get", hottub_error_get_callback},
//     {"hottub.error.set", hottub_error_set_callback},
//     {NULL, NULL} // Sentinel value to mark the end of the array
// };
// //-----------------------------------------------------------------------------
