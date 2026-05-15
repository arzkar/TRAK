# firmware.spec.md
# TRAK — Firmware Specification
# Updated: Option B — ESP32 collects raw data only, no parsing

---

## Environment

| Property | Value |
|---|---|
| IDE | Arduino IDE 2.x |
| Board package | Espressif ESP32 Arduino Core (latest stable) |
| Board selection | ESP32 Dev Module |
| Upload speed | 115200 baud (stable on CH340C boards) |
| Flash frequency | 80MHz |
| Partition scheme | Default 4MB with spiffs |
| PSRAM | Disabled (WROOM-32D has no PSRAM) |

---

## Libraries

| Library | Version | Purpose | Install via |
|---|---|---|---|
| MPU6050 by ElectronicCats | latest | IMU raw data | Library Manager |
| PubSubClient | 2.8+ | MQTT client | Library Manager |
| ArduinoJson | 6.x | JSON serialization | Library Manager |
| SD | built-in | MicroSD read/write | Built-in |
| Wire | built-in | I²C bus | Built-in |
| SPI | built-in | SPI bus | Built-in |
| BluetoothSerial | built-in | ELM327 BT connection | Built-in (ESP32 core) |
| WiFi | built-in | Network connectivity | Built-in (ESP32 core) |

---

## Design — ESP32 Responsibilities

**ESP32 does:**
- Connect to ELM327 over Bluetooth and send AT commands
- Collect raw hex response strings from ELM327 as-is
- Read raw ax/ay/az/gx/gy/gz floats from MPU6050
- Read built-in sensors (chip temp, free heap, RSSI, uptime)
- Compute g_total from raw IMU for crash detection only
- Package everything into a raw JSON batch
- Write batch to SD card
- Publish batch to MQTT broker
- Replay unsynced SD batches on WiFi reconnect

**ESP32 does NOT do:**
- Parse OBD hex into RPM, speed, throttle etc
- Compute lean angle or pitch
- Do any unit conversion
- Make any analytics decisions

All parsing and computation happens in Hono backend.

---

## Architecture

### FreeRTOS Task Layout

```
Core 0                              Core 1
──────────────────────────────      ──────────────────────────────
Task: obd_task                      Task: imu_task
  Priority: 2                         Priority: 3 (highest)
  Stack: 4096 bytes                    Stack: 2048 bytes
  Interval: 100ms poll                 Interval: 10ms sample
  Collects: raw hex strings            Collects: raw ax/ay/az/gx/gy/gz
  from ELM327 for each PID             Crash detection: g_total only

Task: mqtt_task                     Task: sd_task
  Priority: 1                         Priority: 2
  Stack: 8192 bytes                    Stack: 4096 bytes
  Interval: 30s batch publish          Interval: 500ms write
  Handles: WiFi reconnect              Handles: offline buffering
           SD replay on connect                 batch file management
```

### Shared Memory (FreeRTOS Queues)

```
imu_task   ──→ imu_queue   (capacity: 50 readings)  ──→ sd_task
obd_task   ──→ obd_queue   (capacity: 10 readings)   ──→ sd_task
sd_task    ──→ mqtt_queue  (capacity: 5 batches)     ──→ mqtt_task
```

All queues protected by FreeRTOS semaphores.

---

## Sampling Rates

| Data source | Sample interval | Samples per 30s batch |
|---|---|---|
| MPU6050 (IMU) | 10ms (100Hz) | 3,000 |
| ELM327 OBD | 100ms (10Hz) | 300 |
| SD card write | 500ms | — |
| MQTT publish | 30,000ms | 1 batch |

IMU downsampled to 10Hz before batching → 300 IMU readings per batch matching OBD rate.

---

## Data Structs

```cpp
struct IMURaw {
  uint32_t t;     // ms offset from session start
  float ax, ay, az;  // raw accelerometer (g)
  float gx, gy, gz;  // raw gyroscope (°/s)
  // NO lean, pitch, g_total — computed in Hono
};

struct OBDRaw {
  uint32_t t;           // ms offset
  char pid010C[16];     // raw hex string e.g. "410C1AF8"
  char pid010D[16];
  char pid0111[16];
  char pid0105[16];
  char pid0142[16];
};

struct DeviceInfo {
  float chip_temp;
  uint32_t free_heap;
  uint32_t uptime_ms;
  int wifi_rssi;
  uint32_t cpu_freq_mhz;
};
```

