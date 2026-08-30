/**
 * @file ws_api.js
 * @author Gaetano (Nino) Ricca (gricca1967@gmail.com)
 * @brief  WebSocket API for the Hot Tub Controller web interface. 
 *
 * @details This file contains the WebSocket API for the Hot Tub Controller web interface.
 *
 * @note Matching hardware:
 * - model: ESP32-S3-DevKitC-1.         SKU: ESP32-S3-DevKitC-1-N16R8
 * - mfg: RS Engineering.               date: 2026-08-28
 *
 * @version 0.1
 * @date 2026-08-28
 *
 * @copyright Copyright (c) 2026
 *
 */


import { createCrc32JsonWrapper } from '/js/communication/crc32_wrapper.js';
import { createWebSocketClient } from '/js/communication/ws_client.js';
import { parseMessage } from '/js/communication/message_parser.js';
import { callbacks } from '/js/callbacks.js';

import { client } from '/js/communication/ws_client.js';
// let client = null;



// WebSocket Event Handlers

function handleSocketOpen() {
  // setBadge('connected', 'ok');
//   sendBtn.disabled = false;
//   if (heatToggle) {
//     heatToggle.disabled = false;
//   }
//   pumpModeInputs.forEach((input) => {
//     input.disabled = false;
//   });
//   safeSetText(sendView, 'Connected. Ready to send.');
  // startStatusPolling();
}

function handleSocketClose() {
  // setBadge('reconnecting', 'warn');
  sendBtn.disabled = true;
  if (heatToggle) {
    heatToggle.disabled = true;
  }
  pumpModeInputs.forEach((input) => {
    input.disabled = true;
  });
  safeSetText(sendView, 'Connection closed. Reconnecting...');
  stopOtaPolling();
  stopStatusPolling();
}

function handleSocketError() {
  // setBadge('error', 'bad');
  sendBtn.disabled = true;
  if (heatToggle) {
    heatToggle.disabled = true;
  }
  pumpModeInputs.forEach((input) => {
    input.disabled = true;
  });
  safeSetText(sendView, 'WebSocket error. Check console.');
  stopStatusPolling();
}

function handleSocketMessage(rawOrParsed) {
  const parsed = typeof rawOrParsed === 'string'
    ? parseMessage(rawOrParsed)
    : rawOrParsed;

//   renderParsedMessage(parsed, typeof rawOrParsed === 'string' ? rawOrParsed : JSON.stringify(parsed.payload || parsed, null, 2));

  if (!(parsed && parsed.valid)) {
    console.warn('Invalid CRC32 payload:', rawOrParsed, parsed);
    return;
  }

  // console.log('[app] Parsed WebSocket payload received:', parsed.payload);
}


function connect() {
  const proto = window.location.protocol === 'https:' ? 'wss' : 'ws';
  client = createWebSocketClient({
    url: `${proto}://${window.location.host}/ws`,
    onOpen: handleSocketOpen,
    onClose: handleSocketClose,
    onError: handleSocketError,
    onMessage: handleSocketMessage,
    routes: callbacks,
  });
}



export const ws_api = {
    connect,
    handleSocketOpen,
    handleSocketClose,
    handleSocketError,
    handleSocketMessage
}
