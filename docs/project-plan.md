# TRAK

### Telemetry, Real-time Analytics & Kinematics

**A connected motorcycle telemetry platform — Honda CB350**

---

## Overview

TRAK is a full-stack vehicle telemetry platform built around the Honda H'ness CB350. It combines onboard edge hardware (ESP32), OBD diagnostics (ELM327), inertial measurement (MPU6050), and offline-first data buffering (MicroSD) to collect, process, and stream real-time ride and vehicle data to a cloud backend for analytics.

The platform is designed to demonstrate production-scale IoT system thinking — reliable telemetry ingestion, offline buffering, CAN/OBD integration, and real-time observability — on a real vehicle, built from scratch.

---

## Architecture

```
Honda CB350 ECU
      │
      │ K-Line (ISO 14230 KWP2000)
      ▼
  ELM327 OBD dongle (6-pin → 16-pin adapter)
      │
      │ Bluetooth Classic SPP
      ▼
  ESP32 Edge Node
  ├── MPU6050 IMU         (I²C — GPIO 21/22)
  ├── MicroSD module      (SPI — GPIO 18/19/23/5)
  ├── MCP2515 CAN module  (SPI — GPIO 18/19/23/15) [V2]
  └── WiFi / LTE          (MQTT → backend)
      │
      │ MQTT over WiFi (V1) / 4G LTE (V2)
      ▼
  MQTT Broker (Mosquitto)
      │
      ▼
  Hono backend (Node.js / Bun)
      │
      ▼
  PostgreSQL + TimescaleDB
      │
      ▼
  Analytics dashboard
```

---

## Hardware

### V1 — Core Telemetry Platform

| Component                                  | Purpose                                    |
| ------------------------------------------ | ------------------------------------------ |
| ESP32 WROOM-32D 30-pin (USB-C, CH340C)     | Main microcontroller                       |
| MPU6050 GY-521                             | IMU — lean angle, G-force, crash detection |
| LM2596 buck converter with display         | 12V bike battery → 5V ESP32                |
| MicroSD card module                        | Offline data buffering and replay          |
| 830-point solderless breadboard            | Prototyping                                |
| Jumper wire set (M-M, M-F, F-F, 40pc each) | Connections                                |
| Mini ELM327 Bluetooth OBD2                 | ECU data over OBD                          |
| iovi Honda 6-pin to OBD2 adapter cable     | CB350 OBD port adapter                     |
| MicroSD card (8GB+)                        | Storage media                              |

**Bike connection hardware (buy locally)**

| Component                          | Purpose                         |
| ---------------------------------- | ------------------------------- |
| Fuse tap / add-a-fuse adapter      | Tap switched +12V from fuse box |
| Inline blade fuse holder + 2A fuse | Circuit protection              |

### V2 — CAN + Autonomous Telemetry Upgrade

| Component                        | Purpose                               |
| -------------------------------- | ------------------------------------- |
| MCP2515 CAN bus module (TJA1050) | Direct CAN bus access                 |
| LILYGO T-SIM7600E ESP32          | Replaces V1 ESP32 — adds 4G LTE + GPS |
| NEO-6M GPS module                | Route tracking, corner mapping        |

## Wiring

### Power

```
Bike fuse box (switched +12V, accessory fuse)
    │ fuse tap
    ├── +12V wire (with inline 2A fuse) → Buck converter IN+
    └── GND wire → Frame chassis bolt → Buck converter IN−

Buck converter OUT+ → ESP32 VIN
Buck converter OUT− → ESP32 GND
```

Set buck converter output to exactly 5.0V before connecting ESP32.

### MPU6050 → ESP32 (I²C)

| MPU6050 | ESP32             | Wire                  |
| ------- | ----------------- | --------------------- |
| VCC     | 3.3V              | Red                   |
| GND     | GND               | Black                 |
| SCL     | GPIO 22           | Yellow                |
| SDA     | GPIO 21           | Blue                  |
| AD0     | GND               | Black (I²C addr 0x68) |
| INT     | GPIO 4 (optional) | Purple                |

### MicroSD → ESP32 (SPI)

| SD Module | ESP32   | Wire   |
| --------- | ------- | ------ |
| VCC       | 3.3V    | Red    |
| GND       | GND     | Black  |
| MISO      | GPIO 19 | Blue   |
| MOSI      | GPIO 23 | Green  |
| SCK       | GPIO 18 | Yellow |
| CS        | GPIO 5  | Purple |

### ELM327 → ESP32

No wires. ELM327 connects to CB350 OBD port via iovi cable, communicates with ESP32 over Bluetooth Classic SPP.

### GPIO Allocation Summary

