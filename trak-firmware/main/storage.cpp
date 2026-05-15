#include "storage.h"
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <functional>

static bool _sdAvailable = false;

// ─── Init ─────────────────────────────────────────────────────────────────────
bool initStorage() {
  if (!SD.begin(PIN_SD_CS)) {
    Serial.println("[SD] SD card not found — running in RAM-only mode");
    _sdAvailable = false;
    return false;
  }
  // Ensure base directories exist
  SD.mkdir("/trak");
  SD.mkdir(SD_SESSIONS_PATH);
  Serial.printf("[SD] Mounted OK. Free: %.1f MB\n", sdFreeMB());
  _sdAvailable = true;
  return true;
}

// ─── Calibration ──────────────────────────────────────────────────────────────
IMUCalibration loadCalibration() {
  IMUCalibration cal = {0, 0, 0, 0, 0, 0};
  if (!_sdAvailable) {
    Serial.println("[SD] No SD — using zero calibration offsets");
    return cal;
  }
  if (!SD.exists(SD_CALIBRATION_PATH)) {
    Serial.println("[SD] calibration.json missing — using zero offsets");
    return cal;
  }
  File f = SD.open(SD_CALIBRATION_PATH, FILE_READ);
  if (!f) return cal;

  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, f) == DeserializationError::Ok) {
    cal.ax = doc["ax_offset"] | 0;
    cal.ay = doc["ay_offset"] | 0;
    cal.az = doc["az_offset"] | 0;
    cal.gx = doc["gx_offset"] | 0;
    cal.gy = doc["gy_offset"] | 0;
    cal.gz = doc["gz_offset"] | 0;
    Serial.println("[SD] Calibration loaded from SD");
  }
  f.close();
  return cal;
}

// ─── Batch write ──────────────────────────────────────────────────────────────
void writeBatchToSD(const String& sessionDir, uint32_t batchIndex,
                    const String& json) {
  if (!_sdAvailable) return;

  String dir = String(SD_SESSIONS_PATH) + "/" + sessionDir + "/batches";
  SD.mkdir(dir.c_str());

  char filename[64];
  snprintf(filename, sizeof(filename), "%s/batch_%04u.json",
           dir.c_str(), batchIndex);

  File f = SD.open(filename, FILE_WRITE);
  if (!f) {
    Serial.printf("[SD] Failed to open %s for write\n", filename);
    return;
  }
  f.print(json);
  f.close();
}

void markBatchSynced(const String& sessionDir, uint32_t batchIndex) {
  if (!_sdAvailable) return;
  char filename[64];
  snprintf(filename, sizeof(filename),
           "%s/%s/batches/batch_%04u.synced",
           SD_SESSIONS_PATH, sessionDir.c_str(), batchIndex);
  // Empty sidecar file = synced marker
  File f = SD.open(filename, FILE_WRITE);
  if (f) f.close();
}

// ─── Replay ───────────────────────────────────────────────────────────────────
void replayUnsynced(const String& sessionDir,
                    std::function<bool(const String&)> publishCallback) {
  if (!_sdAvailable) return;

  String batchDir = String(SD_SESSIONS_PATH) + "/" + sessionDir + "/batches";
  File dir = SD.open(batchDir.c_str());
  if (!dir) return;

  File f;
  while ((f = dir.openNextFile())) {
    String name = f.name();
    if (!name.endsWith(".json")) { f.close(); continue; }

    // Check sidecar
    String syncedPath = name;
    syncedPath.replace(".json", ".synced");
    if (SD.exists(syncedPath.c_str())) { f.close(); continue; }

    // Read and publish
    String payload = "";
    while (f.available()) payload += (char)f.read();
    f.close();

    if (publishCallback(payload)) {
      // Mark synced
      File sf = SD.open(syncedPath.c_str(), FILE_WRITE);
      if (sf) sf.close();
      Serial.printf("[SD] Replayed and synced: %s\n", name.c_str());
    }
    delay(100);  // don't flood broker
  }
  dir.close();
}

// ─── Log ─────────────────────────────────────────────────────────────────────
void appendLog(const char* level, const char* msg) {
  Serial.printf("[%s] %s\n", level, msg);
  if (!_sdAvailable) return;
  File f = SD.open(SD_LOG_PATH, FILE_APPEND);
  if (!f) return;
  f.printf("[%s] %s\n", level, msg);
  f.close();
}

// ─── Status ───────────────────────────────────────────────────────────────────
float sdFreeMB() {
  if (!_sdAvailable) return -1.0f;
  return (float)(SD.totalBytes() - SD.usedBytes()) / (1024.0f * 1024.0f);
}

bool storageAvailable() {
  return _sdAvailable;
}
