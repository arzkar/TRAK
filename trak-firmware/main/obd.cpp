#include "obd.h"
#include <BluetoothSerial.h>

static BluetoothSerial _bt;
static bool            _obdAvailable = false;

// ─── Internal helpers ─────────────────────────────────────────────────────────

// Send a command, wait for '>' prompt, return trimmed response.
// Returns "NO DATA" on timeout.
static String sendAT(const char* cmd, uint16_t timeoutMs = OBD_RESPONSE_TIMEOUT) {
  _bt.println(cmd);
  unsigned long start = millis();
  String resp = "";
  while (millis() - start < timeoutMs) {
    while (_bt.available()) {
      char c = _bt.read();
      if (c == '>') {
        resp.trim();
        // Strip echo if present (ELM327 sometimes echoes the command)
        int nl = resp.lastIndexOf('\n');
        if (nl >= 0) resp = resp.substring(nl + 1);
        resp.trim();
        return resp;
      }
      if (c != '\r') resp += c;
    }
  }
  return "NO DATA";
}

// Honda CB350 BS6 ELM327 init sequence (ISO 14230-4 KWP2000)
static bool runInitSequence() {
  sendAT("ATZ",       1000);  // reset — longer timeout
  delay(500);
  sendAT("ATE0");             // echo off
  sendAT("ATL0");             // linefeeds off
  sendAT("ATS0");             // spaces off
  sendAT("ATSP5");            // ISO 14230-4 KWP2000 slow init
  sendAT("ATSH8210F1");       // Honda-specific header
  sendAT("ATAT2");            // adaptive timing level 2
  sendAT("ATST64");           // set timeout ~100ms

  // Probe for supported PIDs — this confirms ECU is responding
  String pids = sendAT("0100", 2000);
  if (pids == "NO DATA" || pids == "?" || pids == "BUS BUSY" ||
      pids.isEmpty() || pids.indexOf("UNABLE") >= 0) {
    Serial.println("[OBD] ECU did not respond to 0100");
    return false;
  }
  Serial.printf("[OBD] Supported PIDs: %s\n", pids.c_str());
  return true;
}

// ─── Public API ───────────────────────────────────────────────────────────────

bool initOBD() {
  // Bypassing Bluetooth to prevent Guru Meditation Error on bench testing
  Serial.println("[OBD] Bluetooth scan skipped (Hardware not present)");
  _obdAvailable = false;
  return false;
}

bool retryOBD() {
  if (_obdAvailable) return true;
  Serial.println("[OBD] Retrying ELM327 connection...");
  return initOBD();
}

// Fill an OBDRaw with "NO DATA" for all PIDs
static void fillNoData(OBDRaw& r, uint32_t sessionOffsetMs) {
  r.t = sessionOffsetMs;
  strlcpy(r.pid010C, "NO DATA", OBD_RESPONSE_BUF);
  strlcpy(r.pid010D, "NO DATA", OBD_RESPONSE_BUF);
  strlcpy(r.pid0111, "NO DATA", OBD_RESPONSE_BUF);
  strlcpy(r.pid0105, "NO DATA", OBD_RESPONSE_BUF);
  strlcpy(r.pid0142, "NO DATA", OBD_RESPONSE_BUF);
}

void pollOBD(OBDRaw& out, uint32_t sessionOffsetMs) {
  if (!_obdAvailable) {
    fillNoData(out, sessionOffsetMs);
    return;
  }

  out.t = sessionOffsetMs;

  auto query = [&](const char* pid, char* dest) {
    String resp = sendAT(pid, OBD_RESPONSE_TIMEOUT);
    // Keep error strings as-is so Hono can distinguish them
    strlcpy(dest, resp.c_str(), OBD_RESPONSE_BUF);
  };

  query("010C", out.pid010C);  // RPM
  query("010D", out.pid010D);  // Speed
  query("0111", out.pid0111);  // Throttle
  query("0105", out.pid0105);  // Coolant
  query("0142", out.pid0142);  // Battery voltage
}

bool obdAvailable() {
  return _obdAvailable;
}
