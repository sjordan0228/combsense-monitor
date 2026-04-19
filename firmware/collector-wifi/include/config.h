#pragma once

#include <cstdint>

// =============================================================================
// WiFi Collector — ESP32-S3-Zero
// Home yard variant: uses WiFi instead of cellular for MQTT.
// =============================================================================

// =============================================================================
// Timing
// =============================================================================

constexpr uint8_t  PUBLISH_INTERVAL_MIN    = 30;
constexpr uint16_t MQTT_CONNECT_TIMEOUT_MS = 10000;
constexpr uint16_t WIFI_CONNECT_TIMEOUT_MS = 15000;

// =============================================================================
// Buffer
// =============================================================================

constexpr uint8_t MAX_HIVE_NODES = 20;

// =============================================================================
// NVS Keys
// =============================================================================

constexpr const char* NVS_NAMESPACE      = "combsense";
constexpr const char* NVS_KEY_WIFI_SSID  = "wifi_ssid";
constexpr const char* NVS_KEY_WIFI_PASS  = "wifi_pass";
constexpr const char* NVS_KEY_MQTT_HOST  = "mqtt_host";
constexpr const char* NVS_KEY_MQTT_PORT  = "mqtt_port";
constexpr const char* NVS_KEY_MQTT_USER  = "mqtt_user";
constexpr const char* NVS_KEY_MQTT_PASS  = "mqtt_pass";

// =============================================================================
// MQTT Topics
// =============================================================================

constexpr const char* MQTT_TOPIC_PREFIX = "combsense/hive/";
constexpr const char* MQTT_OTA_TOPIC    = "combsense/ota/start";
