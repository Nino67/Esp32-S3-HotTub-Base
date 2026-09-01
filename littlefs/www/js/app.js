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
import { createChartManager } from '/js/chart_manager.js';
import { hottub } from '/js/globals.js';
import { safeSetText, safeSetStyle } from '/js/globals.js';
import { ws_manager } from '/js/communication/ws_manager.js';


// UI Elements
const stateView = document.getElementById('stateView');
const statusView = document.getElementById('systemStatusView');
const receiveView = document.getElementById('receiveView');
const receivePayloadToggle = document.getElementById('receivePayloadToggle');
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
const settingsToggle = document.getElementById('settingsToggle');
const settingsPanel = document.getElementById('settingsPanel');
const settingsFields = document.getElementById('settingsFields');
const tempUnitSelect = document.getElementById('tempUnitSelect');
const setpointTempInput = document.getElementById('setpointTempInput');
const lowPassFilterAlphaInput = document.getElementById('lowPassFilterAlphaInput');
const highHysteresisInput = document.getElementById('highHysteresisInput');
const lowHysteresisInput = document.getElementById('lowHysteresisInput');
const pumpPreRunTimeInput = document.getElementById('pumpPreRunTimeInput');
const pumpPostRunTimeInput = document.getElementById('pumpPostRunTimeInput');
const pumpModeInputs = Array.from(document.querySelectorAll('input[name="pumpMode"]'));
const pumpTrack = document.querySelector('.pump-track');
const RECEIVE_PAYLOAD_TOGGLE_KEY = 'hottub.receivePayloadVisible';



// Application State
let requestId = 1;
let settingsUnlocked = false;
let otaProgressTimer = null;
let otaProgressValue = 0;
let otaReloadScheduled = false;
let otaInProgress = false;
let otaLastStatus = 'idle';
let otaLastProgress = 0;
let otaLastError = false;
let otaSessionStartedAt = 0;
let showReceivedPayload = true;
const chartManager = createChartManager();
let temperatureChartId = null;

function loadReceivePayloadPreference() {
  try {
    const saved = window.localStorage.getItem(RECEIVE_PAYLOAD_TOGGLE_KEY);
    if (saved === '0') {
      return false;
    }
    if (saved === '1') {
      return true;
    }
  } catch (err) {
    console.warn('Failed to read receive payload preference:', err);
  }

  return Boolean(receivePayloadToggle ? receivePayloadToggle.checked : true);
}

function saveReceivePayloadPreference(isVisible) {
  try {
    window.localStorage.setItem(RECEIVE_PAYLOAD_TOGGLE_KEY, isVisible ? '1' : '0');
  } catch (err) {
    console.warn('Failed to save receive payload preference:', err);
  }
}



/**
 * @brief Main application loop running at 1Hz.
 */
setInterval(() => {
  // Update the UI elements based on the current hot tub state
  if (filteredTemperatureDisplay) {
      const tempUnit = hottub.tempUnitCelsius ? '°C' : '°F';
      safeSetText(filteredTemperatureDisplay, `${hottub.filteredWaterTemp.toFixed(1)} ${tempUnit}`);
  } else {
      console.warn('[ws_client] filteredTemperature element not found');
  }

  // Update the temperature chart with the latest hot tub state
  updateTemperatureChart(hottub);

  // Update the UI controls based on the latest hot tub state
  syncControlStateFromPayload(hottub);
}, 1000);
//-----------------------------------------------------------------------------




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


function setSettingsControlState(isOn, pending = false) {
  if (!settingsToggle) {
    return;
  }

  settingsToggle.checked = Boolean(isOn);
  settingsToggle.disabled = Boolean(pending);

  const row = settingsToggle.closest('.toggle-row');
  if (row) {
    row.classList.toggle('is-pending', Boolean(pending));
  }
}

function showSettingsPanel(show) {
  if (!settingsPanel) {
    return;
  }

  settingsPanel.hidden = !show;
  settingsPanel.setAttribute('aria-hidden', show ? 'false' : 'true');

  if (settingsToggle) {
    settingsToggle.checked = show;
  }

  if (!show) {
    settingsUnlocked = false;
  }
}


