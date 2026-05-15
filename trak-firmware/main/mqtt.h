#pragma once
#include <Arduino.h>
#include "config.h"
#include "batch.h"

// Connect to WiFi. Returns true if connected within WIFI_TIMEOUT_MS.
bool connectWiFi();

// Connect/reconnect to MQTT broker. Call in mqtt task loop.
bool connectMQTT();

// Publish a batch. Updates wifi_rssi in the payload before publishing.
// Returns true on success.
bool publishBatch(const String& json);

// Publish a crash event.
bool publishCrash(const String& json);

// Publish a status heartbeat.
bool publishStatus(const String& json);

// Subscribe to command topic and process any pending commands.
void processMQTTCommands();

// Must be called regularly to keep connection alive (mqtt.loop()).
void mqttLoop();

bool wifiConnected();
bool mqttConnected();

// Build status JSON payload.
String buildStatusPayload(const Session& session, bool imuOk, bool obdOk,
                          uint32_t unsyncedBatches, float sdFreeMb);
