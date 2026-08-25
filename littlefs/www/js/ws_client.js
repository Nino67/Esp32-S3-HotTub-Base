import {callback_manager} from './callback_manager.js';
import { parseMessage } from '/js/message_parser.js';
import { callbacks } from '/js/callbacks.js';
// import { updateHotTubState, hottub, safeSetText } from '/js/globals.js';
import { hottub, safeSetText } from '/js/globals.js';


/**
 * @file ws_client.js
 * @author Gaetano (Nino) Ricca (gricca1967@gmail.com)
 * @brief   Application logic for the Hot Tub Controller web socket interface.
 *
 * @details This file contains the application logic for the Hot Tub 
 * Controller web socket interface. 
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
  

export function createWebSocketClient({
  url,
  onOpen,
  onClose,
  onError,
  onMessage,
  reconnectDelay = 1500,
  messageTimeoutMs = 60000,
}) {
  let socket = null;
  let reconnectTimer = null;
  let staleTimer = null;
  let lastMessageAt = 0;
  let closedManually = false;

  callback_manager.registerHandlers(callbacks);

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

        if (parsed.valid && parsed.payload) {
          const type = parsed.payload.type || parsed.payload.cmd_type;
          const command = parsed.payload.cmd || parsed.payload.command;
           
          // check if the message is a 'pub' type, which indicates a state update
          if (type === 'pub') {
            // Update the hottub state with new values
            const newState = parsed.payload.response;
            Object.keys(newState).forEach(key => {
              if (key in hottub) {
                hottub[key] = newState[key];
              } else {
                console.warn(`[updateHotTubState] Unknown property: ${key}`);
              }
            });

            // // Update the UI elements based on the new state    
            // const filteredTemperatureDisplay = document.getElementById('filteredTemperature');
            // if (filteredTemperatureDisplay) {
            //     const tempUnit = hottub.tempUnitCelsius ? '°C' : '°F';
            //     safeSetText(filteredTemperatureDisplay, `${parsed.payload.response.filteredWaterTemp.toFixed(1)} ${tempUnit}`);
            // } else {
            //     console.warn('[ws_client] filteredTemperature element not found');
            // }   
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
