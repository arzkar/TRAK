#pragma once

#include "secrets.h"

// ─── Identity ────────────────────────────────────────────────────────────────
#define DEVICE_ID          "trak-hcb350-001"
#define FIRMWARE_VERSION   "1.0.0"

// ─── WiFi ────────────────────────────────────────────────────────────────────
// Values defined in secrets.h
#define WIFI_TIMEOUT_MS    15000

// ─── MQTT ────────────────────────────────────────────────────────────────────
// Values defined in secrets.h
#define MQTT_PORT          1883
#define MQTT_KEEPALIVE     60

#define MQTT_TELEMETRY_TOPIC  "trak/" DEVICE_ID "/telemetry"
#define MQTT_STATUS_TOPIC     "trak/" DEVICE_ID "/status"
#define MQTT_CRASH_TOPIC      "trak/" DEVICE_ID "/crash"
#define MQTT_COMMAND_TOPIC    "trak/" DEVICE_ID "/command"

// ─── GPIO ────────────────────────────────────────────────────────────────────
#define PIN_I2C_SDA        21
#define PIN_I2C_SCL        22
#define PIN_MPU_INT         4   // optional interrupt
#define PIN_SD_CS           5
// SPI: SCK=18, MISO=19, MOSI=23 (ESP32 hardware defaults)

// ─── Timing (ms) ─────────────────────────────────────────────────────────────
#define IMU_SAMPLE_MS      10     // 100Hz raw, downsampled to 10Hz for batch
#define OBD_POLL_MS        100    // 10Hz
#define SD_WRITE_MS        500
#define MQTT_BATCH_MS      30000  // 30s batch window
#define STATUS_INTERVAL_MS 10000  // 10s for testing

// ─── OBD ─────────────────────────────────────────────────────────────────────
#define OBD_BT_NAME            "OBDII"
#define OBD_RESPONSE_TIMEOUT   200   // ms to wait for ELM327 prompt
#define OBD_RETRY_INTERVAL_MS  5000
#define OBD_MAX_RETRIES        3
#define OBD_RESPONSE_BUF       32    // max chars in a raw ELM327 response

// ─── IMU ─────────────────────────────────────────────────────────────────────
// MPU6050 configured for ±8g / ±500°/s
#define ACCEL_SCALE        4096.0f   // LSB per g
#define GYRO_SCALE         65.5f     // LSB per °/s
#define CRASH_G_THRESHOLD  4.0f      // g_total above this = crash candidate
#define PRE_CRASH_BUF_SIZE 300       // ~10s at 30Hz downsampled

// ─── Batch / JSON ────────────────────────────────────────────────────────────
#define BATCH_SIZE         50        // Reduced for reliability during bench testing
#define JSON_DOC_CAPACITY  32768     // 32KB is plenty for 100 readings

// ─── SD / Storage ────────────────────────────────────────────────────────────
#define SD_CALIBRATION_PATH "/trak/calibration.json"
#define SD_CONFIG_PATH      "/trak/config.json"
#define SD_LOG_PATH         "/trak/device.log"
#define SD_SESSIONS_PATH    "/trak/sessions"
