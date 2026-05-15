# telemetry.spec.md
# TRAK — Telemetry & MQTT Specification
# Updated: Option B — Raw ingestion, parse on backend

---

## Transport

| Property | Value |
|---|---|
| Protocol | MQTT v3.1.1 |
| Broker | Mosquitto (self-hosted, Docker) |
| Port | 1883 (plain), 8883 (TLS — production) |
| QoS level | 1 (at least once delivery) |
| Retain | false |
| Keep-alive | 60 seconds |
| Client ID | `trak-{device_id}-{random_4_hex}` |
| Auth | Username + password (production), none (dev) |

---

## Topic Structure

```
trak/
  {device_id}/
    telemetry       ← batch telemetry publish (ESP32 → broker)
    crash           ← crash event publish (ESP32 → broker)
    status          ← heartbeat / device status (ESP32 → broker)
    command         ← commands from backend (broker → ESP32)
```

---

## Design Decision — Option B

ESP32 does zero parsing. It sends:
- Raw OBD hex strings exactly as received from ELM327
- Raw IMU floats exactly as read from MPU6050 registers
- ESP32 built-in sensor values (chip temp, heap, RSSI, uptime)

All parsing, unit conversion, and derived value computation happens in Hono.
Raw payload is stored permanently in `raw_telemetry` table before any processing.
This enables reprocessing historical data if parsing logic changes.

---

## Telemetry Batch Payload

Published to: `trak/{device_id}/telemetry`
Frequency: every 30 seconds
QoS: 1

```json
{
  "v": 1,
  "device_id": "trak-cb350-001",
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
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

---

## Field Reference

### Batch envelope

| Field | Type | Description |
|---|---|---|
| v | int | Payload schema version |
| device_id | string | Unique device identifier |
| session_id | UUID string | Ride session UUID |
| firmware | string | Firmware semver |
| batch_index | int | Monotonically increasing counter |
| batch_start | int64 ms epoch | Absolute timestamp of first reading |
| batch_duration_ms | int | Actual duration of batch window |
| reading_count | int | Number of readings in array |

### Device block (once per batch)

| Field | Type | Unit | Description |
|---|---|---|---|
| chip_temp | float | °C | ESP32 internal chip temperature |
| free_heap | int | bytes | Available SRAM |
| uptime_ms | int | ms | Time since last boot |
| wifi_rssi | int | dBm | WiFi signal strength |
| cpu_freq_mhz | int | MHz | CPU frequency |

### Per-reading fields

| Field | Type | Description |
|---|---|---|
| t | uint32 ms | Offset from batch_start |
| obd.010C | string | Raw RPM hex from ELM327 |
| obd.010D | string | Raw speed hex from ELM327 |
| obd.0111 | string | Raw throttle hex from ELM327 |
| obd.0105 | string | Raw coolant temp hex from ELM327 |
| obd.0142 | string | Raw battery voltage hex from ELM327 |
| imu.ax | float g | Raw accelerometer X |
| imu.ay | float g | Raw accelerometer Y |
| imu.az | float g | Raw accelerometer Z |
| imu.gx | float °/s | Raw gyroscope X |
| imu.gy | float °/s | Raw gyroscope Y |
| imu.gz | float °/s | Raw gyroscope Z |

### OBD error values

| Value | Meaning |
|---|---|
| `"NO DATA"` | ECU did not respond to PID |
| `"?"` | ELM327 did not understand command |
| `null` | Timeout — no response received |
| `"BUS BUSY"` | K-Line bus was busy |

Hono handles all error values and inserts NULL into Postgres.

---

## What Hono Derives

All computed by Hono parser — never sent by ESP32:

| Derived field | Source | Formula |
|---|---|---|
| rpm | obd.010C | `((A*256)+B)/4` |
| speed_kmh | obd.010D | `A` |
| throttle | obd.0111 | `A*100/255` |
| coolant | obd.0105 | `A-40` |
| vbat | obd.0142 | `((A*256)+B)/1000` |
| lean | imu.ay, imu.az | `atan2(ay,az) * (180/π)` |
| pitch | imu.ax, imu.az | `atan2(-ax,az) * (180/π)` |
| g_total | imu.ax/ay/az | `sqrt(ax²+ay²+az²)` |

---

## Crash Event Payload

Published to: `trak/{device_id}/crash`
Trigger: ESP32 detects crash using raw G-total threshold on raw ax/ay/az
QoS: 1, Retain: true

Note: ESP32 still performs crash detection locally — it computes g_total from raw IMU floats only for this purpose. No full parsing needed.

```json
{
  "v": 1,
  "device_id": "trak-cb350-001",
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "crash_timestamp": 1717234500000,
  "raw_imu_at_crash": {
    "ax": 3.2,
    "ay": -5.1,
    "az": 1.4,
    "gx": 120.0,
    "gy": 45.0,
    "gz": 30.0
  },
  "g_total_at_crash": 6.8,
  "obd_speed_raw": "410D02",
  "pre_crash_batch_index": 41
}
```

---

## Status Heartbeat Payload

Published to: `trak/{device_id}/status`
Frequency: every 60 seconds
QoS: 0

```json
{
  "v": 1,
  "device_id": "trak-cb350-001",
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "timestamp": 1717234260000,
  "uptime_ms": 3600000,
  "wifi_rssi": -62,
  "free_heap_bytes": 180000,
  "sd_free_mb": 7340,
  "unsynced_batches": 0,
  "obd_connected": true,
  "imu_ok": true,
  "chip_temp": 42.5,
  "battery_voltage_raw": "41420BB8"
}
```

---

## Command Payload

Published to: `trak/{device_id}/command`
QoS: 1

```json
{ "command": "reboot", "payload": {} }
```

| Command | Effect |
|---|---|
| `reboot` | Restart ESP32 |
| `calibrate_imu` | Run IMU calibration |
| `end_session` | Finalise session, start new |
| `sync_now` | Replay all unsynced SD batches |

---

## Backend Subscriber

```typescript
import mqtt from 'mqtt'

const client = mqtt.connect(`mqtt://${MQTT_BROKER}:1883`)

client.subscribe('trak/+/telemetry', { qos: 1 })
client.subscribe('trak/+/crash',     { qos: 1 })
client.subscribe('trak/+/status',    { qos: 0 })

client.on('message', async (topic, payload) => {
  const [, deviceId, messageType] = topic.split('/')
  const body = JSON.parse(payload.toString())

  switch (messageType) {
    case 'telemetry': await handleTelemetryBatch(deviceId, body); break
    case 'crash':     await handleCrashEvent(deviceId, body);     break
    case 'status':    await handleStatusUpdate(deviceId, body);   break
  }
})
```

---

## Payload Size Estimates

| Payload | Readings | Approx size |
|---|---|---|
| Telemetry batch | 300 | ~65KB JSON |
| Telemetry batch minified | 300 | ~48KB |
| Crash event | — | ~400 bytes |
| Status heartbeat | — | ~300 bytes |
