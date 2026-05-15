#include "batch.h"
#include <ArduinoJson.h>
#include <esp_system.h>
#include <time.h>

// ─── UUID generation (v4) ─────────────────────────────────────────────────────
static void generateUUID(char* out) {
  uint8_t b[16];
  for (int i = 0; i < 4; i++) {
    uint32_t r = esp_random();
    b[i*4+0] = (r >> 24) & 0xFF;
    b[i*4+1] = (r >> 16) & 0xFF;
    b[i*4+2] = (r >>  8) & 0xFF;
    b[i*4+3] =  r        & 0xFF;
  }
  b[6] = (b[6] & 0x0F) | 0x40;  // version 4
  b[8] = (b[8] & 0x3F) | 0x80;  // variant bits

  snprintf(out, 37,
    "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    b[0],b[1],b[2],b[3], b[4],b[5], b[6],b[7],
    b[8],b[9], b[10],b[11],b[12],b[13],b[14],b[15]);
}

// ─── Session ──────────────────────────────────────────────────────────────────
Session createSession() {
  Session s;
  s.startMs = millis();
  generateUUID(s.uuid);

  // Use uptime as directory name (no RTC available in V1)
  snprintf(s.dir, sizeof(s.dir), "sess_%010lu", s.startMs);
  Serial.printf("[Session] Started: %s\n", s.uuid);
  return s;
}

// ─── Batch ────────────────────────────────────────────────────────────────────
void batchInit(Batch& b, uint32_t index, uint32_t startMs) {
  b.index   = index;
  b.startMs = startMs;
  b.count   = 0;
}

bool batchAdd(Batch& b, const IMURaw& imu, const OBDRaw& obd) {
  if (b.count >= BATCH_SIZE) return false;
  BatchReading& r = b.readings[b.count++];
  r.t   = millis() - b.startMs;  // offset from batch start
  r.imu = imu;
  r.obd = obd;
  return true;
}

bool batchFull(const Batch& b) {
  return b.count >= BATCH_SIZE;
}

// ─── JSON serialisation — Option B raw payload ────────────────────────────────
String batchSerialise(const Batch& b, const Session& session) {
  // 65536 byte document — fits a full 300-reading raw batch (~48KB minified)
  DynamicJsonDocument doc(JSON_DOC_CAPACITY);

  doc["v"]                 = 1;
  doc["device_id"]         = DEVICE_ID;
  doc["session_id"]        = session.uuid;
  doc["firmware"]          = FIRMWARE_VERSION;
  doc["batch_index"]       = b.index;
  doc["batch_start"]       = (uint64_t)b.startMs;
  doc["batch_duration_ms"] = (uint32_t)(millis() - b.startMs);
  doc["reading_count"]     = b.count;

  // Device block — built-in ESP32 sensors always available
  JsonObject dev = doc.createNestedObject("device");
  dev["chip_temp"]    = temperatureRead();       // ESP32 internal
  dev["free_heap"]    = (uint32_t)ESP.getFreeHeap();
  dev["uptime_ms"]    = millis();
  dev["wifi_rssi"]    = 0;                       // filled by mqtt.cpp at publish time
  dev["cpu_freq_mhz"] = (uint32_t)getCpuFrequencyMhz();

  // Readings array — raw OBD hex + raw IMU floats
  JsonArray readings = doc.createNestedArray("readings");
  for (uint32_t i = 0; i < b.count; i++) {
    const BatchReading& r = b.readings[i];
    JsonObject ro = readings.createNestedObject();
    ro["t"] = r.obd.t;  // use OBD timestamp as canonical

    JsonObject obd = ro.createNestedObject("obd");
    obd["010C"] = r.obd.pid010C;
    obd["010D"] = r.obd.pid010D;
    obd["0111"] = r.obd.pid0111;
    obd["0105"] = r.obd.pid0105;
    obd["0142"] = r.obd.pid0142;

    JsonObject imu = ro.createNestedObject("imu");
    imu["ax"] = serialized(String(r.imu.ax, 4));
    imu["ay"] = serialized(String(r.imu.ay, 4));
    imu["az"] = serialized(String(r.imu.az, 4));
    imu["gx"] = serialized(String(r.imu.gx, 4));
    imu["gy"] = serialized(String(r.imu.gy, 4));
    imu["gz"] = serialized(String(r.imu.gz, 4));
  }

  String out;
  out.reserve(doc.memoryUsage());
  serializeJson(doc, out);
  return out;
}
