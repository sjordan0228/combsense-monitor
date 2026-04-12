#pragma once

#include <cstdint>
#include "hive_payload.h"

/// Operating states for the hive node state machine.
enum class NodeState : uint8_t {
    BOOT,
    DAYTIME_IDLE,
    NIGHTTIME_SLEEP,
    SENSOR_READ,
    ESPNOW_TRANSMIT,
    BLE_CHECK,
    BLE_SYNC,
    OTA_RECEIVE
};

/// Metadata for the LittleFS circular buffer.
struct StorageMeta {
    uint16_t head;     // Index of next write position
    uint16_t count;    // Number of valid readings stored
    uint8_t  version;  // Storage format version
} __attribute__((packed));
