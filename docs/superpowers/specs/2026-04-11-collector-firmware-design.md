# Yard Collector Firmware — Design Spec

**Date:** 2026-04-11
**Scope:** LilyGO T-SIM7080G-S3 yard collector firmware — ESP-NOW receive, cellular MQTT publish, OTA relay to hive nodes, self-OTA, daily time sync broadcast.
**Framework:** Arduino via PlatformIO, espressif32 v6.x, TinyGSM
**Architecture:** Modular with central loop — always-on ESP-NOW listener with periodic modem wake for MQTT publish

---

## 1. Hardware: LilyGO T-SIM7080G-S3

| Parameter | Value |
|---|---|
| Core chip | ESP32-S3 |
| Flash | 8 MB (with Octal SPI PSRAM) |
| Cellular modem | SIM7080G — LTE-M + NB-IoT + GPS |
| LTE-M throughput | 300-375 kbps down |
| SIM provider | Hologram — $1/month + negligible data |
| Data usage | < 0.5 MB/month per yard (5 hives, 30-min intervals) |

### Claimed GPIO Pins

| GPIO | Function | Owner |
|---|---|---|
| 4 | Modem RXD | SIM7080G |
| 5 | Modem TXD | SIM7080G |
| 41 | Modem PWRKEY | SIM7080G |
| 3 | Modem RI (ring indicator) | SIM7080G |
| 42 | Modem DTR | SIM7080G |
| 38 | SD CLK | SD card |
| 39 | SD CMD | SD card |
| 40 | SD DATA | SD card |
| 15 | PMU SDA | Power management |
| 7 | PMU SCL | Power management |
| 6 | PMU IRQ | Power management |
| 35-37 | Reserved | Octal SPI PSRAM |

No additional GPIO needed — the collector has no sensors. ESP-NOW and WiFi use the onboard radio, not GPIO.

---

## 2. Project Structure

```
firmware/collector/
├── platformio.ini
├── partitions_ota.csv
├── include/
│   ├── config.h              — T-SIM7080G pin definitions, MQTT config, timing
│   └── types.h               — CollectorState enum, PayloadBuffer
└── src/
    ├── main.cpp              — Main loop: receive → batch → publish → time sync → sleep
    ├── espnow_receiver.cpp/.h — ESP-NOW receive callback, payload buffer
    ├── cellular.cpp/.h        — SIM7080G modem lifecycle via TinyGSM, NTP sync
    ├── mqtt_publisher.cpp/.h  — MQTT connect, batch publish, OTA command subscribe
    ├── ota_relay.cpp/.h       — Download firmware from GitHub, chunk and send to hive nodes
    ├── ota_self.cpp/.h        — Self-OTA: download and apply own firmware
    └── time_sync.cpp/.h       — Broadcast epoch timestamp to hive nodes via ESP-NOW
```

Uses shared headers:
- `firmware/shared/hive_payload.h` — HivePayload struct
- `firmware/shared/ota_protocol.h` — OTA packet types
- `firmware/shared/espnow_protocol.h` — NEW: ESP-NOW packet envelope with type routing

---

## 3. ESP-NOW Protocol Extension

New shared header: `firmware/shared/espnow_protocol.h`

```cpp
enum class EspNowPacketType : uint8_t {
    SENSOR_DATA  = 0x10,   // Node → Collector: HivePayload
    TIME_SYNC    = 0x20,   // Collector → Node: epoch timestamp
    OTA_PACKET   = 0x30    // Either direction: OTA transfer
};

struct EspNowHeader {
    EspNowPacketType type;
    uint8_t          data_len;
} __attribute__((packed));

struct TimeSyncPayload {
    uint32_t epoch_seconds;
} __attribute__((packed));
```

All ESP-NOW packets are wrapped in `EspNowHeader`. The receiver checks `type` and routes to the appropriate handler.

**Breaking change:** The hive node currently sends raw `HivePayload` over ESP-NOW. It needs to wrap payloads in `EspNowHeader` with type `SENSOR_DATA`. This change is tracked as a separate issue against the hive node firmware.

---

## 4. Main Loop & Power States

The collector does not deep sleep — it must stay awake to receive ESP-NOW packets at any time. It uses automatic light sleep between receives (~5-8 mA).