function populateSettingsFields(state = {}) {
  const src = state.response && typeof state.response === 'object' && Object.keys(state.response).length > 0
    ? state.response
    : state;

  if (tempUnitSelect) {
    tempUnitSelect.value = src.tempUnitCelsius ? 'celsius' : 'fahrenheit';
  }

  if (setpointTempInput) {
    setpointTempInput.value = typeof src.setpointTemp === 'number' ? String(src.setpointTemp) : '';
  }

  if (lowPassFilterAlphaInput) {
    lowPassFilterAlphaInput.value = typeof src.lowPassFilterAlpha === 'number' ? String(src.lowPassFilterAlpha) : '';
  }

  if (highHysteresisInput) {
    highHysteresisInput.value = typeof src.highHysteresis === 'number' ? String(src.highHysteresis) : '';
  }

  if (lowHysteresisInput) {
    lowHysteresisInput.value = typeof src.lowHysteresis === 'number' ? String(src.lowHysteresis) : '';
  }

  if (pumpPreRunTimeInput) {
    pumpPreRunTimeInput.value = typeof src.pumpPreRunTime === 'number' ? String(src.pumpPreRunTime) : '';
  }

  if (pumpPostRunTimeInput) {
    pumpPostRunTimeInput.value = typeof src.pumpPostRunTime === 'number' ? String(src.pumpPostRunTime) : '';
  }
}


