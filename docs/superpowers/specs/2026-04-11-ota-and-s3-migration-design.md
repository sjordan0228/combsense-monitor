# OTA Update System & ESP32-S3 Migration — Design Spec

**Date:** 2026-04-11
**Scope:** Migrate hive node from ESP32-WROOM-32 to Freenove ESP32-S3 Lite (8MB flash), add OTA update capability via collector relay, restructure firmware directory for multi-target builds.
**Modifies:** Phase 1 hive node firmware

---

## 1. Hardware Change: ESP32-S3

### Why
- 8 MB flash enables dual OTA partitions with ample headroom
- BLE 5.0 improves throughput for sensor sync and OTA
- Native USB eliminates UART chip (lower sleep current on production boards)
- Same ESP-NOW compatibility with T-SIM7080G collector (also S3-based)
- Freenove ESP32-S3 Lite is readily available at ~$7/board

### Board: Freenove ESP32-S3 Lite
- Module: ESP32-S3-WROOM (8 MB flash, 8 MB PSRAM)
- USB: Native USB-C (no external UART)
- Onboard RGB LED: GPIO 48 (WS2812 — disabled at boot)
- PlatformIO board: `esp32-s3-devkitc-1`

### PlatformIO Config Changes
```ini
[env:esp32s3]
platform = espressif32@^6.5.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.flash_size = 8MB
board_build.partitions = partitions_ota.csv
board_build.filesystem = littlefs
monitor_speed = 115200

lib_deps =
    adafruit/Adafruit SHT31 Library@^2.2.2
    bogde/HX711@^0.7.5
    h2zero/NimBLE-Arduino@^1.4.0

build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DCONFIG_PM_ENABLE
    -DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL
    -DCONFIG_BT_NIMBLE_ROLE_BROADCASTER
    -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=0
    -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=0
    -DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
```

---

## 2. GPIO Pin Map (Freenove ESP32-S3 Lite)

Pins are ordered to minimize wire crossing when connecting sensors. Adjacent pins are grouped by subsystem.

Constraints: GPIO 26-32 reserved for flash/PSRAM. GPIO 0, 3, 45, 46 are strapping pins — avoided.

| GPIO | Assignment | Direction | Notes |
|---|---|---|---|
| **I2C Bus (SHT31 x2)** | | | |
| 8 | SDA | Bidirectional | S3 default I2C SDA |
| 9 | SCL | Output | S3 default I2C SCL |
| **HX711 Weight** | | | |
| 10 | HX711_DOUT | Input | Adjacent to CLK for clean 2-wire run |
| 11 | HX711_CLK | Output | |
| 12 | MOSFET_HX711 | Output | N-channel gate — next to HX711 pins |
| **Battery** | | | |
| 1 | BATTERY_ADC | Input (ADC1_CH0) | ADC1 — works with WiFi active |
| **Status** | | | |
| 48 | ONBOARD_RGB | Output | WS2812 — set LOW at boot to disable |
| **Phase 2 — IR Array (reserved)** | | | |
| 4 | MUX_S0 | Output | Address lines grouped sequentially |
| 5 | MUX_S1 | Output | |
| 6 | MUX_S2 | Output | |
| 7 | MUX_S3 | Output | |
| 13 | MUX_EN_TX | Output | Enable lines adjacent |
| 14 | MUX_EN_RX | Output | |
| 15 | MUX_SIG_TX | Output | Signal lines together |
| 2 | MUX_SIG_RX | Input (ADC1_CH1) | ADC1, digital read with internal pull-up |
| 16 | MOSFET_IR | Output | Gate near signal pins |

### Pin Selection Rationale
- GPIO 1 for battery ADC — ADC1, no WiFi conflict
- GPIO 2 for IR RX — ADC1, internal pull-up for digital beam-break detection
- GPIO 4-7 for mux address — sequential bus, clean wiring
- GPIO 8-9 for I2C — S3 hardware default
- GPIO 10-12 for HX711 group — physically adjacent on header
- GPIO 13-16 for IR mux — grouped after HX711
- GPIO 48 for onboard RGB — disabled to save power

---

## 3. Directory Restructure

