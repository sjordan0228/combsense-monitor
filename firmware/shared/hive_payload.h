#pragma once

#include <cstdint>

/// Sensor payload transmitted via ESP-NOW and stored in LittleFS.
/// Shared between hive node and collector firmware.
struct HivePayload {
    uint8_t  version;            // Payload format version
    char     hive_id[16];        // Null-terminated hive identifier
    uint32_t timestamp;          // Unix epoch seconds
    float    weight_kg;
    float    temp_internal;      // Celsius — SHT31 at 0x44
    float    temp_external;      // Celsius — SHT31 at 0x45
    float    humidity_internal;  // %RH
    float    humidity_external;  // %RH
    uint16_t bees_in;            // Phase 2 — zeroed in Phase 1
    uint16_t bees_out;           // Phase 2 — zeroed in Phase 1
    uint16_t bees_activity;      // Phase 2 — zeroed in Phase 1
    uint8_t  battery_pct;        // 0-100
    int8_t   rssi;               // ESP-NOW signal strength
} __attribute__((packed));
