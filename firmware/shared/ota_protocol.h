#pragma once

#include <cstdint>

/// OTA command identifiers for ESP-NOW OTA transfer protocol.
enum class OtaCommand : uint8_t {
    OTA_START = 0x01,   // Collector -> Node: begin transfer
    OTA_CHUNK = 0x02,   // Collector -> Node: firmware data chunk
    OTA_END   = 0x03,   // Collector -> Node: transfer complete
    OTA_ABORT = 0x04,   // Either direction: cancel transfer
    OTA_ACK   = 0x05,   // Node -> Collector: chunk acknowledged
    OTA_READY = 0x06    // Node -> Collector: ready for next chunk
};

/// Max firmware data bytes per ESP-NOW OTA packet.
/// ESP-NOW max payload is 250 bytes. Header uses 6 bytes.
constexpr uint8_t OTA_MAX_CHUNK_DATA = 244;

/// Single OTA packet sent over ESP-NOW.
struct OtaPacket {
    OtaCommand command;
    uint16_t   chunk_index;
    uint16_t   total_chunks;
    uint8_t    data[OTA_MAX_CHUNK_DATA];
    uint8_t    data_len;     // Actual bytes in data[] for this chunk
} __attribute__((packed));

/// Payload inside data[] of an OTA_START packet.
struct OtaStartPayload {
    uint32_t firmware_size;  // Total bytes
    uint16_t total_chunks;
    uint32_t crc32;          // CRC32 of entire firmware
    char     version[16];    // Semantic version string
} __attribute__((packed));

/// NVS keys for OTA progress tracking.
constexpr const char* NVS_KEY_OTA_ACTIVE   = "ota_active";
constexpr const char* NVS_KEY_OTA_TOTAL    = "ota_total";
constexpr const char* NVS_KEY_OTA_RECEIVED = "ota_received";
constexpr const char* NVS_KEY_OTA_CRC32    = "ota_crc32";
constexpr const char* NVS_KEY_OTA_VERSION  = "ota_version";