function verifySettingsAccess() {
  const password = prompt('Enter settings password:');
  return password === 'roland1969';
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

function setOtaProgress(value) {
  const pct = Math.max(0, Math.min(100, Number(value) || 0));
  safeSetStyle(otaProgressBar, 'width', `${pct}%`);
  safeSetText(otaProgressBar, `${pct}%`);
}

function scheduleOtaReload(statusText, progressValue) {
  const normalizedStatus = String(statusText || '').toLowerCase();
  const isCompleteStatus = /success|complete|completed|done|reboot|pending_reboot/.test(normalizedStatus);
  const isCompleteProgress = Number(progressValue) >= 100;
  if (!isCompleteStatus || !isCompleteProgress || otaReloadScheduled) {
    return;
  }

  otaReloadScheduled = true;
  otaInProgress = false;
  safeSetText(otaStatus, `${statusText} - reloading page...`);
  window.setTimeout(() => {
    window.location.reload();
  }, 3500);
}

function stopOtaProgressFallback() {
  if (otaProgressTimer) {
    window.clearInterval(otaProgressTimer);
    otaProgressTimer = null;
  }
}

function startOtaProgressFallback() {
  stopOtaProgressFallback();
  otaInProgress = true;
  otaReloadScheduled = false;
  otaLastError = false;
  otaLastStatus = 'downloading';
  otaLastProgress = 0;
  otaSessionStartedAt = Date.now();
  otaProgressValue = 0;
  setOtaProgress(0);
  safeSetText(otaStatus, 'downloading');

  otaProgressTimer = window.setInterval(() => {
    if (otaProgressValue >= 95) {
      return;
    }

    const step = otaProgressValue < 40 ? 5 : otaProgressValue < 75 ? 2 : 1;
    otaProgressValue = Math.min(95, otaProgressValue + step);
    setOtaProgress(otaProgressValue);
  }, 1200);
}

function formatOtaState(value) {
  if (typeof value === 'number') {
    const map = {
      0: 'ready',
      1: 'downloading',
      2: 'verifying',
      3: 'flashing',
      4: 'failed',
      5: 'pending_reboot',
    };
    return map[value] || String(value);
  }

  if (typeof value === 'string' && value.length > 0) {
    return value;
  }

  return null;
}

function updateOtaUiFromParsed(parsed) {
  if (!parsed || !parsed.valid || !parsed.payload) {
    return;
  }

  const payload = parsed.payload;
  const response = payload.response && typeof payload.response === 'object' ? payload.response : {};
  const params = payload.params && typeof payload.params === 'object' ? payload.params : {};
  const firmware = response.firmware && typeof response.firmware === 'object' ? response.firmware : {};
  const cmd = String(payload.cmd || payload.command || '');

  const otaSpecificProgress =
    response.otaProgress ?? response.ota_progress ?? response.otaPercent ?? response.ota_percent ??
    payload.otaProgress ?? payload.ota_progress ?? payload.otaPercent ?? payload.ota_percent;

  const otaSpecificStatus =
    response.otaStatus ?? response.ota_status ?? response.otaState ?? response.ota_state ??
    payload.otaStatus ?? payload.ota_status ?? payload.otaState ?? payload.ota_state;

  const isOtaMessage = cmd.includes('ota');
  const fallbackProgress =
    response.progress ?? response.percentage ?? response.percent ??
    payload.progress ?? payload.percentage ?? payload.percent;
  const firmwareProgress =
    Number(firmware.ota_total_expected_bytes) > 0
      ? (Number(firmware.ota_bytes_written || 0) / Number(firmware.ota_total_expected_bytes)) * 100
      : undefined;
  const fallbackStatus =
    response.status ?? response.state ?? payload.status ?? payload.state ??
    formatOtaState(firmware.current_ota_state);

  const rawProgress = otaSpecificProgress ?? firmwareProgress ?? (isOtaMessage ? fallbackProgress : undefined);
  const rawStatus = otaSpecificStatus ?? (isOtaMessage ? fallbackStatus : undefined);
  let latestProgress = null;

  if (isOtaMessage) {
    otaInProgress = true;
  }

  if (typeof rawStatus !== 'undefined' && rawStatus !== null) {
    const statusText = String(rawStatus);
    otaLastStatus = statusText;
    safeSetText(otaStatus, statusText);

    const terminalStatus = /failed|error|success|complete|completed|done|reboot|pending_reboot/i.test(statusText);
    if (terminalStatus) {
      stopOtaProgressFallback();
      if (/success|complete|completed|done|reboot|pending_reboot/i.test(statusText)) {
        setOtaProgress(100);
        otaLastProgress = 100;
        latestProgress = 100;
        scheduleOtaReload(statusText, 100);
      } else if (/failed|error/i.test(statusText)) {
        otaInProgress = false;
        otaLastError = true;
      }
    }
  }

  if (typeof rawProgress !== 'undefined' && rawProgress !== null) {
    let progress = rawProgress;
    if (typeof progress === 'string') {
      progress = progress.replace('%', '').trim();
    }

    const value = Number(progress);
    if (Number.isFinite(value)) {
      const normalized = value > 0 && value <= 1 ? value * 100 : value;
      stopOtaProgressFallback();
      setOtaProgress(normalized);
      otaLastProgress = normalized;
      latestProgress = normalized;
      if (otaInProgress && normalized >= 100) {
        scheduleOtaReload(otaLastStatus || 'completed', normalized);
      }
    }
  }

  if (typeof rawStatus !== 'undefined' && rawStatus !== null && latestProgress !== null) {
    scheduleOtaReload(rawStatus, latestProgress);
  }

  // Keep send view useful for OTA command payload context.
  if (isOtaMessage && Object.keys(params).length > 0) {
    safeSetText(sendView, JSON.stringify(payload));
  }
}

window.addEventListener('hottub-ws-close', () => {
  if (otaReloadScheduled || !otaInProgress) {
    return;
  }

  if (otaLastError) {
    otaInProgress = false;
    return;
  }

  const status = String(otaLastStatus || '').toLowerCase();
  const recentOtaSession = otaSessionStartedAt > 0 && (Date.now() - otaSessionStartedAt) < (20 * 60 * 1000);
  const likelyRebootTransition = /download_complete|flashing|complete|completed|success|done|reboot|pending_reboot|downloading/.test(status);

  if (recentOtaSession && likelyRebootTransition) {
    otaReloadScheduled = true;
    safeSetText(otaStatus, `${otaLastStatus || 'ota'} - reconnecting...`);
    window.setTimeout(() => {
      window.location.reload();
    }, 3000);
  }
});

export function renderParsedMessage(parsed, raw) {
  if (showReceivedPayload) {
    safeSetText(receiveView, raw);
  } else {
    safeSetText(receiveView, '');
  }

  if (parsed.valid) {
    safeSetText(stateView, JSON.stringify(parsed.payload, null, 2));
  } else {
    safeSetText(stateView, `CRC invalid: ${parsed.reason || `${parsed.computed} != ${parsed.expected}`}`);
  }

  updateOtaUiFromParsed(parsed);
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
  sendBtn.disabled = true;
  safeSetText(otaStatus, 'idle');
  setOtaProgress(0);

  showReceivedPayload = loadReceivePayloadPreference();

  if (receivePayloadToggle) {
    receivePayloadToggle.checked = showReceivedPayload;
    if (!showReceivedPayload) {
      safeSetText(receiveView, '');
    }

    receivePayloadToggle.addEventListener('change', () => {
      showReceivedPayload = Boolean(receivePayloadToggle.checked);
      saveReceivePayloadPreference(showReceivedPayload);
      if (!showReceivedPayload) {
        safeSetText(receiveView, '');
      }
    });
  }

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

  if (settingsToggle) {
    settingsToggle.addEventListener('change', () => {
      if (settingsToggle.checked) {
        if (!settingsUnlocked) {
          const authorized = verifySettingsAccess();
          if (!authorized) {
            settingsToggle.checked = false;
            showSettingsPanel(false);
            safeSetText(sendView, 'Settings access denied: invalid password.');
            return;
          }
          settingsUnlocked = true;
          populateSettingsFields(hottub);
        }

        showSettingsPanel(true);
        return;
      }

      showSettingsPanel(false);
      setSettingsControlState(false, false);
    });
  }

  if (tempUnitSelect) {
    tempUnitSelect.addEventListener('change', () => {
      sendHotTubCommand('hottub.temperature.unit.set', {
        'temp.unit.celsius.set': tempUnitSelect.value === 'celsius',
      });
    });
  }

  if (setpointTempInput) {
    setpointTempInput.addEventListener('change', () => {
      const value = parseFloat(setpointTempInput.value);
      if (Number.isFinite(value)) {
        sendHotTubCommand('hottub.setpoint.temperature.set', {
          'setpoint.temperature.set': value,
        });
      }
    });
  }

  if (lowPassFilterAlphaInput) {
    lowPassFilterAlphaInput.addEventListener('change', () => {
      const value = parseFloat(lowPassFilterAlphaInput.value);
      if (Number.isFinite(value)) {
        sendHotTubCommand('hottub.low.pass.filter.alpha.set', {
          'low.pass.filter.alpha.set': value,
        });
      }
    });
  }

  if (highHysteresisInput) {
    highHysteresisInput.addEventListener('change', () => {
      const value = parseFloat(highHysteresisInput.value);
      if (Number.isFinite(value)) {
        sendHotTubCommand('hottub.high.hysteresis.set', {
          'high.hysteresis.set': value,
        });
      }
    });
  }

  if (lowHysteresisInput) {
    lowHysteresisInput.addEventListener('change', () => {
      const value = parseFloat(lowHysteresisInput.value);
      if (Number.isFinite(value)) {
        sendHotTubCommand('hottub.low.hysteresis.set', {
          'low.hysteresis.set': value,
        });
      }
    });
  }

  if (pumpPreRunTimeInput) {
    pumpPreRunTimeInput.addEventListener('change', () => {
      const value = parseFloat(pumpPreRunTimeInput.value);
      if (Number.isFinite(value)) {
        sendHotTubCommand('hottub.pump.pre.run.time.set', {
          'pump.pre.run.time.set': value,
        });
      }
    });
  }

  if (pumpPostRunTimeInput) {
    pumpPostRunTimeInput.addEventListener('change', () => {
      const value = parseFloat(pumpPostRunTimeInput.value);
      if (Number.isFinite(value)) {
        sendHotTubCommand('hottub.pump.post.run.time.set', {
          'pump.post.run.time.set': value,
        });
      }
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
  setSettingsControlState(false, false);
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
    startOtaProgressFallback();

    try {
      const wrapped = createCrc32JsonWrapper(payload);
      ws_manager.send(wrapped);
      safeSetText(sendView, wrapped);
      console.log('Sending OTA update request:', wrapped);
    } catch (err) {
      safeSetText(sendView, `Invalid OTA payload: ${err.message}`);
      safeSetText(otaStatus, 'failed');
      stopOtaProgressFallback();
      setOtaProgress(0);
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
      startOtaProgressFallback();

      try {
        const wrapped = createCrc32JsonWrapper(payload);
        ws_manager.send(wrapped);
        safeSetText(sendView, wrapped);
        console.log('Sending OTA manifest request:', wrapped);
      } catch (err) {
        safeSetText(sendView, `Invalid OTA payload: ${err.message}`);
        safeSetText(otaStatus, 'failed');
        stopOtaProgressFallback();
        setOtaProgress(0);
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
