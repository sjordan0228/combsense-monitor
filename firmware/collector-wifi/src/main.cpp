#include <Arduino.h>
#include "config.h"
#include "types.h"
#include "espnow_receiver.h"
#include "wifi_mqtt.h"
#include "time_sync.h"
#include "serial_console.h"
#include <Preferences.h>

namespace {

uint32_t lastPublishTime = 0;
const uint32_t publishIntervalMs = static_cast<uint32_t>(PUBLISH_INTERVAL_MIN) * 60UL * 1000UL;

void runPublishCycle() {
    Serial.println("[MAIN] === PUBLISH CYCLE ===");

    if (!WifiMqtt::connectWifi()) {
        return;
    }

    uint32_t epoch = WifiMqtt::syncNtp();

    if (WifiMqtt::connectMqtt()) {
        PayloadBuffer& buffer = EspNowReceiver::getBuffer();
        uint8_t count = WifiMqtt::publishBatch(buffer);
        Serial.printf("[MAIN] Published %u hive payloads\n", count);
        buffer.clear();
    }

    WifiMqtt::disconnect();

    // Time sync broadcast after WiFi is down (ESP-NOW needs WiFi STA)
    // Re-init ESP-NOW for broadcast
    if (epoch > 0) {
        TimeSync::broadcast(epoch);
    }

    Serial.println("[MAIN] Publish cycle complete");
}

}  // anonymous namespace

void setup() {
    Serial.begin(115200);
    delay(3000);
    Serial.println("\n[MAIN] HiveSense WiFi Collector — ESP32-S3-Zero");

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.end();

    SerialConsole::checkForConsole();

    EspNowReceiver::initialize();
    WifiMqtt::initialize();

    lastPublishTime = millis();
    Serial.println("[MAIN] Ready — listening for ESP-NOW packets");
}

void loop() {
    if (millis() - lastPublishTime >= publishIntervalMs) {
        runPublishCycle();
        lastPublishTime = millis();
    }

    delay(10);
}
