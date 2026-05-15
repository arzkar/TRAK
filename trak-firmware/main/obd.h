#pragma once
#include <Arduino.h>
#include "config.h"

// ─── Raw OBD reading struct ───────────────────────────────────────────────────
// All fields are raw hex strings exactly as returned by ELM327.
// Error values: "NO DATA", "?", "BUS BUSY" — all handled by Hono.
struct OBDRaw {
  uint32_t t;                          // ms offset from session start
  char pid010C[OBD_RESPONSE_BUF];     // RPM
  char pid010D[OBD_RESPONSE_BUF];     // Speed
  char pid0111[OBD_RESPONSE_BUF];     // Throttle position
  char pid0105[OBD_RESPONSE_BUF];     // Coolant temp
  char pid0142[OBD_RESPONSE_BUF];     // Battery voltage
};

// Connect to ELM327 over Bluetooth and run Honda CB350 BS6 init sequence.
// Returns true if connected and init succeeded.
bool initOBD();

// Poll all PIDs once. Fills `out` with raw hex strings.
// If OBD is unavailable, fills all fields with "NO DATA".
void pollOBD(OBDRaw& out, uint32_t sessionOffsetMs);

// Attempt reconnect — call periodically if obdAvailable() returns false.
bool retryOBD();

bool obdAvailable();