| GPIO | Function                 | Protocol |
| ---- | ------------------------ | -------- |
| 21   | MPU6050 SDA              | I²C      |
| 22   | MPU6050 SCL              | I²C      |
| 4    | MPU6050 INT (optional)   | Digital  |
| 18   | SD + MCP2515 SCK         | SPI      |
| 19   | SD + MCP2515 MISO        | SPI      |
| 23   | SD + MCP2515 MOSI        | SPI      |
| 5    | SD card CS               | SPI      |
| 15   | MCP2515 CS (V2 reserved) | SPI      |
| VIN  | 5V from buck converter   | Power    |
| 3.3V | MPU6050 + SD power       | Power    |
| GND  | Common ground            | Power    |

---

## Bike Connection Points

Only three things physically connect to the CB350:

1. **Fuse tap** — plugs into fuse box under seat (switched +12V, powers down with ignition)
2. **iovi OBD cable** — plugs into Honda's red 6-pin diagnostic port under seat
3. **Frame ground** — single bolt on chassis near fuse box

Everything is non-destructive and fully reversible. Unplug in under 60 seconds for service visits.

---

## Data Collected

### From ELM327 / OBD (ECU data)

| PID  | Data                   | Unit |
| ---- | ---------------------- | ---- |
| 010C | Engine RPM             | rpm  |
| 010D | Vehicle speed          | km/h |
| 0111 | Throttle position      | %    |
| 0105 | Coolant temperature    | °C   |
| 010F | Intake air temperature | °C   |
| 0142 | Control module voltage | V    |
| 0110 | MAF air flow rate      | g/s  |

### From MPU6050 (IMU / kinematics)

| Measurement          | Use                                     |
| -------------------- | --------------------------------------- |
| Lean angle (roll)    | Cornering analysis, max lean per corner |
| Pitch angle          | Braking dive, acceleration squat        |
| Yaw rate             | Turn initiation                         |
| Lateral G-force      | Cornering load                          |
| Longitudinal G-force | Braking / acceleration intensity        |
| Vertical G-force     | Road surface quality, bump detection    |
| Crash detection      | Spike across all axes + speed = 0       |

### Derived Metrics

- Power estimate (RPM × throttle)
- Braking events (speed drop rate + G-force)
- Riding style score (aggregate aggressiveness)
- Idle vs riding time
- Hard acceleration / hard braking events
- Engine load curve (throttle vs RPM scatter)

---

## Firmware (ESP32 — Arduino IDE)

### Libraries

- `MPU6050` by ElectronicCats
- `SD` (built-in Arduino)
- `BluetoothSerial` (built-in ESP32 core)
- `PubSubClient` (MQTT)
- `ArduinoJson`
- `Wire` (I²C, built-in)

### Core Loop Architecture

```
Core 0 (FreeRTOS Task)          Core 1 (FreeRTOS Task)
─────────────────────           ──────────────────────
OBD polling (100ms)             IMU sampling (10ms)
MQTT publish (30s batch)        SD card logging (500ms)
WiFi reconnect handler          Crash detection
```

### ELM327 Initialization (Honda CB350 BS6)

```cpp
BT.println("ATZ");        // reset
delay(1000);
BT.println("ATE0");       // echo off
BT.println("ATL0");       // linefeeds off
BT.println("ATSP5");      // force ISO 14230 KWP2000 slow init
BT.println("ATSH8210F1"); // Honda-specific header
BT.println("ATAT2");      // adaptive timing
BT.println("0100");       // query supported PIDs
```

### MQTT Batch Payload (every 30s)

```json
{
  "device_id": "trak-cb350-001",
  "session_id": "uuid",
  "firmware_version": "1.0.0",
  "batch_start": 1715123400000,
  "readings": [
    {
      "t": 0,
      "rpm": 1240,
      "spd": 0,
      "tps": 12,
      "coolant": 78,
      "lean": 1.2,
      "ax": 0.01,
      "ay": 0.02,
      "az": 9.8,
      "gx": 0.1,
      "gy": 0.0,
      "gz": 0.0
    }
  ]
}
```

### Offline Buffering Strategy

- While WiFi connected: publish MQTT batch every 30s
- While WiFi offline: write batch as JSON to SD card (`/sessions/YYYYMMDD/batch_N.json`)
- On reconnect: replay SD batches in order, delete after confirmed publish

---

## Backend

### Stack

- **Runtime:** Bun
- **Framework:** Hono
- **ORM:** Drizzle
- **Database:** PostgreSQL + TimescaleDB extension
- **Broker:** Mosquitto (Docker)
- **Validation:** Zod

### Project Structure

```
src/
  index.ts
  routes/
    telemetry.ts      ← MQTT subscriber + DB write
    sessions.ts       ← GET /sessions, /sessions/:id
    analytics.ts      ← analytics endpoints
  db/
    schema.ts
    client.ts
  mqtt/
    subscriber.ts     ← Mosquitto client
  middleware/
    auth.ts           ← device API key validation
```

### Database Schema

