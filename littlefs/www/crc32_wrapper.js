const JSON_CRC32 = 'crc32';

const CRC32_TABLE = new Uint32Array(256);
for (let i = 0; i < 256; i += 1) {
  let crc = i;
  for (let j = 0; j < 8; j += 1) {
    crc = (crc & 1) ? 0xedb88320 ^ (crc >>> 1) : crc >>> 1;
  }
  CRC32_TABLE[i] = crc >>> 0;
}

function crc32(data, initial = 0) {
  if (!(data instanceof Uint8Array)) {
    throw new TypeError('crc32 expects a Uint8Array');
  }
  let crc = initial ^ 0xffffffff;
  for (let i = 0; i < data.length; i += 1) {
    crc = CRC32_TABLE[(crc ^ data[i]) & 0xff] ^ (crc >>> 8);
  }
  return (crc ^ 0xffffffff) >>> 0;
}

function normalizeJsonObject(json) {
  let obj = json;
  if (typeof obj === 'string') {
    obj = JSON.parse(obj);
  }
  if (obj === null || typeof obj !== 'object' || Array.isArray(obj)) {
    throw new TypeError('Input must be a JSON object or JSON object string');
  }
  return obj;
}

export function createCrc32JsonWrapper(json) {
  const obj = normalizeJsonObject(json);
  const wrapper = {};
  for (const key of Object.keys(obj)) {
    if (key === JSON_CRC32) {
      continue;
    }
    wrapper[key] = obj[key];
  }

  const base = JSON.stringify(wrapper);
  if (!base.endsWith('}')) {
    throw new Error('Serialized JSON did not produce an object');
  }

  const prefixNoClose = base.slice(0, -1);
  const bytes = new TextEncoder().encode(prefixNoClose);
  const checksum = crc32(bytes);
  return `${prefixNoClose},"${JSON_CRC32}":${checksum}}`;
}

export function parseAndVerifyCrc32Wrapper(raw) {
  if (typeof raw !== 'string') {
    throw new TypeError('Input must be a raw JSON string');
  }
  const trimmed = raw.trim();
  const match = trimmed.match(/^(.*),"crc32"\s*:\s*(\d+)}\s*$/);
  if (!match) {
    return { valid: false, reason: 'invalid wrapper format' };
  }

  const prefixNoClose = match[1];
  const expected = Number(match[2]);
  const computed = crc32(new TextEncoder().encode(prefixNoClose));
  if (computed !== expected) {
    return { valid: false, expected, computed };
  }

  try {
    const payload = JSON.parse(`${prefixNoClose}}`);
    return { valid: true, payload, expected, computed };
  } catch (err) {
    return { valid: false, reason: 'invalid JSON payload' };
  }
}