---

## Memory Budget

| Resource | Allocated | Notes |
|---|---|---|
| obd_task stack | 4096 bytes | |
| imu_task stack | 2048 bytes | |
| mqtt_task stack | 8192 bytes | JSON serialization needs headroom |
| sd_task stack | 4096 bytes | |
| imu_queue | ~3KB | 50 × IMURaw |
| obd_queue | ~1KB | 10 × OBDRaw |
| mqtt_queue | ~20KB | 5 batches × ~4KB (raw hex strings larger than parsed ints) |
| JSON batch buffer | ~22KB | One 30s raw batch serialized |
| Pre-crash buffer | ~12KB | 300 IMURaw readings (10s at 30Hz) |
| Total heap used | ~70KB | Well within 520KB SRAM |

---

## OBD Task — Raw Collection Only

```cpp
void obdTask(void* param) {
  while (true) {
    OBDRaw r;
    r.t = millis() - sessionStart;

    // Send PID, store raw response string — no parsing
    String response;

    response = queryELM327("010C");
    response.toCharArray(r.pid010C, sizeof(r.pid010C));

    response = queryELM327("010D");
    response.toCharArray(r.pid010D, sizeof(r.pid010D));

    response = queryELM327("0111");
    response.toCharArray(r.pid0111, sizeof(r.pid0111));

    response = queryELM327("0105");
    response.toCharArray(r.pid0105, sizeof(r.pid0105));

    response = queryELM327("0142");
    response.toCharArray(r.pid0142, sizeof(r.pid0142));

    xQueueSend(obd_queue, &r, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

String queryELM327(String pid) {
  BT.println(pid);
  unsigned long start = millis();
  String response = "";
  while (millis() - start < 200) {
    while (BT.available()) {
      char c = BT.read();
      if (c == '>') return response.trim();
      response += c;
    }
  }
  return "NO DATA";  // timeout
}
```

---

## IMU Task — Raw Collection + Crash Detection Only

```cpp
void imuTask(void* param) {
  while (true) {
    int16_t rawAx, rawAy, rawAz, rawGx, rawGy, rawGz;
    imu.getMotion6(&rawAx, &rawAy, &rawAz, &rawGx, &rawGy, &rawGz);

    IMURaw r;
    r.t  = millis() - sessionStart;
    r.ax = (rawAx - ax_offset) / 4096.0f;
    r.ay = (rawAy - ay_offset) / 4096.0f;
    r.az = (rawAz - az_offset) / 4096.0f;
    r.gx = (rawGx - gx_offset) / 65.5f;
    r.gy = (rawGy - gy_offset) / 65.5f;
    r.gz = (rawGz - gz_offset) / 65.5f;

    // Crash detection — only computation allowed on ESP32
    float gTotal = sqrt(r.ax*r.ax + r.ay*r.ay + r.az*r.az);
    if (gTotal > CRASH_G_THRESHOLD) {
      triggerCrashEvent(r, gTotal);
    }

    // Update pre-crash circular buffer
    preCrashBuffer[bufferIndex % PRE_CRASH_BUFFER_SIZE] = r;
    bufferIndex++;

    xQueueSend(imu_queue, &r, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}
```

---

## JSON Batch Serialization