```
firmware/
├── hive-node/              — ESP32-S3 hive node firmware
│   ├── platformio.ini
│   ├── partitions_ota.csv
│   ├── include/
│   │   ├── config.h        — S3 pin definitions, timing, BLE UUIDs, NVS keys
│   │   ├── types.h         — NodeState enum, StorageMeta
│   │   └── module.h        — Module interface convention
│   └── src/
│       ├── main.cpp
│       ├── state_machine.cpp/.h
│       ├── power_manager.cpp/.h
│       ├── sensor_sht31.cpp/.h
│       ├── sensor_hx711.cpp/.h
│       ├── comms_espnow.cpp/.h
│       ├── comms_ble.cpp/.h
│       ├── storage.cpp/.h
│       ├── battery.cpp/.h
│       └── ota_update.cpp/.h   — NEW: OTA receive module
├── collector/              — LilyGO T-SIM7080G (future)
│   ├── platformio.ini
│   └── src/
└── shared/                 — Headers used by both firmwares
    ├── hive_payload.h      — HivePayload struct
    └── ota_protocol.h      — OTA chunk format, command IDs
```

### Shared Headers
`HivePayload` moves from `hive-node/include/types.h` to `shared/hive_payload.h`. Both firmwares include it via relative path. This prevents struct drift between node and collector.

`ota_protocol.h` defines the OTA wire format used by both sides.

---

## 4. Custom Partition Table (8 MB Flash)

File: `hive-node/partitions_ota.csv`

```csv
# Name,     Type, SubType, Offset,   Size,     Flags
nvs,        data, nvs,     0x9000,   0x5000,
otadata,    data, ota,     0xE000,   0x2000,
app0,       app,  ota_0,   0x10000,  0x380000,
app1,       app,  ota_1,   0x390000, 0x380000,
littlefs,   data, spiffs,  0x710000, 0xF0000,
```

| Partition | Size | Purpose |
|---|---|---|
| nvs | 20 KB | Config, calibration, OTA progress |
| otadata | 8 KB | Tracks active OTA slot |
| app0 | 3.5 MB | Primary firmware |
| app1 | 3.5 MB | OTA update target |
| littlefs | 960 KB | Sensor readings (~20,000 entries) |
| **Total** | **8 MB** | |

Current app: 1.1 MB. Headroom per slot: 2.4 MB.

---

## 5. OTA Update System

### Architecture

```
iOS App ──MQTT──→ HiveMQ Cloud ──MQTT──→ Yard Collector ──ESP-NOW──→ Hive Node
   │                                          │                        │
   │  "update hive-3 to v1.2.0"              │  downloads .bin        │  receives chunks
   │                                          │  from GitHub           │  writes to OTA
   │                                          │  Releases              │  partition
   │                                          │                        │  reboots
```

### Trigger Flow
1. You push a tagged release to GitHub with the `.bin` firmware artifact
2. In the iOS app, you select a hive and tap "Update Firmware" with the release tag
3. App publishes MQTT message to `hivesense/ota/start` with payload: `{ "hive_id": "HIVE-003", "tag": "v1.2.0" }`
4. Collector subscribes, downloads the `.bin` from the GitHub release URL over cellular
5. Collector begins chunked ESP-NOW transfer to the target hive node

### ESP-NOW OTA Protocol

Defined in `shared/ota_protocol.h`:

```cpp
enum class OtaCommand : uint8_t {
    OTA_START = 0x01,   // Collector → Node: begin OTA, includes total size + chunk count
    OTA_CHUNK = 0x02,   // Collector → Node: one chunk of firmware data
    OTA_END   = 0x03,   // Collector → Node: transfer complete, includes CRC32
    OTA_ABORT = 0x04,   // Either direction: cancel OTA
    OTA_ACK   = 0x05,   // Node → Collector: acknowledge chunk received
    OTA_READY = 0x06    // Node → Collector: ready for next chunk (after wake)
};

struct OtaPacket {
    OtaCommand command;
    uint16_t   chunk_index;
    uint16_t   total_chunks;
    uint8_t    data[244];    // ESP-NOW max 250 - 6 byte header
    uint8_t    data_len;     // Actual bytes in data[] for this chunk
} __attribute__((packed));

struct OtaStartPayload {
    uint32_t firmware_size;  // Total bytes
    uint16_t total_chunks;
    uint32_t crc32;          // CRC32 of entire firmware
    char     version[16];    // Semantic version string
} __attribute__((packed));
```

