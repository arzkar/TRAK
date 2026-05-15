#include "mqtt.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

static WiFiClient    _wifi;
static PubSubClient  _mqtt(_wifi);

// ─── WiFi ─────────────────────────────────────────────────────────────────────
bool connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < WIFI_TIMEOUT_MS) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Failed — entering offline mode");
    return false;
  }
  Serial.printf("[WiFi] Connected. IP: %s  RSSI: %d dBm\n",
                WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

bool wifiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

// ─── MQTT ─────────────────────────────────────────────────────────────────────
static void onMQTTMessage(char* topic, byte* payload, unsigned int len) {
  // Handle commands from backend
  String t(topic);
  if (t.endsWith("/command")) {
    char buf[256];
    size_t n = min((unsigned int)sizeof(buf) - 1, len);
    memcpy(buf, payload, n);
    buf[n] = '\0';
    Serial.printf("[MQTT] Command received: %s\n", buf);
    // TODO: parse and dispatch commands (reboot, calibrate_imu, etc.)
  }
}

bool connectMQTT() {
  if (!wifiConnected()) return false;
  if (_mqtt.connected()) return true;

  _mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  _mqtt.setKeepAlive(MQTT_KEEPALIVE);
  _mqtt.setBufferSize(24576);  // 24KB — enough for 50 readings
  _mqtt.setCallback(onMQTTMessage);

  // Unique client ID per connection
  char clientId[40];
  snprintf(clientId, sizeof(clientId), "trak-%s-%04x",
           DEVICE_ID, (uint16_t)(esp_random() & 0xFFFF));

  Serial.printf("[MQTT] Connecting as %s ...\n", clientId);
  bool ok = _mqtt.connect(clientId);

  if (!ok) {
    Serial.printf("[MQTT] Connect failed, state=%d\n", _mqtt.state());
    return false;
  }

  // Subscribe to command topic
  _mqtt.subscribe(MQTT_COMMAND_TOPIC, 1);
  Serial.println("[MQTT] Connected OK");
  return true;
}

bool mqttConnected() {
  return _mqtt.connected();
}

void mqttLoop() {
  _mqtt.loop();
}

void processMQTTCommands() {
  _mqtt.loop();
}

// ─── Publish helpers ──────────────────────────────────────────────────────────

bool publishBatch(const String& json) {
  if (!mqttConnected()) return false;
  
  // Send raw JSON — RSSI is already in status heartbeats if needed
  bool ok = _mqtt.publish(MQTT_TELEMETRY_TOPIC,
                          (const uint8_t*)json.c_str(), json.length(),
                          false);  // retain=false
  if (!ok) {
    Serial.printf("[MQTT] Batch publish failed. Size: %u, State: %d, Free Heap: %u\n", 
                  json.length(), _mqtt.state(), ESP.getFreeHeap());
  } else {
    Serial.printf("[MQTT] Batch published (%u bytes)\n", json.length());
  }
  return ok;
}

bool publishCrash(const String& json) {
  if (!mqttConnected()) return false;
  return _mqtt.publish(MQTT_CRASH_TOPIC,
                       (const uint8_t*)json.c_str(), json.length(),
                       true);  // retain=true for crash events
}

bool publishStatus(const String& json) {
  if (!mqttConnected()) return false;
  return _mqtt.publish(MQTT_STATUS_TOPIC,
                       (const uint8_t*)json.c_str(), json.length(),
                       false);
}

// ─── Status payload ───────────────────────────────────────────────────────────
String buildStatusPayload(const Session& session, bool imuOk, bool obdOk,
                          uint32_t unsyncedBatches, float sdFreeMb) {
  StaticJsonDocument<512> doc;
  doc["v"]                = 1;
  doc["device_id"]        = DEVICE_ID;
  doc["session_id"]       = session.uuid;
  doc["timestamp"]        = millis();
  doc["uptime_ms"]        = millis();
  doc["wifi_rssi"]        = wifiConnected() ? WiFi.RSSI() : 0;
  doc["free_heap_bytes"]  = (uint32_t)ESP.getFreeHeap();
  doc["sd_free_mb"]       = sdFreeMb;
  doc["unsynced_batches"] = unsyncedBatches;
  doc["obd_connected"]    = obdOk;
  doc["imu_ok"]           = imuOk;
  doc["chip_temp"]        = temperatureRead();
  doc["firmware"]         = FIRMWARE_VERSION;

  String out;
  serializeJson(doc, out);
  return out;
}