```cpp
String buildBatchJSON(OBDRaw* obdReadings, IMURaw* imuReadings, int count, DeviceInfo& device) {
  StaticJsonDocument<24000> doc;

  doc["v"]                  = 1;
  doc["device_id"]          = DEVICE_ID;
  doc["session_id"]         = currentSessionId;
  doc["firmware"]           = FIRMWARE_VERSION;
  doc["batch_index"]        = batchIndex++;
  doc["batch_start"]        = batchStartTime;
  doc["batch_duration_ms"]  = millis() - batchStartTime;
  doc["reading_count"]      = count;

  JsonObject dev = doc.createNestedObject("device");
  dev["chip_temp"]    = device.chip_temp;
  dev["free_heap"]    = device.free_heap;
  dev["uptime_ms"]    = device.uptime_ms;
  dev["wifi_rssi"]    = device.wifi_rssi;
  dev["cpu_freq_mhz"] = device.cpu_freq_mhz;

  JsonArray readings = doc.createNestedArray("readings");
  for (int i = 0; i < count; i++) {
    JsonObject r = readings.createNestedObject();
    r["t"] = obdReadings[i].t;

    JsonObject obd = r.createNestedObject("obd");
    obd["010C"] = obdReadings[i].pid010C;
    obd["010D"] = obdReadings[i].pid010D;
    obd["0111"] = obdReadings[i].pid0111;
    obd["0105"] = obdReadings[i].pid0105;
    obd["0142"] = obdReadings[i].pid0142;

    JsonObject imuObj = r.createNestedObject("imu");
    imuObj["ax"] = imuReadings[i].ax;
    imuObj["ay"] = imuReadings[i].ay;
    imuObj["az"] = imuReadings[i].az;
    imuObj["gx"] = imuReadings[i].gx;
    imuObj["gy"] = imuReadings[i].gy;
    imuObj["gz"] = imuReadings[i].gz;
  }

  String output;
  serializeJson(doc, output);
  return output;
}
```

---

## State Machine

```
BOOT
  │
  ▼
INIT_HARDWARE
  ├── Wire.begin(21, 22)
  ├── SD.begin(5)
  ├── imu.initialize()
  └── BluetoothSerial.begin()
  │
  ▼
CONNECT_ELM327
  ├── BT.connect("OBDII")
  └── Send init sequence (see obd.spec.md)
  │
  ▼
CALIBRATE_IMU
  └── Load offsets from SD calibration.json
      (run full calibration if file missing)
  │
  ▼
CONNECT_WIFI
  ├── WiFi.begin(SSID, PASS)
  ├── Timeout: 15s
  └── On fail: OFFLINE_MODE
  │
  ▼
START_SESSION
  └── Generate session UUID, create SD directory
  │
  ▼
RUNNING
  ├── imu_task:  collect raw IMU every 10ms
  ├── obd_task:  collect raw OBD hex every 100ms
  ├── sd_task:   write batch to SD every 500ms
  └── mqtt_task: publish batch every 30s
        ├── WiFi on:  publish → mark synced
        └── WiFi off: write to SD only
              └── On reconnect: replay unsynced batches
```

---

## Configuration Constants

```cpp
#define WIFI_SSID           "your_ssid"
#define WIFI_PASS           "your_password"
#define MQTT_BROKER         "192.168.1.x"
#define MQTT_PORT           1883
#define MQTT_TOPIC          "trak/cb350-001/telemetry"
#define MQTT_STATUS_TOPIC   "trak/cb350-001/status"
#define MQTT_CRASH_TOPIC    "trak/cb350-001/crash"
#define DEVICE_ID           "trak-cb350-001"
#define FIRMWARE_VERSION    "1.0.0"

// Pins
#define SD_CS_PIN           5
#define MPU_INT_PIN         4

// Timing
#define IMU_SAMPLE_MS       10
#define OBD_POLL_MS         100
#define SD_WRITE_MS         500
#define MQTT_BATCH_MS       30000
#define STATUS_INTERVAL_MS  60000

// IMU config
#define ACCEL_RANGE         MPU6050_ACCEL_FS_8    // ±8g
#define GYRO_RANGE          MPU6050_GYRO_FS_500   // ±500°/s
#define CRASH_G_THRESHOLD   4.0f

// OBD
#define BT_DEVICE_NAME      "OBDII"
#define OBD_TIMEOUT_MS      200
```

---

## Error Handling

| Condition | Response |
|---|---|
| MPU6050 not found | Log, halt IMU task, continue without IMU |
| SD card not present | Log, disable SD task, RAM buffer only |
| ELM327 BT fail | Retry every 5s, max 3 retries |
| ELM327 returns NO DATA | Store "NO DATA" string as-is, Hono handles |
| ELM327 timeout | Store "NO DATA" string |
| WiFi fail | OFFLINE_MODE, write to SD |
| MQTT publish fail | Retain batch, retry next cycle |
| SD card full | Stop writing, log error |
| Crash detected | Flush batch immediately, publish crash event |
