// All OBD parsing happens here — never in ESP32 firmware (Option B).
//
// ELM327 raw response format: "410C1AF8"
//   Bytes: [41][0C][1A][F8]
//          mode PID  A   B
//   Strip first 4 hex chars (2 bytes = mode + PID echo), then read A, B etc.

const OBD_ERRORS = new Set(['NO DATA', '?', 'BUS BUSY', 'UNABLE TO CONNECT', 'ERROR'])

/**
 * Parse a raw ELM327 hex response into individual data bytes.
 * Returns null on error values, null responses, or malformed strings.
 */
function parseHex(raw: string | null | undefined, byteCount: number): number[] | null {
  if (raw == null) return null
  const trimmed = raw.trim()
  if (OBD_ERRORS.has(trimmed)) return null

  // Strip spaces and response header (first 4 hex chars = mode byte + PID echo)
  const clean = trimmed.replace(/\s/g, '').slice(4)
  if (clean.length < byteCount * 2) return null

  const bytes: number[] = []
  for (let i = 0; i < byteCount * 2; i += 2) {
    const b = parseInt(clean.slice(i, i + 2), 16)
    if (isNaN(b)) return null
    bytes.push(b)
  }
  return bytes
}

/** PID 010C — Engine RPM: ((A×256)+B)/4  →  rpm */
export function parseRPM(raw: string | null | undefined): number | null {
  const b = parseHex(raw, 2)
  if (!b) return null
  return ((b[0] * 256) + b[1]) / 4
}

/** PID 010D — Vehicle speed: A  →  km/h */
export function parseSpeed(raw: string | null | undefined): number | null {
  const b = parseHex(raw, 1)
  return b ? b[0] : null
}

/** PID 0111 — Throttle position: A×100/255  →  % */
export function parseThrottle(raw: string | null | undefined): number | null {
  const b = parseHex(raw, 1)
  return b ? (b[0] * 100) / 255 : null
}

/** PID 0105 — Coolant temperature: A−40  →  °C */
export function parseCoolant(raw: string | null | undefined): number | null {
  const b = parseHex(raw, 1)
  return b ? b[0] - 40 : null
}

/** PID 0142 — Control module voltage: ((A×256)+B)/1000  →  V */
export function parseVoltage(raw: string | null | undefined): number | null {
  const b = parseHex(raw, 2)
  return b ? ((b[0] * 256) + b[1]) / 1000 : null
}
