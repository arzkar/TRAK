# imu.spec.md
# TRAK — IMU Specification (MPU6050)

---

## Sensor Configuration

| Parameter | Setting | Justification |
|---|---|---|
| Accelerometer full-scale range | ±8g | Covers normal riding (±2g) and crash detection (up to 8g) |
| Gyroscope full-scale range | ±500°/s | Covers aggressive cornering lean transitions |
| Digital low-pass filter (DLPF) | 44Hz bandwidth | Attenuates engine vibration (above 44Hz) without distorting lean dynamics |
| Sample rate divider | 9 | Gives 100Hz effective sample rate (1kHz / (9+1)) |
| I²C address | 0x68 (AD0 = GND) | |
| Clock source | PLL with X-axis gyro reference | More stable than internal oscillator |

---

## Axis Orientation

Mount MPU6050 flat on a rigid surface of the frame rail (horizontal, parallel to ground when bike is upright). Align axes as follows:

```
        Forward (bike direction)
             ↑  X+
             │
   Y- ───────┼─────── Y+  (right side of bike)
             │
             ↓  X-

        Z+ = up (out of board face)
        Z- = down (into frame)
```

**Mounting requirements:**
- Rigid mounting — no rubber damping (dampening distorts IMU data)
- Frame rail preferred over engine mount (less vibration noise)
- Level when bike is on centre stand (calibrate with bike upright)

---

## Computed Measurements

### Lean Angle (Roll)

```cpp
// Static lean angle from accelerometer
float leanAngle_accel = atan2(ay, az) * RAD_TO_DEG;

// Dynamic lean angle with complementary filter (recommended)
// Alpha = 0.98 weights gyro 98%, accel 2%
// dt = time delta in seconds between samples (0.01s at 100Hz)
float leanAngle = 0.98f * (leanAngle + gyroX * dt) + 0.02f * leanAngle_accel;
```

- Positive value: right lean
- Negative value: left lean
- Range: -90° to +90°
- Accuracy: ±2° (complementary filter), ±5° (accelerometer only)

### Pitch Angle

```cpp
float pitchAngle_accel = atan2(-ax, az) * RAD_TO_DEG;
float pitchAngle = 0.98f * (pitchAngle + gyroY * dt) + 0.02f * pitchAngle_accel;
```

- Positive: nose up (acceleration squat)
- Negative: nose down (braking dive)

### G-Force Magnitude

```cpp
float gTotal = sqrt(ax*ax + ay*ay + az*az);
// At rest: gTotal ≈ 1.0g (gravity)
// Hard braking: gTotal ≈ 1.2–1.5g
// Crash: gTotal > 4.0g
```

### Lateral G-Force (cornering load)

```cpp
// Lateral G is the Y-axis component (adjusted for lean)
float lateralG = ay;
```

### Longitudinal G-Force (braking/acceleration)

```cpp
// Longitudinal G is the X-axis component
float longitudinalG = ax;
// Positive: acceleration
// Negative: braking
```

---

## Calibration Procedure

Run once on first boot with bike stationary and upright on centre stand:

```cpp
void calibrateMPU6050() {
  // Collect 1000 samples at rest
  long ax_sum = 0, ay_sum = 0, az_sum = 0;
  long gx_sum = 0, gy_sum = 0, gz_sum = 0;

  for (int i = 0; i < 1000; i++) {
    int16_t ax, ay, az, gx, gy, gz;
    imu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    ax_sum += ax; ay_sum += ay; az_sum += az;
    gx_sum += gx; gy_sum += gy; gz_sum += gz;
    delay(2);
  }

  // Offsets
  ax_offset = ax_sum / 1000;
  ay_offset = ay_sum / 1000;
  az_offset = (az_sum / 1000) - 16384;  // subtract 1g (16384 LSB/g at ±2g)
  gx_offset = gx_sum / 1000;
  gy_offset = gy_sum / 1000;
  gz_offset = gz_sum / 1000;

  // Store offsets in SPIFFS for persistence across reboots
  saveCalibrationToSPIFFS();
}
```

Apply offsets at every reading:
```cpp
ax = (raw_ax - ax_offset) / 4096.0f;  // convert to g (±8g = 4096 LSB/g)
ay = (raw_ay - ay_offset) / 4096.0f;
az = (raw_az - az_offset) / 4096.0f;
gx = (raw_gx - gx_offset) / 65.5f;   // convert to °/s (±500°/s = 65.5 LSB/°/s)
gy = (raw_gy - gy_offset) / 65.5f;
gz = (raw_gz - gz_offset) / 65.5f;
```

---

## Crash Detection

```cpp
#define CRASH_G_THRESHOLD    4.0f   // g-force spike threshold
#define CRASH_SPEED_THRESHOLD 5.0f  // km/h — bike considered stopped
#define CRASH_CONFIRM_MS     500    // must persist for 500ms to confirm

bool detectCrash(float gTotal, float speed_kmh) {
  static unsigned long crashStartTime = 0;
  static bool possibleCrash = false;

  if (gTotal > CRASH_G_THRESHOLD && speed_kmh < CRASH_SPEED_THRESHOLD) {
    if (!possibleCrash) {
      possibleCrash = true;
      crashStartTime = millis();
    } else if (millis() - crashStartTime > CRASH_CONFIRM_MS) {
      return true;  // confirmed crash
    }
  } else {
    possibleCrash = false;
  }
  return false;
}
```

On crash confirmed:
1. Set `session.crash_detected = true`
2. Record `session.crash_timestamp`
3. Flush current IMU buffer immediately to SD
4. Publish crash event to MQTT topic `trak/cb350/crash`

---

## Pre-Crash Buffer

Maintain a circular buffer of the last 10 seconds of IMU readings (1000 samples at 100Hz) to capture pre-crash dynamics:

```cpp
#define PRE_CRASH_BUFFER_SIZE 1000  // 10s at 100Hz

IMUReading preCrashBuffer[PRE_CRASH_BUFFER_SIZE];
int bufferIndex = 0;

void updatePreCrashBuffer(IMUReading r) {
  preCrashBuffer[bufferIndex % PRE_CRASH_BUFFER_SIZE] = r;
  bufferIndex++;
}
```

---

## IMU Reading Struct

```cpp
struct IMUReading {
  uint32_t timestamp;   // millis() offset from session start
  float ax;             // accelerometer X (g)
  float ay;             // accelerometer Y (g)
  float az;             // accelerometer Z (g)
  float gx;             // gyroscope X (°/s)
  float gy;             // gyroscope Y (°/s)
  float gz;             // gyroscope Z (°/s)
  float lean;           // computed lean angle (°)
  float pitch;          // computed pitch angle (°)
  float g_total;        // computed G magnitude
};
// Size: 40 bytes per reading
// At 100Hz downsampled to 10Hz for batching: 300 readings per 30s batch
// Batch size: 300 × 40 = 12,000 bytes ≈ 12KB
```

---

## Known Limitations

| Limitation | Impact | Mitigation |
|---|---|---|
| Accelerometer drift | Lean angle error under sustained acceleration | Complementary filter with gyro |
| Vibration noise from engine | High-frequency noise on all axes | DLPF at 44Hz |
| Temperature sensitivity | Gyro drift changes with temperature | Recalibrate after warmup |
| No magnetometer | No absolute heading | GPS heading in V2 |
| Single IMU | No redundancy | Acceptable for V1 |