```
┌──────────────────────────────────────────┐
│           LIGHT SLEEP                     │
│  ESP-NOW radio active, CPU idles          │
│  ~5-8 mA                                 │
│                                           │
│  ESP-NOW packet received → buffer it      │
│  30-min timer fires → PUBLISH CYCLE       │
└───────────────────┬──────────────────────┘
                    ▼
┌──────────────────────────────────────────┐
│         PUBLISH CYCLE (~30 sec)          │
│  1. Power on SIM7080G modem              │
│  2. Wait for network registration        │
│  3. NTP sync (update local clock)        │
│  4. Connect MQTT (TLS to HiveMQ Cloud)   │
│  5. Publish all buffered payloads        │
│  6. Subscribe, check for OTA commands    │
│  7. If OTA command → handle it           │
│  8. Disconnect MQTT                      │
│  9. Broadcast TIME_SYNC via ESP-NOW      │
│  10. Power off modem                     │
│  ~150-300 mA during modem-on             │
└───────────────────┬──────────────────────┘
                    ▼
             Back to LIGHT SLEEP
```

### Payload Buffer

- Array of `HivePayload` structs in RAM, indexed by hive_id
- Max 20 entries (one per hive node)
- New payloads from the same hive_id overwrite previous — only latest reading published per cycle
- No flash storage needed — data loss on power cycle is acceptable (next node transmit repopulates)

### Power Budget

| Mode | Current | Duration/day | mAh/day |
|---|---|---|---|
| Light sleep (ESP-NOW listening) | ~5-8 mA | ~23.5 hrs | ~140 mAh |
| Modem on + MQTT publish | ~150-300 mA | 48 × 30 sec = 24 min | ~50 mAh |
| ESP32-S3 active during publish | ~40 mA | 24 min | ~16 mAh |
| **Total** | | | **~206 mAh** |

Requires solar panel (5-10W) for sustained operation. Harvest ~2,500 mAh/day — 12x surplus.

---

## 5. Cellular & MQTT

### Modem Lifecycle (cellular.cpp)

Uses TinyGSM library for SIM7080G abstraction.

```cpp
namespace Cellular {
    bool powerOn();           // PWRKEY pulse sequence, wait for modem ready
    void powerOff();          // Graceful modem shutdown
    bool waitForNetwork();    // Wait for LTE-M/NB-IoT registration
    bool syncNtp();           // NTP time sync, update system clock
    TinyGsm& getModem();     // Access modem instance for MQTT/HTTP
}
```

**Power-on sequence:** The T-SIM7080G requires a specific PWRKEY pulse:
1. Set PWRKEY LOW
2. Wait 1000 ms
3. Set PWRKEY HIGH
4. Wait 5000 ms for modem boot
5. Send AT command, verify response

**NTP sync:** Use modem's AT+CNTP command. Updates the collector's local clock. Called every publish cycle.

### MQTT Publishing (mqtt_publisher.cpp)

Uses SIM7080G's native MQTT AT commands via TinyGSM — TLS terminates on the modem.

```cpp
namespace MqttPublisher {
    bool initialize(TinyGsm& modem);  // Configure MQTT with NVS credentials
    bool connect();                    // Connect to HiveMQ Cloud
    bool publishBatch(const HivePayload* buffer, uint8_t count);  // Publish all buffered payloads
    bool checkOtaCommands();           // Subscribe, read retained OTA messages
    void disconnect();                 // Clean disconnect
}
```

**Topic schema** — maps each HivePayload field to its own MQTT topic per the README:

| Topic | Source Field |
|---|---|
| `hivesense/hive/{id}/weight` | weight_kg |
| `hivesense/hive/{id}/temp/internal` | temp_internal |
| `hivesense/hive/{id}/temp/external` | temp_external |
| `hivesense/hive/{id}/humidity/internal` | humidity_internal |
| `hivesense/hive/{id}/humidity/external` | humidity_external |
| `hivesense/hive/{id}/bees/in` | bees_in |
| `hivesense/hive/{id}/bees/out` | bees_out |
| `hivesense/hive/{id}/bees/activity` | bees_activity |
| `hivesense/hive/{id}/battery` | battery_pct |
| `hivesense/hive/{id}/rssi` | rssi |

10 MQTT publishes per hive per cycle.

**OTA command subscription:** Collector subscribes to `hivesense/ota/start` (retained). Payload format:

```json
{"hive_id": "HIVE-003", "tag": "v1.2.0", "target": "node"}
```

`target` is either `"node"` (relay to hive) or `"collector"` (self-update).

### MQTT Credentials in NVS

| Key | Type | Default | Purpose |
|---|---|---|---|
| `mqtt_host` | string | "" | HiveMQ Cloud endpoint |
| `mqtt_port` | uint16 | 8883 | TLS port |
| `mqtt_user` | string | "" | MQTT username |
| `mqtt_pass` | string | "" | MQTT password |

Configured via serial console on first setup.

---

## 6. OTA Relay (ota_relay.cpp)

