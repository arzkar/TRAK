# TRAK — LLM Directive Plan
# Paste this at the start of every coding session

---

## What is TRAK

TRAK (Telemetry, Real-time Analytics & Kinematics) is a connected motorcycle telemetry platform built on a Honda H'ness CB350 BS6. It collects real-time ride and vehicle data using an ESP32 edge node, streams it over MQTT to a Hono backend, and stores it in PostgreSQL with TimescaleDB for analytics.

The project is split into two parts:
- **Firmware** — C++ on ESP32 (Arduino IDE)
- **Backend** — TypeScript, Hono framework, Bun runtime, Drizzle ORM, PostgreSQL + TimescaleDB, Mosquitto MQTT broker

---

## Hardware (finalized, do not suggest alternatives)

| Component | Detail |
|---|---|
| Microcontroller | ESP32-WROOM-32D 30-pin, CH340C, USB-C |
| IMU | MPU6050 GY-521 — I²C on GPIO 21 (SDA), 22 (SCL) |
| OBD interface | ELM327 Bluetooth Classic SPP |
| OBD adapter | iovi Honda 6-pin to OBD2 16-pin cable |
| Storage | MicroSD module — SPI on GPIO 18/19/23, CS GPIO 5 |
| Power | LM2596 buck converter — 12V bike battery → 5V ESP32 |
| CAN (V2) | MCP2515 + TJA1050 — SPI, CS GPIO 15 |
| LTE + GPS (V2) | LILYGO T-SIM7600E replaces ESP32 in V2 |

Do not suggest different microcontrollers, different OBD adapters, different IMUs, or different power solutions. These are purchased and finalized.

---

## Bike (finalized)

- Honda H'ness CB350 BS6 (Euro 5)
- OBD protocol: ISO 14230-4 KWP2000 K-Line (NOT standard CAN on the diagnostic port)
- ELM327 init sequence for CB350:
  ```
  ATZ → ATE0 → ATL0 → ATS0 → ATSP5 → ATSH8210F1 → ATAT2 → ATST64 → 0100
  ```
- OBD port: red 6-pin diagnostic connector under seat
- Power tap: fuse box under seat, switched accessory fuse via fuse tap

---

## Architecture Decision — Option B (finalized, do not change)

**ESP32 does NOT parse anything.**

ESP32 responsibilities:
- Send AT commands to ELM327, collect raw hex response strings
- Read raw ax/ay/az/gx/gy/gz floats from MPU6050
- Read built-in ESP32 sensors (chip_temp, free_heap, wifi_rssi, uptime_ms)
- Compute g_total from raw IMU for crash detection only (exception)
- Package raw data into JSON batch
- Write to SD card
- Publish to MQTT

Hono responsibilities:
- Store raw MQTT payload in raw_telemetry table immediately
- Parse OBD hex strings into real values (RPM, speed, throttle, coolant, voltage)
- Compute IMU derived values (lean angle, pitch, g_total)
- Insert parsed values into telemetry table
- Support reprocessing — reparse raw_telemetry if parser logic changes

**Never suggest moving parsing back to ESP32. This decision is final.**

---

## MQTT Payload Format (ESP32 sends this)

```json
{
  "v": 1,
  "device_id": "trak-cb350-001",
  "session_id": "uuid",
  "firmware": "1.0.0",
  "batch_index": 42,
  "batch_start": 1717234222000,
  "batch_duration_ms": 30000,
  "reading_count": 300,
  "device": {
    "chip_temp": 42.5,
    "free_heap": 180432,
    "uptime_ms": 3600000,
    "wifi_rssi": -62,
    "cpu_freq_mhz": 240
  },
  "readings": [
    {
      "t": 0,
      "obd": {
        "010C": "410C1AF8",
        "010D": "410D3C",
        "0111": "41110A",
        "0105": "410578",
        "0142": "41420BB8"
      },
      "imu": {
        "ax": 0.01,
        "ay": -0.21,
        "az": 9.78,
        "gx": 0.1,
        "gy": 0.0,
        "gz": 0.0
      }
    }
  ]
}
```

OBD error values ESP32 may send: `"NO DATA"`, `"?"`, `"BUS BUSY"`, `null`. Hono must handle all of these and insert NULL into Postgres.

---

## OBD PID Parsing Formulas (run in Hono)

| PID | Field | Formula |
|---|---|---|
| 010C | rpm | `((A*256)+B)/4` |
| 010D | speed_kmh | `A` |
| 0111 | throttle % | `A*100/255` |
| 0105 | coolant °C | `A-40` |
| 0142 | vbat V | `((A*256)+B)/1000` |

Where A and B are the 3rd and 4th bytes of the response hex string (strip first 4 chars = response header).

---

## IMU Computed Values (run in Hono)

```typescript
lean   = atan2(ay, az) * (180 / PI)      // degrees, + right, - left
pitch  = atan2(-ax, az) * (180 / PI)     // degrees, + nose up, - nose down
gTotal = sqrt(ax² + ay² + az²)           // g magnitude
```

---

## Database Schema (finalized)

Five tables:

