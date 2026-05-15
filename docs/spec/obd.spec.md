# obd.spec.md
# TRAK — OBD / ELM327 Specification

---

## Protocol

The Honda CB350 BS6 uses **ISO 14230-4 KWP2000** (K-Line) on its 6-pin diagnostic port. This is NOT standard OBD-II CAN. The ELM327 must be forced to this protocol explicitly — auto-detection (`ATSP0`) often fails on Honda's implementation.

| Property | Value |
|---|---|
| Physical layer | K-Line (single wire, ISO 9141) |
| Protocol | ISO 14230-4 KWP2000 |
| ELM327 protocol code | SP5 (slow init) or SP4 (fast init) — try SP5 first |
| Honda header | 8210F1 |
| Baud rate | 10.4kbaud |
| Max PIDs/second | ~10 (Bluetooth SPP latency is the bottleneck) |

---

## ELM327 Initialization Sequence

Send commands in this exact order. Wait for "OK" response after each before proceeding.

```
ATZ           // Reset ELM327 — wait 1000ms after sending
ATE0          // Echo off — reduces response parsing complexity
ATL0          // Linefeeds off
ATS0          // Spaces off — compact responses, easier to parse
ATSP5         // Force ISO 14230-4 KWP slow init (Honda CB350 BS6)
ATSH8210F1    // Set Honda-specific CAN/KWP header
ATAT2         // Adaptive timing mode 2 — handles Honda's variable response times
ATST64        // Set timeout to 100ms (0x64 = 100 in decimal)
0100          // Query supported PIDs (Mode 01, PID 00) — confirm ECU responds
```

If `0100` returns no data or `NO DATA`:
1. Try `ATSP4` (fast init) instead of `ATSP5`
2. Try without `ATSH8210F1` header
3. Try `ATSP0` (auto) as last resort

---

## PID Reference — Honda CB350 BS6

All PIDs use OBD-II Mode 01 (show current data).

| PID | Name | Formula | Unit | Expected range |
|---|---|---|---|---|
| 010C | Engine RPM | `((A*256)+B)/4` | RPM | 800–8000 |
| 010D | Vehicle speed | `A` | km/h | 0–150 |
| 0111 | Throttle position absolute | `A*100/255` | % | 0–100 |
| 0105 | Coolant temperature | `A-40` | °C | 60–110 |
| 010F | Intake air temperature | `A-40` | °C | 15–60 |
| 0142 | Control module voltage | `((A*256)+B)/1000` | V | 11.5–14.5 |
| 0110 | MAF air flow rate | `((A*256)+B)/100` | g/s | 0–30 |
| 0145 | Relative throttle position | `A*100/255` | % | 0–100 |
| 0104 | Engine load | `A*100/255` | % | 0–100 |
| 0133 | Barometric pressure | `A` | kPa | 95–105 |

**Check supported PIDs first:**
Send `0100` → response bitmask tells you which PIDs are actually supported by the CB350 ECU. Not all of the above are guaranteed to respond.

---

## Response Parsing

ELM327 returns raw hex bytes as ASCII text. Example RPM response:

```
Sent:     010C
Received: 41 0C 1A F8

Decode:
  41 = positive response to mode 01
  0C = PID
  1A = byte A (hex) = 26 (decimal)
  F8 = byte B (hex) = 248 (decimal)
  RPM = ((26 * 256) + 248) / 4 = 1726 RPM
```

### Arduino parsing function

```cpp
int parseRPM(String response) {
  // response: "41 0C 1A F8"
  response.trim();
  response.replace(" ", "");  // "410C1AF8"
  if (response.length() < 8) return -1;
  int A = strtol(response.substring(4, 6).c_str(), NULL, 16);
  int B = strtol(response.substring(6, 8).c_str(), NULL, 16);
  return ((A * 256) + B) / 4;
}

int parseSpeed(String response) {
  // response: "41 0D 3C"
  response.trim();
  response.replace(" ", "");
  if (response.length() < 6) return -1;
  return strtol(response.substring(4, 6).c_str(), NULL, 16);
}

float parseCoolantTemp(String response) {
  response.trim();
  response.replace(" ", "");
  if (response.length() < 6) return -1;
  int A = strtol(response.substring(4, 6).c_str(), NULL, 16);
  return A - 40.0f;
}

float parseVoltage(String response) {
  // response: "41 42 0B B8"
  response.trim();
  response.replace(" ", "");
  if (response.length() < 8) return -1;
  int A = strtol(response.substring(4, 6).c_str(), NULL, 16);
  int B = strtol(response.substring(6, 8).c_str(), NULL, 16);
  return ((A * 256) + B) / 1000.0f;
}
```

---

## Polling Loop

```cpp
void obdTask(void* param) {
  while (true) {
    OBDReading r;
    r.timestamp = millis();
    r.rpm       = queryPID("010C", parseRPM);
    r.speed     = queryPID("010D", parseSpeed);
    r.throttle  = queryPID("0111", parseThrottle);
    r.coolant   = queryPID("0105", parseCoolantTemp);
    r.voltage   = queryPID("0142", parseVoltage);

    xQueueSend(obd_queue, &r, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(100));  // 10Hz polling
  }
}

String queryPID(String pid, int timeout_ms = 200) {
  BT.println(pid);
  unsigned long start = millis();
  String response = "";
  while (millis() - start < timeout_ms) {
    while (BT.available()) {
      char c = BT.read();
      if (c == '>') return response;  // ELM327 prompt = end of response
      response += c;
    }
  }
  return "";  // timeout
}
```

---

## Error Conditions

| Condition | ELM327 Response | Action |
|---|---|---|
| ECU not responding | `NO DATA` | Skip reading, increment error counter |
| Protocol mismatch | `?` or empty | Reinitialize with different ATSP value |
| BT connection lost | BT.available() returns false | Reconnect, reinitialize |
| Timeout | Empty string after timeout_ms | Skip reading |
| Bus busy | `BUS BUSY` | Wait 500ms, retry |
| Init failure | No "OK" response | Retry full init sequence |

---

## Latency Characteristics

| Operation | Typical latency |
|---|---|
| Single PID request + response | 80–150ms over BT SPP |
| Full 5-PID poll cycle | ~500–750ms |
| Effective OBD update rate | ~2Hz for all 5 PIDs |
| Effective RPM-only rate | ~10Hz (single PID) |

Latency is dominated by Bluetooth SPP round-trip, not ELM327 processing. Wired CAN (V2 with MCP2515) eliminates this entirely.

---

## Battery Voltage via OBD

Quick health check — available without engine running (ignition on only):

```
ATRV    // Returns battery voltage directly, e.g. "12.4V"
```

Use this as a connectivity test on the bench before attempting PID queries.

---

## Warranty Note

OBD access is read-only. No ECU parameters are written or modified. This is identical to what a Honda service tool does during routine diagnostics. Remove ELM327 and iovi cable before service visits.
