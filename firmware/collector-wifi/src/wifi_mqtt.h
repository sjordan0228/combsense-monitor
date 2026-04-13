#pragma once

#include "types.h"
#include <cstdint>

/// WiFi connection and MQTT publishing for the home yard collector.
/// Replaces cellular/TinyGSM used on the LilyGO collector.
namespace WifiMqtt {

    /// Load WiFi and MQTT credentials from NVS.
    bool initialize();

    /// Connect to WiFi. Blocks up to WIFI_CONNECT_TIMEOUT_MS.
    bool connectWifi();

    /// Connect to MQTT broker.
    bool connectMqtt();

    /// Publish all occupied buffer entries. Returns number published.
    uint8_t publishBatch(PayloadBuffer& buffer);

    /// Sync time via NTP. Returns epoch seconds or 0 on failure.
    uint32_t syncNtp();

    /// Disconnect MQTT and WiFi.
    void disconnect();

}  // namespace WifiMqtt
