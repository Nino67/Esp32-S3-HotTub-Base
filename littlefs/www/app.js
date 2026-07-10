

import { createCrc32JsonWrapper, parseAndVerifyCrc32Wrapper } from '/crc32_wrapper.js';

const badge = document.getElementById('connBadge');
const stateView = document.getElementById('stateView');
const receiveView = document.getElementById('receiveView');
const sendView = document.getElementById('sendView');
const commandInput = document.getElementById('commandInput');
const otaStatus = document.getElementById('otaStatus');
const otaProgressBar = document.getElementById('otaProgressBar');
const sendBtn = document.getElementById('sendBtn');
const otaBtn = document.getElementById('otaBtn');

let socket;
let otaPollingInterval = null;

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
    if (socket && socket.readyState === WebSocket.OPEN) {
      // socket.send(JSON.stringify({ command: 'get_state' }));
    } else {
      stopOtaPolling();
    }
  }, 500);
}

function updateOtaState(state) {
  if (state.ota_status) {
    otaStatus.textContent = state.ota_status;
  } else {
    otaStatus.textContent = 'idle';
  }

  setOtaProgress(state.ota_progress ?? 0);

  if (state.ota_pending) {
    startOtaPolling();
  } else {
    stopOtaPolling();
  }
}

function hardwareInit() {
  setBadge('initializing', 'warn');
  sendBtn.disabled = true;
  otaStatus.textContent = 'idle';
  setOtaProgress(0);

  // add event listener for button 'sendBtn' to send command to the server
  sendBtn.addEventListener('click', () => {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      sendView.textContent = 'Socket is not open. Waiting for connection...';
      return;
    }

    try {
      const wrapped = createCrc32JsonWrapper(commandInput.value);
      socket.send(wrapped);
      sendView.textContent = wrapped;
      console.log('Sending:', wrapped);
    } catch (err) {
      sendView.textContent = `Invalid JSON: ${err.message}`;
      console.error('Failed to wrap JSON:', err);
    }
  });

  otaBtn.addEventListener('click', () => {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      sendView.textContent = 'Socket is not open. Waiting for connection...';
      return;
    }

    const url = prompt('Nino Enter OTA binary URL (GitHub raw/release asset URL):', 
      'https://raw.githubusercontent.com/Nino67/Esp32-S3-HotTub-Base/main/firmware/hot_tub_controller.bin');
    if (!url) {
      return;
    }

    const payload = {
      command: 'ota_update',
      url,
    };

    otaStatus.textContent = 'requested';
    setOtaProgress(0);

    try {
      const wrapped = createCrc32JsonWrapper(payload);
      socket.send(wrapped);
      sendView.textContent = wrapped;
      console.log('Sending OTA update request:', wrapped);
    } catch (err) {
      sendView.textContent = `Invalid OTA payload: ${err.message}`;
      otaStatus.textContent = 'failed';
      console.error('Failed to wrap OTA payload:', err);
    }
  });

  // connect to the server
  connect();

}  


function connect() {
  const proto = window.location.protocol === 'https:' ? 'wss' : 'ws';
  socket = new WebSocket(`${proto}://${window.location.host}/ws`);

  socket.addEventListener('open', () => {
    setBadge('connected', 'ok');
    sendBtn.disabled = false;
    sendView.textContent = 'Connected. Ready to send.';
    // socket.send(JSON.stringify({ command: 'get_state' }));
  });

  socket.addEventListener('close', () => {
    setBadge('reconnecting', 'warn');
    sendBtn.disabled = true;
    sendView.textContent = 'Connection closed. Reconnecting...';
    stopOtaPolling();
    window.setTimeout(connect, 1500);
  });

  socket.addEventListener('message', (event) => {
    const result = parseAndVerifyCrc32Wrapper(event.data);
    if (result.valid) {
      updateOtaState(result.payload);
      const text = JSON.stringify(result.payload, null, 2);
      stateView.textContent = text;
      receiveView.textContent = event.data;
      console.log('Received verified payload:', result.payload);
    } else {
      stateView.textContent = `CRC invalid: ${result.reason || `${result.computed} != ${result.expected}`}`;
      receiveView.textContent = event.data;
      console.warn('Invalid CRC32 payload:', event.data, result);
    }
  });

  socket.addEventListener('error', () => {
    setBadge('error', 'bad');
    sendBtn.disabled = true;
    sendView.textContent = 'WebSocket error. Check console.';
  });
}

hardwareInit();