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
    stateView.textContent = JSON.stringify(parsed.state || parsed.payload, null, 2);
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

  temperatureChartId = chartManager.createChart({
    id: 'temperature-chart',
    container: chartContainer,
    title: '',
    seriesLabels: ['waterTemp', 'filteredWaterTemp'],
    maxPoints: 100,
  });
}

function hardwareInit() {
  setBadge('initializing', 'warn');
  sendBtn.disabled = true;
  otaStatus.textContent = 'idle';
  setOtaProgress(0);
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

    const payload = {
      id: 1,
      type: 'req',
      cmd: 'ota.manager.update.github',
      params: { url },
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
}

function handleSocketClose() {
  setBadge('reconnecting', 'warn');
  sendBtn.disabled = true;
  sendView.textContent = 'Connection closed. Reconnecting...';
  stopOtaPolling();
}

function handleSocketError() {
  setBadge('error', 'bad');
  sendBtn.disabled = true;
  sendView.textContent = 'WebSocket error. Check console.';
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

  const src = state.response && typeof state.response === 'object' ? state.response : state;
  const parsedTimestampMs = src.lastUpdateTime ? Date.parse(src.lastUpdateTime) : NaN;
  const timestamp = Number.isFinite(parsedTimestampMs)
    ? parsedTimestampMs / 1000
    : Math.floor(Date.now() / 1000);

  chartManager.addPoint(temperatureChartId, timestamp, {
    waterTemp: src.waterTemp,
    filteredWaterTemp: src.filteredWaterTemp,
    // airTemp: src.airTemp,
  });``
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
