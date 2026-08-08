import { parseAndVerifyCrc32Wrapper } from '/js/crc32_wrapper.js';

/**
 * Parse a CRC32-wrapped hot tub message and normalize it into a reusable state object.
 * @param {string} raw - Raw message text from the WebSocket.
 * @returns {Object} - A parse result with validity, payload, state, and optional reason.
 */
export function parseHotTubMessage(raw) {
  if (typeof raw !== 'string') {
    return { valid: false, reason: 'message must be a string' };
  }

  const result = parseAndVerifyCrc32Wrapper(raw);
  if (!result.valid) {
    return result;
  }

  const payload = result.payload;
  if (payload === null || typeof payload !== 'object' || Array.isArray(payload)) {
    return { valid: false, reason: 'payload must be an object', expected: result.expected, computed: result.computed };
  }

  const state = buildHotTubState(payload);
  return {
    valid: true,
    payload,
    state,
    expected: result.expected,
    computed: result.computed,
  };
}

/**
 * Build a normalized hot tub state object from a payload envelope.
 * @param {Object} payload - Verified message payload.
 * @returns {Object} - Normalized state object.
 */
export function buildHotTubState(payload) {
  const response = payload.response && typeof payload.response === 'object' && !Array.isArray(payload.response)
    ? payload.response
    : payload;

  return {
    id: payload.id ?? null,
    type: payload.type ?? null,
    cmd: payload.cmd ?? null,
    params: payload.params ?? null,
    status: payload.status ?? null,
    response,
  };
}
