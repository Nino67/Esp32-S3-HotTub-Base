/**
 * @file ws_manager.js
 * @author Gaetano (Nino) Ricca (gricca1967@gmail.com)
 * @brief  WebSocket Manager for the Controller web interface. 
 *
 * @details This file contains the WebSocket Manager for the Controller web interface.
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


import {callback_manager} from '/js/communication/callback_manager.js';
import { parseMessage } from '/js/communication/message_parser.js';
import { callbacks } from '/js/callbacks.js';
import { hottub } from '/js/globals.js';
import { renderParsedMessage } from '/js/app.js';

let client = null;
let requestId = 1;


function createWebSocketClient({
  url,
  onOpen,
  onClose,
  onError,
  onMessage,
  reconnectDelay = 1500,
  messageTimeoutMs = 60000,
  routes = null,
}) {
  let socket = null;
  let reconnectTimer = null;
  let staleTimer = null;
  let lastMessageAt = 0;
  let closedManually = false;

  // Register the callbacks with the callback manager
  callback_manager.registerHandlers(routes || callbacks);

  function stopStaleTimer() {
    if (staleTimer !== null) {
      clearInterval(staleTimer);
      staleTimer = null;
    }
  }

  function startStaleTimer() {
    stopStaleTimer();
    staleTimer = window.setInterval(() => {
      if (!socket || socket.readyState !== WebSocket.OPEN) {
        return;
      }

      const now = Date.now();
      if (lastMessageAt > 0 && (now - lastMessageAt) > messageTimeoutMs) {
        console.warn(`[ws_client] No messages for ${now - lastMessageAt}ms; reconnecting socket`);
        socket.close();
      }
    }, Math.max(1000, Math.floor(messageTimeoutMs / 3)));
  }



  /**
   * Connect to the WebSocket server.
   */
  function connect() {
    if (closedManually) {
      return;
    }

    socket = new WebSocket(url);

    socket.addEventListener('open', () => {
      lastMessageAt = Date.now();
      startStaleTimer();

      if (typeof onOpen === 'function') {
        try {
          onOpen();
        } catch (err) {
          console.error('WebSocket onOpen handler error:', err);
        }
      }
    });

    socket.addEventListener('message', async (event) => {
      if (!event.data) { return null; }

      lastMessageAt = Date.now();

      try 
      {
        const parsed = parseMessage(event.data);
        // console.log('[ws_client] Parsed WebSocket payload received:', parsed.payload);

        if (parsed.valid && parsed.payload) {
          const type = parsed.payload.type || parsed.payload.cmd_type;
          const command = parsed.payload.cmd || parsed.payload.command;
           
          // check if the message is a 'pub' type, which indicates a state update
          if (type === 'pub') {
            // console.log('[ws_client] Received state update:', parsed.payload.response);
            // Update the hottub state with new values
            const newState = parsed.payload.response;
            Object.keys(newState).forEach(key => {
              if (key in hottub) {
                hottub[key] = newState[key];
              } else {
                console.warn(`[updateHotTubState] Unknown property: ${key}`);
              }
            });
          }
          else {
            if (command) {
              await callback_manager.dispatch(command, parsed.payload);
            } else {
              console.warn('[ws_client] No cmd found in payload:', parsed.payload);
            }
          }  
        } 
        else 
        {
          console.warn('[ws_client] Invalid parsed message, skipping callback dispatch');
        }

        if (typeof onMessage === 'function') 
        {
          onMessage(parsed);
        }
      } 
      catch (err) 
      {
        console.error('Failed to process WebSocket message:', err);
        if (typeof onMessage === 'function') 
        {
          onMessage({ valid: false, reason: err.message });
        }
      }
    }); // end of message event listener


    socket.addEventListener('close', (event) => {
      stopStaleTimer();
      if (typeof onClose === 'function') {
        onClose(event);
      }
      if (!closedManually) {
        reconnectTimer = window.setTimeout(() => {
          reconnectTimer = null;
          connect();
        }, reconnectDelay);
      }
    });

    socket.addEventListener('error', (event) => {
      if (typeof onError === 'function') {
        onError(event);
      }
    });
  }

  function send(data) {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
      throw new Error('WebSocket is not open');
    }
    socket.send(data);
  }

  function close() {
    closedManually = true;
    stopStaleTimer();
    if (reconnectTimer !== null) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    if (socket && socket.readyState !== WebSocket.CLOSED) {
      socket.close();
    }
  }

  function getReadyState() {
    return socket ? socket.readyState : WebSocket.CLOSED;
  }

  connect();

  return {
    send,
    close,
    get readyState() {
      return getReadyState();
    },
  };
}



/*****************************************************************************/
/*****************************************************************************/
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
//   sendBtn.disabled = true;
//   if (heatToggle) {
//     heatToggle.disabled = true;
//   }
//   pumpModeInputs.forEach((input) => {
//     input.disabled = true;
//   });
//   safeSetText(sendView, 'Connection closed. Reconnecting...');
//   stopOtaPolling();
//   stopStatusPolling();
}

function handleSocketError() {
//   // setBadge('error', 'bad');
//   sendBtn.disabled = true;
//   if (heatToggle) {
//     heatToggle.disabled = true;
//   }
//   pumpModeInputs.forEach((input) => {
//     input.disabled = true;
//   });
//   safeSetText(sendView, 'WebSocket error. Check console.');
//   stopStatusPolling();
}

function handleSocketMessage(rawOrParsed) {
  const parsed = typeof rawOrParsed === 'string'
    ? parseMessage(rawOrParsed)
    : rawOrParsed;

  renderParsedMessage(parsed, typeof rawOrParsed === 'string' ? rawOrParsed : JSON.stringify(parsed.payload || parsed, null, 2));

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



export const ws_manager = {
  connect,
  close: () => client?.close?.(),
  send: (data) => client?.send?.(data),
  get readyState() {
    return client ? client.readyState : WebSocket.CLOSED;
  },
  handleSocketClose,
  handleSocketError,
  handleSocketMessage,
};

// export function getWsClient() {
//   return client;
// }

/*****************************************************************************/
/*****************************************************************************/
