/**
 * @file app_state.js
 * @author Gaetano (Nino) Ricca (gricca1967@gmail.com)
 * @brief   Simple application state container with subscription support.
 *
 * @details This file contains a simple application state container with subscription support.
 * It provides functions to create an application state, get and set state values, and subscribe to state changes.
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