```sql
CREATE TABLE sessions (
  id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  device_id TEXT NOT NULL,
  started_at TIMESTAMPTZ,
  ended_at TIMESTAMPTZ,
  distance_km FLOAT,
  max_lean FLOAT,
  max_speed FLOAT
);

CREATE TABLE telemetry (
  id BIGSERIAL,
  device_id TEXT NOT NULL,
  session_id UUID REFERENCES sessions(id),
  timestamp TIMESTAMPTZ NOT NULL,
  rpm INT,
  speed_kmh FLOAT,
  throttle_pos FLOAT,
  coolant_temp FLOAT,
  lean_angle FLOAT,
  accel_x FLOAT,
  accel_y FLOAT,
  accel_z FLOAT,
  gyro_x FLOAT,
  gyro_y FLOAT,
  gyro_z FLOAT
);

-- TimescaleDB hypertable (1 line upgrade from standard Postgres)
SELECT create_hypertable('telemetry', 'timestamp');

CREATE INDEX idx_telemetry_session ON telemetry(session_id, timestamp DESC);
```

### Key Analytics Queries

```sql
-- Max lean angle per session
SELECT session_id, MAX(ABS(lean_angle)) AS max_lean
FROM telemetry GROUP BY session_id;

-- RPM band distribution
SELECT
  CASE
    WHEN rpm < 2000 THEN 'idle'
    WHEN rpm < 4000 THEN 'cruise'
    WHEN rpm < 6000 THEN 'pull'
    ELSE 'redline'
  END AS band,
  COUNT(*) * 0.1 AS seconds
FROM telemetry
WHERE session_id = $1
GROUP BY band;

-- Crash detection candidates
SELECT timestamp, accel_x, accel_y, accel_z,
  SQRT(accel_x^2 + accel_y^2 + accel_z^2) AS g_total
FROM telemetry
WHERE SQRT(accel_x^2 + accel_y^2 + accel_z^2) > 4.0
ORDER BY timestamp;

-- Hard braking events
SELECT timestamp, speed_kmh, accel_x
FROM telemetry
WHERE accel_x < -0.4
ORDER BY accel_x ASC;
```

---

## Build Phases

### Phase 1 — Bench validation (no bike)

- [ ] MPU6050 printing values to Serial Monitor
- [ ] SD card writing and reading test file
- [ ] ELM327 pairing over Bluetooth, returning `ATRV` (battery voltage)
- [ ] ELM327 returning RPM/speed from car or OBD simulator app
- [ ] MQTT publish to local Mosquitto broker
- [ ] Hono backend receiving and storing batch
- [ ] All three combined into single firmware

### Phase 2 — Bike integration

- [ ] iovi cable fits CB350 OBD port confirmed
- [ ] ELM327 powers on from OBD port (ignition on, engine off)
- [ ] Honda BS6 protocol handshake working (`ATSP5` + header)
- [ ] Live RPM reading with engine running
- [ ] Full sensor fusion (OBD + IMU) logging to SD
- [ ] MQTT batch uploading on WiFi connect

### Phase 3 — Hardening

- [ ] Transfer breadboard to perfboard, solder
- [ ] Mount in weatherproof project box under seat
- [ ] MPU6050 mounted rigid to frame rail (axis-aligned)
- [ ] Offline replay tested (ride with WiFi off, reconnect, verify data)
- [ ] Crash detection threshold tuned on real road vibration data

### Phase 4 — V2 upgrade

- [ ] Swap ESP32 for LILYGO T-SIM7600E
- [ ] Add NEO-6M GPS (route replay, corner mapping)
- [ ] MCP2515 CAN tap for direct internal ECU bus access
- [ ] Design custom PCB in KiCad
- [ ] Fabricate at JLCPCB

---

## Testing Without the Bike

| Test           | Method                                       |
| -------------- | -------------------------------------------- |
| MPU6050        | USB power, tilt by hand, read Serial Monitor |
| SD card        | USB power, write/read test file              |
| Buck converter | 12V DC adapter, verify 5V output             |
| ELM327 OBD     | OBD simulator app on phone, or test on a car |
| MQTT pipeline  | Local Mosquitto + Hono on laptop             |
| Full firmware  | All above combined on bench before bike      |

80% of V1 can be validated at a desk before the bike is touched.

---

## Portfolio Positioning

**One-line description:**

> TRAK is a real-time vehicle telemetry and kinematics platform — ESP32 edge node with OBD/ECU diagnostics, IMU motion sensing, offline-first MQTT ingestion, and time-series analytics on PostgreSQL.

**What it demonstrates:**

- Embedded systems (ESP32, FreeRTOS, SPI/I²C/UART)
- IoT architecture (edge → broker → backend → database)
- Automotive protocols (OBD-II, KWP2000, CAN)
- Backend engineering (Hono, Drizzle, Zod, Bun)
- Time-series data engineering (TimescaleDB, analytics queries)
- Real-world hardware integration on a production vehicle

---

_TRAK — Telemetry, Real-time Analytics & Kinematics_
_Honda H'ness CB350 · ESP32 · OBD · IMU · MQTT · PostgreSQL_
