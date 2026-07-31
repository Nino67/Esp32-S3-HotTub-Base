/**
 * @file app_state.js
 * @brief Simple application state container with subscription support.
 */

export function createAppState(initialState = {}) {
  let state = { ...initialState };
  const listeners = new Set();

  function notify() {
    for (const listener of listeners) {
      try {
        listener(state);
      } catch (err) {
        console.error('AppState listener error:', err);
      }
    }
  }

  return {
    getState() {
      return state;
    },
    setState(nextState) {
      state = { ...state, ...nextState };
      notify();
    },
    replaceState(nextState) {
      state = { ...nextState };
      notify();
    },
    subscribe(listener) {
      if (typeof listener !== 'function') {
        throw new TypeError('AppState.subscribe requires a function');
      }
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
  };
}
