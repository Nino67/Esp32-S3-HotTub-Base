import { createCrc32JsonWrapper } from '/js/crc32_wrapper.js';
import { createWebSocketClient } from '/js/ws_client.js';
import { parseHotTubMessage } from '/js/message_parser.js';
import { createAppState } from '/js/app_state.js';
import { createChartManager } from '/js/chart_manager.js';

const badge = document.getElementById('connBadge');
const stateView = document.getElementById('stateView');
const receiveView = document.getElementById('receiveView');
const sendView = document.getElementById('sendView');
const commandInput = document.getElementById('commandInput');
const otaStatus = document.getElementById('otaStatus');
const otaProgressBar = document.getElementById('otaProgressBar');
const sendBtn = document.getElementById('sendBtn');
const otaBtn = document.getElementById('otaBtn');
const chartContainer = document.getElementById('chartContainer');

let client = null;
let otaPollingInterval = null;
let statusPollingInterval = null;
let requestId = 100;
const appState = createAppState({ latestRawMessage: null, latestPayload: null, latestHotTubState: null });
const chartManager = createChartManager();
let temperatureChartId = null;

function setBadge(text, status) {
  badge.textContent = text;
  badge.dataset.status = status;
}

function setOtaProgress(value) {
  const pct = Math.max(0, Math.min(100, Number(value) || 0));
  otaProgressBar.style.width = `${pct}%`;
  otaProgressBar.textContent = `${pct}%`;
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
  if (!client || client.readyState !== WebSocket.OPEN) {
    return;
  }

  const payload = {
    id: requestId += 1,
    type: 'req',
    cmd: 'system.status.get',
    params: '',
  };

  try {
    client.send(createCrc32JsonWrapper(payload));
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
    if (client && client.readyState === WebSocket.OPEN) {
      // reserved for future polling commands
    } else {
      stopOtaPolling();
    }
  }, 500);
}

function updateOtaState(state) {
  otaStatus.textContent = state.ota_status || 'idle';
  setOtaProgress(state.ota_progress ?? 0);

  if (state.ota_pending) {
    startOtaPolling();
  } else {
    stopOtaPolling();
  }
}

function renderParsedMessage(parsed, raw) {
  receiveView.textContent = raw;
  if (parsed.valid) {
    stateView.textContent = JSON.stringify(parsed.payload, null, 2);
  } else {
    stateView.textContent = `CRC invalid: ${parsed.reason || `${parsed.computed} != ${parsed.expected}`}`;
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
      seriesLabels: ['waterTemp', 'filteredWaterTemp'],
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

async function hardwareInit() {
  setBadge('initializing', 'warn');
  sendBtn.disabled = true;
  otaStatus.textContent = 'idle';
  setOtaProgress(0);

  if (!window.uPlot) {
    const loaded = await ensureUPlotLoaded();
    if (!loaded) {
      console.error('uPlot script failed to load from /vendor/uPlot.iife.min.js');
    }
  }

  initializeCharts();

  sendBtn.addEventListener('click', () => {
    if (!client || client.readyState !== WebSocket.OPEN) {
      sendView.textContent = 'Socket is not open. Waiting for connection...';
      return;
    }

    try {
      const wrapped = createCrc32JsonWrapper(commandInput.value);
      client.send(wrapped);
      sendView.textContent = wrapped;
      console.log('Sending:', wrapped);
    } catch (err) {
      sendView.textContent = `Invalid JSON: ${err.message}`;
      console.error('Failed to wrap JSON:', err);
    }
  });

  otaBtn.addEventListener('click', () => {
    if (!client || client.readyState !== WebSocket.OPEN) {
      sendView.textContent = 'Socket is not open. Waiting for connection...';
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

    otaStatus.textContent = 'requested';
    setOtaProgress(0);

    try {
      const wrapped = createCrc32JsonWrapper(payload);
      client.send(wrapped);
      sendView.textContent = wrapped;
      console.log('Sending OTA update request:', wrapped);
    } catch (err) {
      sendView.textContent = `Invalid OTA payload: ${err.message}`;
      otaStatus.textContent = 'failed';
      console.error('Failed to wrap OTA payload:', err);
    }
  });

  connect();
}

function handleSocketOpen() {
  setBadge('connected', 'ok');
  sendBtn.disabled = false;
  sendView.textContent = 'Connected. Ready to send.';
  // startStatusPolling();
}

function handleSocketClose() {
  setBadge('reconnecting', 'warn');
  sendBtn.disabled = true;
  sendView.textContent = 'Connection closed. Reconnecting...';
  stopOtaPolling();
  stopStatusPolling();
}

function handleSocketError() {
  setBadge('error', 'bad');
  sendBtn.disabled = true;
  sendView.textContent = 'WebSocket error. Check console.';
  stopStatusPolling();
}

function handleSocketMessage(raw) {
  const parsed = parseHotTubMessage(raw);
  renderParsedMessage(parsed, raw);

  if (parsed.valid) {
    updateOtaState(parsed.payload);
    updateAppState(parsed, raw);
    updateTemperatureChart(parsed.state);
    console.log('Received verified payload:', parsed.payload);
  } else {
    console.warn('Invalid CRC32 payload:', raw, parsed);
  }
}

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

  chartManager.addPoint(temperatureChartId, timestamp, {
    waterTemp: src.waterTemp,
    filteredWaterTemp: src.filteredWaterTemp,
    // airTemp: src.airTemp,
  });
}

function connect() {
  const proto = window.location.protocol === 'https:' ? 'wss' : 'ws';
  client = createWebSocketClient({
    url: `${proto}://${window.location.host}/ws`,
    onOpen: handleSocketOpen,
    onClose: handleSocketClose,
    onError: handleSocketError,
    onMessage: handleSocketMessage,
  });
}

hardwareInit();
