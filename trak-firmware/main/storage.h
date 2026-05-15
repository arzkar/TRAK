#pragma once
#include <Arduino.h>
#include "config.h"
#include "imu.h"

// Initialise SD card. Returns true if mounted.
// If false, firmware continues in RAM-only mode (no offline buffering).
bool initStorage();

// Load IMU calibration from SD. Returns default (all zeros) if file missing.
IMUCalibration loadCalibration();

// Persist the given batch JSON to SD.
// Path: /trak/sessions/<session_dir>/batches/batch_NNNN.json
void writeBatchToSD(const String& sessionDir, uint32_t batchIndex,
                    const String& json);

// Mark a batch as synced (creates an empty .synced sidecar file).
void markBatchSynced(const String& sessionDir, uint32_t batchIndex);

// Replay all unsynced batches for the current session via MQTT.
// Calls publishCallback for each pending batch JSON.
void replayUnsynced(const String& sessionDir,
                    std::function<bool(const String&)> publishCallback);

// Append a log line to device.log (rolling 500 lines).
void appendLog(const char* level, const char* msg);

// Free megabytes on the SD card. Returns -1 if SD not mounted.
float sdFreeMB();

bool storageAvailable();