Downloads firmware from GitHub Releases over cellular, then relays to hive nodes via ESP-NOW.

```cpp
namespace OtaRelay {
    bool downloadFirmware(const char* tag);        // HTTPS GET from GitHub, save to LittleFS
    bool startRelay(const char* hiveId);           // Begin chunked ESP-NOW transfer to target node
    bool sendNextChunk();                          // Send one chunk, wait for ACK
    bool isRelayInProgress();                      // Check if relay is active
    void abortRelay();                             // Cancel and clean up
}
```

### Download Flow

1. Construct URL: `https://github.com/sjordan0228/hivesense-monitor/releases/download/{tag}/hive-node.bin`
2. HTTPS GET via TinyGSM HTTP client
3. Stream response body to LittleFS file `/ota_relay.bin`
4. Calculate CRC32 during download
5. Close modem after download complete

### Relay Flow

1. On next ESP-NOW contact from target hive node, send `OTA_START` packet with firmware size, chunk count, CRC32
2. Send chunks sequentially, wait for `OTA_ACK` after each
3. If hive node goes to sleep mid-transfer, resume on next wake (node sends `OTA_READY` with last received chunk)
4. After all chunks sent, send `OTA_END`
5. Delete `/ota_relay.bin` from LittleFS

### Storage

Firmware binary stored temporarily in the collector's unused OTA partition (app1, 3.5 MB) rather than LittleFS. This avoids the 960 KB LittleFS size limit and keeps the filesystem free for other use. The collector reads chunks from the stored binary during relay. Only one OTA relay active at a time.

---

## 7. Self-OTA (ota_self.cpp)

Same pattern as hive node OTA but simpler — the collector downloads directly, no relay needed.

```cpp
namespace OtaSelf {
    bool downloadAndApply(const char* tag);  // Download collector firmware, write to OTA partition, reboot
    void validateNewFirmware();              // Health check after OTA boot
}
```

Flow:
1. Construct URL: `https://github.com/sjordan0228/hivesense-monitor/releases/download/{tag}/collector.bin`
2. HTTPS GET, stream directly to OTA partition via `esp_ota_write()` (no LittleFS intermediate)
3. Verify CRC32
4. `esp_ota_set_boot_partition()` + reboot
5. On next boot, `validateNewFirmware()` checks modem responds, ESP-NOW initializes, MQTT connects

---

## 8. Time Sync (time_sync.cpp)

Broadcasts current epoch timestamp to all hive nodes via ESP-NOW after every MQTT publish cycle.

```cpp
namespace TimeSync {
    bool broadcast(uint32_t epochSeconds);  // ESP-NOW broadcast TIME_SYNC packet
}
```

- Uses ESP-NOW broadcast (destination FF:FF:FF:FF:FF:FF)
- All hive nodes in range receive it and update their RTC
- Fires at the end of every publish cycle (after modem NTP sync)
- ~100 µs, negligible power

---

## 9. Dependencies

| Library | Purpose | PlatformIO |
|---|---|---|
| `vshymanskyy/TinyGSM` | SIM7080G modem abstraction | lib_deps |
| `WiFi` | ESP-NOW requires WiFi STA mode | Bundled |
| `esp_now.h` | ESP-NOW receive/send | Bundled |
| `esp_ota_ops.h` | Self-OTA | Bundled |
| `LittleFS` | Temporary firmware binary storage | Bundled |
| `Preferences` | NVS config storage | Bundled |
| `esp_http_client.h` | HTTPS download from GitHub | Bundled |

### Partition Table

Same as hive node — dual OTA + LittleFS on 8 MB flash:

```csv
# Name,     Type, SubType, Offset,   Size,     Flags
nvs,        data, nvs,     0x9000,   0x5000,
otadata,    data, ota,     0xE000,   0x2000,
app0,       app,  ota_0,   0x10000,  0x380000,
app1,       app,  ota_1,   0x390000, 0x380000,
littlefs,   data, spiffs,  0x710000, 0xF0000,
```

---

## 10. Hive Node Changes Required

The following changes to the hive node firmware are required for collector compatibility. These are tracked separately.

1. **ESP-NOW packet wrapping** — Hive node must wrap `HivePayload` in `EspNowHeader` with type `SENSOR_DATA` before sending. Currently sends raw struct.
2. **ESP-NOW receive callback** — Hive node needs a receive callback to handle `TIME_SYNC` packets and call `StateMachine::setTime()`.
3. **OTA packet routing** — Hive node's ESP-NOW receive callback must route `OTA_PACKET` types to `OtaUpdate::handleOtaPacket()`.

These are small changes to `comms_espnow.cpp` on the hive node side.
