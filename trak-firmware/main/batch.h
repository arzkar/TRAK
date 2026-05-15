#pragma once
#include <Arduino.h>
#include "config.h"
#include "imu.h"
#include "obd.h"

// ─── Session ──────────────────────────────────────────────────────────────────
struct Session {
  char uuid[37];       // "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx"
  char dir[24];        // "YYYYMMDD_HHMMSS" used as SD directory name
  uint32_t startMs;    // millis() at session start
};

// Generate a new session with a UUID and SD directory name.
Session createSession();

// ─── Batch building ───────────────────────────────────────────────────────────
// A Batch collects up to BATCH_SIZE aligned IMU+OBD reading pairs.
struct BatchReading {
  uint32_t t;
  IMURaw   imu;
  OBDRaw   obd;
};

struct Batch {
  uint32_t     index;
  uint32_t     startMs;
  uint32_t     count;
  BatchReading readings[BATCH_SIZE];
};

// Initialise a fresh batch.
void batchInit(Batch& b, uint32_t index, uint32_t startMs);

// Add a paired reading. Returns false if batch is full.
bool batchAdd(Batch& b, const IMURaw& imu, const OBDRaw& obd);

// Serialise to JSON string using the Option B raw payload format.
// Caller is responsible for the returned String's lifetime.
String batchSerialise(const Batch& b, const Session& session);

bool batchFull(const Batch& b);
