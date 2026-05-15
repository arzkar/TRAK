# storage.spec.md
# TRAK — Storage & Offline Buffering Specification

---

## Overview

The MicroSD card serves as the offline buffer — all telemetry is written to SD continuously, regardless of WiFi status. When WiFi is available, batches are published to MQTT and marked as synced. When offline, batches accumulate on SD and are replayed in order on reconnect.

---

## SD Card Requirements

| Property | Requirement |
|---|---|
| Type | MicroSD / MicroSDHC |
| Capacity | 8GB minimum |
| Speed class | Class 10 (U1 minimum) |
| Format | FAT32 |
| Max file size | 4GB (FAT32 limit — not a concern at TRAK data rates) |

---

## File Structure

```
/trak/
  sessions/
    20250601_143022/          ← session directory (YYYYMMDD_HHMMSS)
      meta.json               ← session metadata
      batches/
        batch_0001.json       ← 30s batch, readings 0–299
        batch_0002.json       ← 30s batch, readings 300–599
        batch_0003.json       ← ...
        batch_0001.synced     ← empty file = batch uploaded to MQTT
        batch_0002.synced
    20250602_091500/
      meta.json
      batches/
        ...
  calibration.json            ← IMU calibration offsets
  config.json                 ← device config (device_id, MQTT broker, etc.)
  device.log                  ← rolling error log (last 500 lines)
```

---

## Session Metadata (meta.json)

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "device_id": "trak-cb350-001",
  "firmware_version": "1.0.0",
  "started_at": 1717234222000,
  "ended_at": null,
  "crash_detected": false,
  "crash_timestamp": null,
  "total_batches": 0,
  "synced_batches": 0,
  "imu_calibrated": true
}
```

`ended_at` and `total_batches` are updated on session end (ignition off = power loss = written in shutdown hook before deep sleep).

---

## Batch File Format (batch_NNNN.json)

```json
{
  "session_id": "550e8400-e29b-41d4-a716-446655440000",
  "device_id": "trak-cb350-001",
  "batch_index": 1,
  "batch_start": 1717234222000,
  "synced": false,
  "readings": [
    {
      "t": 0,
      "rpm": 1240,
      "spd": 0,
      "tps": 12,
      "coolant": 78,
      "iat": 32,
      "voltage": 12.4,
      "lean": 1.2,
      "pitch": -0.4,
      "ax": 0.01,
      "ay": 0.02,
      "az": 9.81,
      "gx": 0.1,
      "gy": 0.0,
      "gz": 0.0,
      "g_total": 9.81
    }
  ]
}
```

Field definitions:
| Field | Type | Unit | Source |
|---|---|---|---|
| t | uint32 | ms offset from batch_start | ESP32 millis() |
| rpm | int | RPM | OBD PID 010C |
| spd | float | km/h | OBD PID 010D |
| tps | float | % | OBD PID 0111 |
| coolant | float | °C | OBD PID 0105 |
| iat | float | °C | OBD PID 010F |
| voltage | float | V | OBD PID 0142 |
| lean | float | degrees | IMU computed |
| pitch | float | degrees | IMU computed |
| ax | float | g | IMU raw |
| ay | float | g | IMU raw |
| az | float | g | IMU raw |
| gx | float | °/s | IMU raw |
| gy | float | °/s | IMU raw |
| gz | float | °/s | IMU raw |
| g_total | float | g | IMU computed |

---

## Write Strategy

```cpp
void sdTask(void* param) {
  while (true) {
    // Drain IMU queue
    IMUReading imuR;
    while (xQueueReceive(imu_queue, &imuR, 0) == pdTRUE) {
      currentBatch.addIMU(imuR);
    }

    // Drain OBD queue
    OBDReading obdR;
    while (xQueueReceive(obd_queue, &obdR, 0) == pdTRUE) {
      currentBatch.addOBD(obdR);
    }

    // Write to SD every 500ms
    currentBatch.writeToSD();

    // Check if batch is complete (30s elapsed)
    if (currentBatch.isComplete()) {
      currentBatch.finalise();
      xQueueSend(mqtt_queue, &currentBatch, portMAX_DELAY);
      currentBatch = Batch();  // start new batch
    }

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
```

---

## Offline Replay Strategy

On WiFi reconnect:

```cpp
void replayUnsynced() {
  File root = SD.open("/trak/sessions");
  while (File session = root.openNextFile()) {
    File batches = SD.open(String(session.name()) + "/batches");
    while (File batch = batches.openNextFile()) {
      String name = batch.name();
      // Skip if already synced
      if (SD.exists(name.replace(".json", ".synced"))) continue;

      // Publish to MQTT
      String payload = readFile(batch);
      if (mqttClient.publish(MQTT_TOPIC, payload.c_str())) {
        // Mark as synced
        SD.open(name.replace(".json", ".synced"), FILE_WRITE).close();
      }

      delay(100);  // don't flood broker
    }
  }
}
```

---

## Storage Estimates

| Metric | Value |
|---|---|
| Readings per 30s batch | 300 (at 10Hz merged) |
| Bytes per reading (JSON) | ~200 bytes |
| Bytes per batch (JSON) | ~60KB |
| Batches per hour | 120 |
| MB per hour of riding | ~7MB |
| 8GB card capacity | ~1,140 hours of riding |
| Typical ride duration | 1–3 hours |
| Days before card fills | Years (effectively unlimited for V1) |

---

## Calibration File (calibration.json)

```json
{
  "timestamp": 1717234000000,
  "ax_offset": 120,
  "ay_offset": -45,
  "az_offset": 230,
  "gx_offset": 15,
  "gy_offset": -8,
  "gz_offset": 3
}
```

Loaded on boot. If not present, run calibration routine before first session.

---

## Config File (config.json)

```json
{
  "device_id": "trak-cb350-001",
  "firmware_version": "1.0.0",
  "wifi_ssid": "your_ssid",
  "wifi_password": "your_password",
  "mqtt_broker": "192.168.1.x",
  "mqtt_port": 1883,
  "mqtt_topic": "trak/cb350/telemetry",
  "obd_bt_name": "OBDII",
  "imu_sample_hz": 100,
  "obd_poll_hz": 10,
  "batch_interval_ms": 30000,
  "crash_g_threshold": 4.0
}
```

Config is read-only at runtime. Update by editing file on SD card via PC.

---

## Error Log (device.log)

Rolling text log, max 500 lines. Older entries overwritten.

```
[2025-06-01 14:30:22] INFO  Session started: 550e8400
[2025-06-01 14:30:23] INFO  MPU6050 initialized OK
[2025-06-01 14:30:24] INFO  SD card mounted OK (7.4GB free)
[2025-06-01 14:30:25] INFO  ELM327 connected: OBDII
[2025-06-01 14:30:26] INFO  OBD init OK, protocol: ISO 14230-4
[2025-06-01 14:30:27] WARN  WiFi connect failed, entering offline mode
[2025-06-01 14:31:00] INFO  Batch 0001 written to SD
[2025-06-01 14:32:15] INFO  WiFi connected: 192.168.1.1
[2025-06-01 14:32:16] INFO  Replaying 1 unsynced batch
[2025-06-01 14:32:17] INFO  Batch 0001 published to MQTT
```
