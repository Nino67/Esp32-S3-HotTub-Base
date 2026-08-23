import {callback_manager} from './callback_manager.js';
import { parseMessage } from '/js/message_parser.js';
// import { hot_tub_callbacks } from '/js/hottub_callbacks.js';
import { callbacks } from '/js/callbacks.js';

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
  

export function createWebSocketClient({ url, onOpen, onClose, onError, onMessage, reconnectDelay = 1500 }) {
  let socket = null;
  let reconnectTimer = null;
  let closedManually = false;

  // console.log('[ws_client] Registering routes:', Object.keys(callbacks));
  callback_manager.registerHandlers(callbacks);

  function connect() {
    if (closedManually) {
      return;
    }

    socket = new WebSocket(url);

    socket.addEventListener('open', () => {
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

      try 
      {
        const parsed = parseMessage(event.data);
        // console.log('[ws_client] Received WS message', parsed);

        if (parsed.valid && parsed.payload) 
        {
          const command = parsed.payload.cmd || parsed.payload.command;
          // console.log('[ws_client] Parsed command:', command);
          if (command) 
          {
            await callback_manager.dispatch(command, parsed.payload);
          } else {
            console.warn('[ws_client] No cmd found in payload:', parsed.payload);
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
    });


    socket.addEventListener('close', (event) => {
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