### OTA Receive Module (Hive Node)

New file: `ota_update.cpp/.h`

```cpp
namespace OtaUpdate {
    /// Check if an OTA transfer is in progress (from NVS).
    bool isTransferInProgress();

    /// Handle an incoming OTA packet from the collector.
    /// Called from the ESP-NOW receive callback.
    bool handleOtaPacket(const OtaPacket& packet);

    /// Resume a partial transfer after sleep.
    /// Returns true if OTA is active and node should signal OTA_READY.
    bool resumeTransfer();

    /// Abort the current transfer, clear NVS progress.
    void abortTransfer();

    /// Run health checks after OTA boot. Mark valid or rollback.
    void validateNewFirmware();
}
```

### Resumable Transfer Across Sleep Cycles

OTA progress stored in NVS:

| Key | Type | Default | Purpose |
|---|---|---|---|
| `ota_active` | bool | false | OTA transfer in progress |
| `ota_total` | uint16 | 0 | Total chunks expected |
| `ota_received` | uint16 | 0 | Last chunk successfully written |
| `ota_crc32` | uint32 | 0 | Expected CRC32 of complete firmware |
| `ota_version` | string | "" | Version being installed |

Flow per wake cycle during OTA:
1. Node wakes, checks `ota_active` in NVS
2. If active: sends `OTA_READY` with `ota_received` to collector via ESP-NOW
3. Collector resumes sending from `ota_received + 1`
4. Node writes chunks to OTA partition, updates `ota_received` in NVS periodically (every 50 chunks to reduce flash wear)
5. Node goes back to sleep after timeout or batch complete
6. Repeats until all chunks received

### Transfer Timing Estimate

- Firmware size: ~1.1 MB = ~4,500 chunks at 244 bytes each
- ESP-NOW throughput with ACK: ~100 chunks/second
- Per wake cycle (30 sec active window): ~3,000 chunks
- **Full transfer: 1-2 wake cycles (~30-60 minutes)**

---

## 6. Rollback & Validation

After booting into new firmware, the node must prove it's healthy before the update is permanent.

### Validation Sequence
1. Check `esp_ota_get_state_info()` — if state is `ESP_OTA_IMG_PENDING_VERIFY`, this is a fresh OTA boot
2. Run health checks within 60 seconds:
   - SHT31 responds on I2C (both addresses)
   - HX711 responds to `wait_ready_timeout`
   - LittleFS mounts successfully
   - ESP-NOW sends at least one packet (ACK or sensor data)
3. All pass: `esp_ota_mark_app_valid_cancel_rollback()`
4. Any fail: `esp_ota_mark_app_invalid_rollback_and_reboot()` — reverts automatically

### State Machine Integration
The state machine gets a new state: `NodeState::OTA_RECEIVE`. When `ota_active` is true in NVS, the BOOT state transitions to `OTA_RECEIVE` instead of `SENSOR_READ`. Normal sensor reads still happen — OTA runs after the sensor/ESP-NOW cycle, sharing the radio window.

---

## 7. Scope — What Gets Built Now vs Later

### This Implementation (Phase 1 addition)
- Directory restructure (`firmware/` → `firmware/hive-node/` + `firmware/shared/`)
- ESP32-S3 migration (config.h pin map, platformio.ini board change)
- Custom 8 MB OTA partition table
- Disable onboard RGB LED
- OTA receive module on hive node
- `shared/hive_payload.h` and `shared/ota_protocol.h`
- Rollback validation in state machine

### Deferred (collector firmware)
- OTA relay module on collector
- GitHub release download over cellular
- MQTT command subscription for OTA triggers
- iOS app "Update Firmware" UI

The hive node OTA receive module can be tested independently using a second ESP32-S3 acting as a mock collector that sends chunked firmware over ESP-NOW.

---

## 8. Dependencies

| Library | Purpose | Source |
|---|---|---|
| `esp_ota_ops.h` | OTA partition management | Bundled with ESP-IDF |
| `esp_partition.h` | Partition table access | Bundled with ESP-IDF |
| `esp_crc.h` or `rom/crc.h` | CRC32 validation | Bundled with ESP-IDF |
| All existing Phase 1 libs | Unchanged | See Phase 1 spec |
