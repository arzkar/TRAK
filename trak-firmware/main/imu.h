#pragma once
#include <Arduino.h>
#include "config.h"

// ─── Raw reading struct ───────────────────────────────────────────────────────
struct IMURaw {
  uint32_t t;           // ms offset from session start
  float    ax, ay, az;  // accelerometer in g  (raw ÷ ACCEL_SCALE)
  float    gx, gy, gz;  // gyroscope    in °/s (raw ÷ GYRO_SCALE)
};

// ─── Calibration offsets (loaded from SD on boot) ────────────────────────────
struct IMUCalibration {
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
};

// Returns true if MPU6050 was found and initialised.
// If not found, subsequent readIMU() calls return zero-filled structs.
bool     initIMU();
void     loadIMUCalibration(const IMUCalibration& cal);

// Fills `out` with the latest raw reading (zeros if sensor unavailable).
void     readIMU(IMURaw& out);

// Crash detection — only computation allowed on ESP32.
// Returns g_total; caller decides whether to trigger crash event.
float    computeGTotal(const IMURaw& r);

bool     imuAvailable();   // runtime flag
