import {callback_manager} from './callback_manager.js';
import { parseHotTubMessage } from '/js/message_parser.js';

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
  

export function createWebSocketClient({ url, onOpen, onClose, onError, onMessage, reconnectDelay = 1500, routes = {} }) {
  let socket = null;
  let reconnectTimer = null;
  let closedManually = false;

  function dispatch(handler, ...args) {
    console.log('Dispatching handler:', handler?.name || 'anonymous', 'with args:', args);
    if (typeof handler === 'function') {
      try {
        handler(...args);
      } catch (err) {
        console.error('WebSocket handler error:', err);
      }
    }
  }

  if (routes && typeof routes === 'object' && !Array.isArray(routes)) {
    callback_manager.registerHandlers(routes);
  }

  function connect() {
    if (closedManually) {
      return;
    }

    socket = new WebSocket(url);

    socket.addEventListener('open', () => {
      dispatch(onOpen);
    });

    socket.addEventListener('message', async (event) => {
      if (!event.data) { return null; }

      try {
        const parsed = parseHotTubMessage(event.data);
        if (parsed.valid && parsed.payload) {
          const command = parsed.payload.cmd || parsed.payload.command;
          if (command) {
            await callback_manager.dispatch(command, parsed.payload);
          }
        }
        dispatch(onMessage, parsed);
      } catch (err) {
        console.error('Failed to process WebSocket message:', err);
        dispatch(onMessage, { valid: false, reason: err.message });
      }
    });


    socket.addEventListener('close', (event) => {
      dispatch(onClose, event);
      if (!closedManually) {
        reconnectTimer = window.setTimeout(() => {
          reconnectTimer = null;
          connect();
        }, reconnectDelay);
      }
    });

    socket.addEventListener('error', (event) => {
      dispatch(onError, event);
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
