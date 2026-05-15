// TRAK Firmware — main.ino
// Telemetry, Real-time Analytics & Kinematics
// Honda H'ness CB350 · ESP32 · Option B (raw passthrough, no parsing on device)

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "config.h"
#include "imu.h"
#include "obd.h"
#include "storage.h"
#include "batch.h"
#include "mqtt.h"

// ─── Queues ───────────────────────────────────────────────────────────────────
// imu_task → sd_task
static QueueHandle_t imu_queue;      // capacity: 50 × IMURaw
// obd_task → sd_task
static QueueHandle_t obd_queue;      // capacity: 10 × OBDRaw
// sd_task → mqtt_task (heap-allocated JSON strings, free after publish)
static QueueHandle_t mqtt_queue;     // capacity: 5 × char*

// ─── Shared session state ─────────────────────────────────────────────────────
static Session          g_session;
static SemaphoreHandle_t g_sessionMux;   // protects g_session reads
static volatile uint32_t g_unsyncedBatches = 0;

// ─── IMU Task — Core 1, Priority 3 ───────────────────────────────────────────
// Samples MPU6050 at 10ms (100Hz). Pushes to imu_queue.
static void imuTask(void* param) {
  TickType_t lastWake = xTaskGetTickCount();
  while (true) {
    IMURaw r;
    readIMU(r);

    // Crash detection — g_total threshold only (Option B exception)
    float gTotal = computeGTotal(r);
    if (gTotal > CRASH_G_THRESHOLD && imuAvailable()) {
      Serial.printf("[CRASH] g_total=%.2f — publishing crash event\n", gTotal);
      char crashJson[512];
      snprintf(crashJson, sizeof(crashJson),
        "{\"v\":1,\"device_id\":\"%s\",\"session_id\":\"%s\","
        "\"crash_timestamp\":%lu,"
        "\"raw_imu_at_crash\":{\"ax\":%.4f,\"ay\":%.4f,\"az\":%.4f,"
        "\"gx\":%.4f,\"gy\":%.4f,\"gz\":%.4f},"
        "\"g_total_at_crash\":%.4f}",
        DEVICE_ID, g_session.uuid, millis(),
        r.ax, r.ay, r.az, r.gx, r.gy, r.gz, gTotal);
      String* cs = new String(crashJson);
      *cs = String("CRASH:") + *cs;
      xQueueSend(mqtt_queue, &cs, 0);
    }

    xQueueSend(imu_queue, &r, 0);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(IMU_SAMPLE_MS));
  }
}

// ─── OBD Task — Core 0, Priority 2 ───────────────────────────────────────────
static void obdTask(void* param) {
  TickType_t   lastWake     = xTaskGetTickCount();
  unsigned long lastRetry   = 0;

  while (true) {
    if (!obdAvailable() && millis() - lastRetry > OBD_RETRY_INTERVAL_MS) {
      retryOBD();
      lastRetry = millis();
    }

    OBDRaw r;
    uint32_t offset = millis() - g_session.startMs;
    pollOBD(r, offset);
    xQueueSend(obd_queue, &r, 0);

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(OBD_POLL_MS));
  }
}

// ─── SD/Batch Task — Core 1, Priority 2 ──────────────────────────────────────
static void sdTask(void* param) {
  static Batch    batch;
  static uint32_t batchIndex = 0;
  static IMURaw   latestIMU  = {};
  bool            hasIMU     = false;

  batchInit(batch, batchIndex++, millis());

  while (true) {
    // 1. Drain IMU queue
    IMURaw imuR;
    while (xQueueReceive(imu_queue, &imuR, 0) == pdTRUE) {
      latestIMU = imuR;
      hasIMU    = true;
    }

    // 2. Drain OBD queue and add to batch
    OBDRaw obdR;
    while (xQueueReceive(obd_queue, &obdR, 0) == pdTRUE) {
      if (!hasIMU) {
        memset(&latestIMU, 0, sizeof(latestIMU));
        latestIMU.t = obdR.t;
      }
      batchAdd(batch, latestIMU, obdR);
      
      // 3. Finalise when batch is full
      if (batchFull(batch)) {
        String json = batchSerialise(batch, g_session);
        Serial.printf("[TRAK] Finalised batch %u (%u readings)\n", batch.index, batch.count);

        // Backup to SD if available
        if (storageAvailable()) {
          writeBatchToSD(g_session.dir, batch.index, json);
        }

        // Push to MQTT queue
        String* ptr = new String(json);
        if (xQueueSend(mqtt_queue, &ptr, 0) != pdTRUE) {
          delete ptr;
          Serial.println("[TRAK] mqtt_queue full, batch dropped");
        } else {
          g_unsyncedBatches++;
        }

        batchInit(batch, batchIndex++, millis());
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ─── MQTT Task — Core 0, Priority 1 ──────────────────────────────────────────
static void mqttTask(void* param) {
  unsigned long lastStatus = 0;

  while (true) {
    if (!wifiConnected())  connectWiFi();
    if (wifiConnected() && !mqttConnected()) {
      if (connectMQTT()) {
        replayUnsynced(g_session.dir, [](const String& json) {
          bool ok = publishBatch(json);
          if (ok) g_unsyncedBatches = (g_unsyncedBatches > 0) ? g_unsyncedBatches - 1 : 0;
          return ok;
        });
      }
    }

    mqttLoop();

    String* ptr = nullptr;
    while (xQueueReceive(mqtt_queue, &ptr, 0) == pdTRUE) {
      if (ptr->startsWith("CRASH:")) {
        String crashJson = ptr->substring(6);
        publishCrash(crashJson);
      } else {
        if (publishBatch(*ptr)) {
          g_unsyncedBatches = (g_unsyncedBatches > 0) ? g_unsyncedBatches - 1 : 0;
        }
      }
      delete ptr;
      ptr = nullptr;
    }

    if (millis() - lastStatus >= STATUS_INTERVAL_MS) {
      String status = buildStatusPayload(
        g_session, imuAvailable(), obdAvailable(),
        g_unsyncedBatches, sdFreeMB());
      publishStatus(status);
      lastStatus = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== TRAK Firmware " FIRMWARE_VERSION " ===");

  initStorage();
  initIMU();
  initOBD(); 

  connectWiFi();
  connectMQTT();

  g_sessionMux = xSemaphoreCreateMutex();
  g_session    = createSession();

  imu_queue  = xQueueCreate(50, sizeof(IMURaw));
  obd_queue  = xQueueCreate(10, sizeof(OBDRaw));
  mqtt_queue = xQueueCreate(5,  sizeof(String*));

  xTaskCreatePinnedToCore(imuTask,  "imu_task",  2048, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(obdTask,  "obd_task",  4096, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(sdTask,   "sd_task",   8192, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(mqttTask, "mqtt_task", 8192, nullptr, 1, nullptr, 0);

  Serial.println("[TRAK] All tasks started");
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
