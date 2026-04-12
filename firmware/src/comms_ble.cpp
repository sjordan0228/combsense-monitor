#include "comms_ble.h"
#include "config.h"
#include "types.h"
#include "storage.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

namespace {

BLEServer*         bleServer        = nullptr;
BLECharacteristic* charSensorLog    = nullptr;
BLECharacteristic* charReadingCount = nullptr;
BLECharacteristic* charHiveId       = nullptr;
BLECharacteristic* charClearLog     = nullptr;

// Written from BLE callback context, read from main task
volatile bool deviceConnected = false;
volatile bool syncComplete    = false;

// ---------------------------------------------------------------------------
// BLE Callbacks
// ---------------------------------------------------------------------------

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        deviceConnected = true;
        syncComplete    = false;
        Serial.println("[BLE] Phone connected");
    }

    void onDisconnect(BLEServer* server) override {
        deviceConnected = false;
        syncComplete    = true;
        Serial.println("[BLE] Phone disconnected");
    }
};

/// Write 0x01 to this characteristic to clear stored readings after sync.
class ClearLogCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        if (value.length() != 1 || value[0] != 0x01) {
            return;
        }

        Serial.println("[BLE] Clear command received — erasing stored readings");
        Storage::clearAllReadings();
        syncComplete = true;

        // Update reading count characteristic so phone sees 0
        uint16_t zero = 0;
        charReadingCount->setValue(reinterpret_cast<uint8_t*>(&zero), sizeof(uint16_t));
    }
};

/// Phone writes hive ID during initial pairing to associate this node.
class HiveIdCallback : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        std::string value = characteristic->getValue();
        if (value.empty() || value.length() >= 16) {
            return;
        }

        Preferences prefs;
        prefs.begin(NVS_NAMESPACE, false);  // read-write
        prefs.putString(NVS_KEY_HIVE_ID, value.c_str());
        prefs.end();

        Serial.printf("[BLE] Hive ID updated to: %s\n", value.c_str());
    }
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Send all stored readings to the connected phone via BLE notifications.
void sendStoredReadings() {
    uint16_t count = Storage::getReadingCount();
    Serial.printf("[BLE] Sending %u readings via notifications\n", count);

    HivePayload payload;
    for (uint16_t i = 0; i < count; i++) {
        if (!Storage::readReading(i, payload)) {
            Serial.printf("[BLE] ERROR: Failed to read index %u\n", i);
            continue;
        }

        charSensorLog->setValue(
            reinterpret_cast<uint8_t*>(&payload),
            sizeof(HivePayload)
        );
        charSensorLog->notify();

        // Small delay between notifications to avoid BLE congestion
        delay(20);
    }

    Serial.println("[BLE] All readings sent");
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace CommsBle {

bool initialize() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    String hiveId = prefs.getString(NVS_KEY_HIVE_ID, "HIVE-001");
    prefs.end();

    String deviceName = "HiveSense-" + hiveId;

    BLEDevice::init(deviceName.c_str());
    BLEDevice::setMTU(512);

    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new ServerCallbacks());

    BLEService* service = bleServer->createService(BLE_SERVICE_UUID);

    // Sensor Log — bulk transfer of stored readings via notify
    charSensorLog = service->createCharacteristic(
        BLE_CHAR_SENSOR_LOG,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    charSensorLog->addDescriptor(new BLE2902());

    // Reading Count — phone reads this to know how many readings to expect
    charReadingCount = service->createCharacteristic(
        BLE_CHAR_READING_COUNT,
        BLECharacteristic::PROPERTY_READ
    );
    uint16_t count = Storage::getReadingCount();
    charReadingCount->setValue(reinterpret_cast<uint8_t*>(&count), sizeof(uint16_t));

    // Hive ID — readable for identification, writable for pairing
    charHiveId = service->createCharacteristic(
        BLE_CHAR_HIVE_ID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE
    );
    charHiveId->setCallbacks(new HiveIdCallback());
    charHiveId->setValue(hiveId.c_str());

    // Clear Log — phone writes 0x01 after confirming complete download
    charClearLog = service->createCharacteristic(
        BLE_CHAR_CLEAR_LOG,
        BLECharacteristic::PROPERTY_WRITE
    );
    charClearLog->setCallbacks(new ClearLogCallback());

    service->start();

    Serial.printf("[BLE] GATT server initialized — %s (%u readings available)\n",
                  deviceName.c_str(), count);
    return true;
}

bool advertiseAndWait(uint16_t timeoutMs) {
    deviceConnected = false;
    syncComplete    = false;

    BLEAdvertising* advertising = BLEDevice::getAdvertising();
    advertising->addServiceUUID(BLE_SERVICE_UUID);
    advertising->setScanResponse(true);
    advertising->setMinPreferred(0x06);
    advertising->start();

    Serial.printf("[BLE] Advertising for %u ms\n", timeoutMs);

    uint32_t deadline = millis() + timeoutMs;
    while (!deviceConnected && millis() < deadline) {
        delay(100);
    }

    if (!deviceConnected) {
        advertising->stop();
        Serial.println("[BLE] No connection — advertising stopped");
        return false;
    }

    // Phone connected — send stored readings
    sendStoredReadings();
    return true;
}

void waitForSyncComplete() {
    Serial.println("[BLE] Waiting for sync to complete...");

    while (!syncComplete) {
        delay(100);
    }
}

void shutdown() {
    BLEDevice::deinit(true);  // true = release memory
    bleServer        = nullptr;
    charSensorLog    = nullptr;
    charReadingCount = nullptr;
    charHiveId       = nullptr;
    charClearLog     = nullptr;

    Serial.println("[BLE] Shutdown — stack deinitialized");
}

}  // namespace CommsBle
