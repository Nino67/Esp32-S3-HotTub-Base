/**
 * @file crc32_wrapper.js 
 * @author Gaetano (Nino) Ricca (gricca1967@gmail.com)
 * @brief   CRC32 wrapper for JSON objects
 *
 * @details This file contains the functions to create and verify CRC32-wrapped
 * JSON objects. It provides functions to compute CRC32 checksums, normalize
 * JSON objects, and create CRC32-wrapped JSON strings.
 *
 * @note Matching hardware:
 * - model: ESP32-S3-DevKitC-1.         SKU: ESP32-S3-DevKitC-1-N8R8
 * - mfg: RS Engineering.               date: 2026-06-22

 * @version 0.1
 * @date 2026-06-22
 *
 * @note Matching hardware:
 * - model: ESP32-S3-DevKitC-1.         SKU: ESP32-S3-DevKitC-1-N8R8
 * - mfg: RS Engineering.               date: 2026-06-22

 * @version 0.1
 * @date 2026-06-22
 *
 * @copyright Copyright (c) 2026
 *
 */

/**
 * CRC32 implementation in JavaScript.
 * This implementation is based on the polynomial 0xEDB88320.
 * It uses a precomputed table for efficiency.
 */
const CRC32_TABLE = new Uint32Array(256);
for (let i = 0; i < 256; i += 1) {
  let crc = i;
  for (let j = 0; j < 8; j += 1) {
    crc = (crc & 1) ? 0xedb88320 ^ (crc >>> 1) : crc >>> 1;
  }
  CRC32_TABLE[i] = crc >>> 0;
} // End of CRC32_TABLE initialization
//------------------------------------------------------------------


/**
 * Computes the CRC32 checksum of a given Uint8Array.
 * @param {Uint8Array} data - The input data to compute the checksum for.
 * @param {number} [initial=0] - The initial CRC value (default is 0).
 * @returns {number} - The computed CRC32 checksum.
 */
function crc32(data, initial = 0) {
  if (!(data instanceof Uint8Array)) {
    throw new TypeError('crc32 expects a Uint8Array');
  }
  let crc = initial ^ 0xffffffff;
  for (let i = 0; i < data.length; i += 1) {
    crc = CRC32_TABLE[(crc ^ data[i]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
} // End of crc32 function
//----------------------------------------------------------------


/**
 * Normalizes a JSON object or JSON string.
 * @param {Object|string} json - The JSON object or JSON string to normalize.
 * @returns {Object} - The normalized JSON object.
 * @throws {TypeError} - If the input is not a valid JSON object or JSON string.
 */
function normalizeJsonObject(json) {
  let obj = json;
  if (typeof obj === 'string') {
    obj = JSON.parse(obj);
  }
  if (obj === null || typeof obj !== 'object' || Array.isArray(obj)) {
    throw new TypeError('Input must be a JSON object or JSON object string');
  }
  return obj;
}// End of normalizeJsonObject function
//------------------------------------------------------------------


/**
 * Creates a CRC32-wrapped JSON string from a given JSON object or string.
 * The returned format is: "<crc32>:<json>".
 * @param {Object|string} json - The JSON object or JSON string to wrap.
 * @returns {string} - The CRC32-wrapped JSON string.
 */
export function createCrc32JsonWrapper(json) {
  const obj = normalizeJsonObject(json);
  const jsonString = JSON.stringify(obj);
  if (!jsonString || !jsonString.startsWith('{') || !jsonString.endsWith('}')) {
    throw new Error('Serialized JSON did not produce an object');
  }

  const bytes = new TextEncoder().encode(jsonString);
  const checksum = crc32(bytes);
  return `${checksum}:${jsonString}`;
} // End of createCrc32JsonWrapper function
//------------------------------------------------------------------


/**
 * Parses and verifies a CRC32-wrapped JSON string.
 * The expected format is: "<crc32>:<json>".
 * @param {string} raw - The CRC32-wrapped JSON string to parse and verify.
 * @returns {Object} - An object containing the validity, payload, expected, and computed CRC32 values.
 */
export function parseAndVerifyCrc32Wrapper(raw) {
  if (typeof raw !== 'string') {
    throw new TypeError('Input must be a raw JSON string');
  }

  const trimmed = raw.trim();
  const separatorIndex = trimmed.indexOf(':');
  if (separatorIndex <= 0) {
    return { valid: false, reason: 'invalid wrapper format' };
  }

  const expectedText = trimmed.slice(0, separatorIndex);
  const jsonText = trimmed.slice(separatorIndex + 1).trim();
  if (!/^[0-9]+$/.test(expectedText) || jsonText.length === 0) {
    return { valid: false, reason: 'invalid wrapper format' };
  }

  const expected = Number(expectedText);
  const computed = crc32(new TextEncoder().encode(jsonText));
  if (computed !== expected) {
    return { valid: false, expected, computed };
  }

  try {
    const payload = JSON.parse(jsonText);
    return { valid: true, payload, expected, computed };
  } catch (err) {
    return { valid: false, reason: 'invalid JSON payload' };
  }
}// End of parseAndVerifyCrc32Wrapper function
//------------------------------------------------------------------
