/**
 * @file ws_client.js
 * @brief Simple reconnecting WebSocket client wrapper.
 */

export function createWebSocketClient({ url, onOpen, onClose, onError, onMessage, reconnectDelay = 1500 }) {
  let socket = null;
  let reconnectTimer = null;
  let closedManually = false;

  function dispatch(handler, ...args) {
    if (typeof handler === 'function') {
      try {
        handler(...args);
      } catch (err) {
        console.error('WebSocket handler error:', err);
      }
    }
  }

  function connect() {
    if (closedManually) {
      return;
    }

    socket = new WebSocket(url);

    socket.addEventListener('open', () => {
      dispatch(onOpen);
    });

    socket.addEventListener('message', (event) => {
      dispatch(onMessage, event.data);
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
