#include "imu.h"
#include <Wire.h>
#include <MPU6050.h>

static MPU6050       _mpu;
static bool          _imuAvailable = false;
static IMUCalibration _cal = {0, 0, 0, 0, 0, 0};

// ─── Init ─────────────────────────────────────────────────────────────────────
bool initIMU() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  _mpu.initialize();

  if (!_mpu.testConnection()) {
    Serial.println("[IMU] MPU6050 not found — running without IMU");
    _imuAvailable = false;
    return false;
  }

  // Configure scale: ±8g, ±500°/s
  _mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_8);
  _mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_500);

  Serial.println("[IMU] MPU6050 initialised OK");
  _imuAvailable = true;
  return true;
}

void loadIMUCalibration(const IMUCalibration& cal) {
  _cal = cal;
  Serial.printf("[IMU] Calibration loaded: ax=%d ay=%d az=%d gx=%d gy=%d gz=%d\n",
                cal.ax, cal.ay, cal.az, cal.gx, cal.gy, cal.gz);
}

// ─── Read ─────────────────────────────────────────────────────────────────────
// Returns zero-filled struct when sensor is unavailable.
// Zero IMU: ax=0, ay=0, az=0 — Hono will compute lean=0, pitch=0, g_total=0
// Backend knows sensor state from status heartbeat (imu_ok: false)
void readIMU(IMURaw& out) {
  out.t = millis();

  if (!_imuAvailable) {
    out.ax = out.ay = out.az = 0.0f;
    out.gx = out.gy = out.gz = 0.0f;
    return;
  }

  int16_t rawAx, rawAy, rawAz, rawGx, rawGy, rawGz;
  _mpu.getMotion6(&rawAx, &rawAy, &rawAz, &rawGx, &rawGy, &rawGz);

  out.ax = (rawAx - _cal.ax) / ACCEL_SCALE;
  out.ay = (rawAy - _cal.ay) / ACCEL_SCALE;
  out.az = (rawAz - _cal.az) / ACCEL_SCALE;
  out.gx = (rawGx - _cal.gx) / GYRO_SCALE;
  out.gy = (rawGy - _cal.gy) / GYRO_SCALE;
  out.gz = (rawGz - _cal.gz) / GYRO_SCALE;
}

// ─── Crash detection ──────────────────────────────────────────────────────────
float computeGTotal(const IMURaw& r) {
  return sqrtf(r.ax * r.ax + r.ay * r.ay + r.az * r.az);
}

bool imuAvailable() {
  return _imuAvailable;
}