**devices** — registered ESP32 devices
**sessions** — one row per ride session
**raw_telemetry** — entire raw MQTT payload as JSONB, written first, never modified
**telemetry** — parsed + computed values, TimescaleDB hypertable partitioned by month
**events** — discrete events (crash, session_start, session_end, calibration)
**device_status** — latest heartbeat per device (upserted)

Key constraint: `raw_telemetry` has UNIQUE(device_id, session_id, batch_index) for idempotency.
Key index: `telemetry` is a TimescaleDB hypertable on `ts` column.

---

## Backend Stack (finalized, do not suggest alternatives)

| Layer | Choice |
|---|---|
| Runtime | Bun |
| Framework | Hono |
| ORM | Drizzle |
| Database | PostgreSQL 16 + TimescaleDB |
| MQTT broker | Mosquitto (Docker) |
| Validation | Zod |
| MQTT client | mqtt (npm) |

Do not suggest Express, Fastify, Prisma, NestJS, TypeORM, or any other alternatives. These are finalized.

---

## Firmware Stack (finalized)

| Layer | Choice |
|---|---|
| IDE | Arduino IDE 2.x |
| Language | C++ (Arduino framework) |
| Board | ESP32 Dev Module |
| Upload speed | 115200 baud |
| Key libraries | MPU6050 by ElectronicCats, PubSubClient, ArduinoJson 6.x, BluetoothSerial, SD, Wire, SPI |

---

## Firmware File Structure

```
trak-firmware/
  main.ino
  config.h         ← all constants and pin definitions
  obd.h / obd.cpp  ← ELM327 BT connection, AT commands, raw hex collection
  imu.h / imu.cpp  ← MPU6050 init, calibration, raw reading, crash detection
  storage.h / storage.cpp  ← SD card, batch files, offline replay
  mqtt.h / mqtt.cpp        ← WiFi, MQTT publish, SD replay on reconnect
  session.h / session.cpp  ← session lifecycle, UUID, metadata
  batch.h / batch.cpp      ← batch building, JSON serialization
```

---

## Backend File Structure

```
trak-backend/
  src/
    index.ts
    routes/
      sessions.ts
      analytics.ts
      devices.ts
    db/
      schema.ts
      client.ts
    mqtt/
      subscriber.ts
    parsers/
      obd-parser.ts    ← OBD hex → real values
      imu-parser.ts    ← raw IMU → lean, pitch, gTotal
    middleware/
      auth.ts
    handlers/
      telemetry.ts     ← ingest batch, store raw, parse, insert
      crash.ts
      status.ts
```

---

## GPIO Allocation (finalized)

| GPIO | Function |
|---|---|
| 4 | MPU6050 INT (optional) |
| 5 | SD card CS |
| 15 | MCP2515 CS (V2 reserved) |
| 18 | SPI SCK (SD + MCP2515) |
| 19 | SPI MISO (SD + MCP2515) |
| 21 | I²C SDA (MPU6050) |
| 22 | I²C SCL (MPU6050) |
| 23 | SPI MOSI (SD + MCP2515) |

---

## MQTT Topics

| Topic | Direction | Description |
|---|---|---|
| `trak/{device_id}/telemetry` | ESP32 → broker | 30s batch |
| `trak/{device_id}/crash` | ESP32 → broker | crash event |
| `trak/{device_id}/status` | ESP32 → broker | 60s heartbeat |
| `trak/{device_id}/command` | broker → ESP32 | remote commands |

---

## Build Phases

### Current phase: V1 bench validation
- [ ] MPU6050 printing raw values to Serial Monitor
- [ ] SD card writing test batch file
- [ ] ESP32 built-in sensors (chip_temp, free_heap, rssi) publishing to MQTT
- [ ] Hono receiving and storing to Postgres
- [ ] ELM327 pairing and returning ATRV
- [ ] Full OBD + IMU firmware combined

### Next: Bike integration
- [ ] Honda BS6 protocol handshake working
- [ ] Live OBD data flowing through full pipeline
- [ ] Real ride data in Postgres

### V2 (future)
- [ ] MCP2515 direct CAN tap
- [ ] LILYGO T-SIM7600E swap (4G LTE + GPS)
- [ ] Custom PCB via KiCad + JLCPCB

---

## Rules for This Session

1. Always write C++ for firmware, TypeScript for backend — never mix them up
2. Never suggest parsing OBD on ESP32 — Option B is final
3. Never suggest alternatives to the finalized hardware or software stack
4. When writing firmware, assume upload speed is 115200 baud
5. When writing Hono routes, use Zod for all request validation
6. Always store raw payload in raw_telemetry before any processing
7. OBD error strings ("NO DATA", "?", "BUS BUSY") must always be handled — never assume clean responses
8. IMU calibration offsets must be loaded from SD card on boot, not hardcoded
9. All MQTT operations must handle reconnection gracefully
10. SD card writes must be non-blocking — use FreeRTOS queues, never block the IMU task

---

## What to ask me if unclear

- Which build phase are we working on right now?
- Is this for firmware or backend?
- Should I test this on the bench or does it need the bike?

---