# OTA Update & ESP32-S3 Migration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate hive node firmware from ESP32-WROOM-32 to Freenove ESP32-S3 Lite (8MB flash), restructure for multi-firmware repo, and add OTA update receive capability.

**Architecture:** Directory restructure moves existing code under `firmware/hive-node/`, extracts shared types to `firmware/shared/`. ESP32-S3 migration updates pin definitions, board config, and partition table. OTA module uses `esp_ota_ops` to receive chunked firmware over ESP-NOW with NVS-based progress tracking for resumable transfers across sleep cycles.

**Tech Stack:** PlatformIO, Arduino framework, espressif32 v6.x, ESP32-S3, NimBLE, esp_ota_ops, LittleFS

**Spec:** `docs/superpowers/specs/2026-04-11-ota-and-s3-migration-design.md`

**Ollama delegation:** Use Ollama (http://192.168.1.16:11434, qwen3-coder:30b) for Tasks 2, 3, 4 (boilerplate config/structs). Review all output before committing.

---

## File Map

### Moved (from `firmware/` to `firmware/hive-node/`)
- `platformio.ini` → `firmware/hive-node/platformio.ini` (rewritten for S3)
- `include/config.h` → `firmware/hive-node/include/config.h` (rewritten for S3 pins)
- `include/types.h` → `firmware/hive-node/include/types.h` (NodeState + StorageMeta only)
- `include/module.h` → `firmware/hive-node/include/module.h` (unchanged)
- `src/*.cpp` + `src/*.h` → `firmware/hive-node/src/` (include paths updated)

### New
- `firmware/hive-node/partitions_ota.csv` — 8MB OTA partition table
- `firmware/shared/hive_payload.h` — HivePayload struct (extracted from types.h)
- `firmware/shared/ota_protocol.h` — OTA command enum and packet structs
- `firmware/hive-node/src/ota_update.h` — OTA receive module header
- `firmware/hive-node/src/ota_update.cpp` — OTA receive module implementation
- `firmware/collector/` — empty directory placeholder

### Modified
- All `.cpp` files — include path updates for new directory structure
- `firmware/hive-node/src/state_machine.cpp` — add OTA_RECEIVE state
- `firmware/hive-node/src/state_machine.h` — add OTA_RECEIVE to enum
- `firmware/hive-node/include/types.h` — move HivePayload out, add OTA_RECEIVE to NodeState
- `firmware/hive-node/src/power_manager.cpp` — disable onboard RGB LED
- `firmware/hive-node/src/comms_espnow.cpp` — add OTA packet receive callback

---

## Task 1: Directory Restructure

**Files:**
- Move: all files from `firmware/` to `firmware/hive-node/`
- Create: `firmware/collector/` (empty placeholder)
- Create: `firmware/shared/` directory

- [ ] **Step 1: Create new directory structure**

```bash
mkdir -p firmware/hive-node/include firmware/hive-node/src firmware/shared firmware/collector
```

- [ ] **Step 2: Move existing files**

```bash
# Move include files
git mv firmware/include/config.h firmware/hive-node/include/config.h
git mv firmware/include/types.h firmware/hive-node/include/types.h
git mv firmware/include/module.h firmware/hive-node/include/module.h

# Move source files
git mv firmware/src/main.cpp firmware/hive-node/src/main.cpp
git mv firmware/src/state_machine.cpp firmware/hive-node/src/state_machine.cpp
git mv firmware/src/state_machine.h firmware/hive-node/src/state_machine.h
git mv firmware/src/power_manager.cpp firmware/hive-node/src/power_manager.cpp
git mv firmware/src/power_manager.h firmware/hive-node/src/power_manager.h
git mv firmware/src/sensor_sht31.cpp firmware/hive-node/src/sensor_sht31.cpp
git mv firmware/src/sensor_sht31.h firmware/hive-node/src/sensor_sht31.h
git mv firmware/src/sensor_hx711.cpp firmware/hive-node/src/sensor_hx711.cpp
git mv firmware/src/sensor_hx711.h firmware/hive-node/src/sensor_hx711.h
git mv firmware/src/comms_espnow.cpp firmware/hive-node/src/comms_espnow.cpp
git mv firmware/src/comms_espnow.h firmware/hive-node/src/comms_espnow.h
git mv firmware/src/comms_ble.cpp firmware/hive-node/src/comms_ble.cpp
git mv firmware/src/comms_ble.h firmware/hive-node/src/comms_ble.h
git mv firmware/src/storage.cpp firmware/hive-node/src/storage.cpp
git mv firmware/src/storage.h firmware/hive-node/src/storage.h
git mv firmware/src/battery.cpp firmware/hive-node/src/battery.cpp
git mv firmware/src/battery.h firmware/hive-node/src/battery.h

# Move platformio.ini
git mv firmware/platformio.ini firmware/hive-node/platformio.ini
```

- [ ] **Step 3: Update include paths in all .cpp files**

Every `.cpp` file that includes `"config.h"` or `"types.h"` currently uses relative paths. After the move, headers in `include/` are still one level up from `src/`, so `#include "config.h"` still resolves via PlatformIO's include path. No changes needed for same-directory includes.

The one file that uses `"../include/config.h"` is `comms_espnow.cpp` — update to `"config.h"`:

In `firmware/hive-node/src/comms_espnow.cpp`, change:
```cpp
#include "../include/config.h"
#include "../include/types.h"
```
to:
```cpp
#include "config.h"
#include "types.h"
```

- [ ] **Step 4: Add placeholder for collector**

Create `firmware/collector/.gitkeep`:
```bash
touch firmware/collector/.gitkeep
```

- [ ] **Step 5: Update platformio.ini include path for shared headers**

In `firmware/hive-node/platformio.ini`, add the shared directory to the build include path:

Add to `build_flags`:
```
-I../shared
```

- [ ] **Step 6: Verify build**

Run: `cd firmware/hive-node && pio run`
Expected: BUILD SUCCESS (still targeting esp32dev at this point — S3 switch is Task 3)

- [ ] **Step 7: Commit**

```bash
git add -A firmware/
git commit -m "refactor: restructure firmware directory for multi-target builds"
```

---

## Task 2: Extract Shared Headers

**Files:**
- Create: `firmware/shared/hive_payload.h`
- Create: `firmware/shared/ota_protocol.h`
- Modify: `firmware/hive-node/include/types.h` — remove HivePayload, add include

- [ ] **Step 1: Create `firmware/shared/hive_payload.h`**

```cpp
#pragma once

#include <cstdint>

/// Sensor payload transmitted via ESP-NOW and stored in LittleFS.
/// Shared between hive node and collector firmware.
/// All fields populated during SENSOR_READ state.
/// Phase 2 bee count fields are zeroed in Phase 1.
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
```

- [ ] **Step 2: Create `firmware/shared/ota_protocol.h`**

```cpp
#pragma once

#include <cstdint>

/// OTA command identifiers for ESP-NOW OTA transfer protocol.
/// Used by both hive node (receiver) and collector (sender).
enum class OtaCommand : uint8_t {
    OTA_START = 0x01,   // Collector → Node: begin transfer
    OTA_CHUNK = 0x02,   // Collector → Node: firmware data chunk
    OTA_END   = 0x03,   // Collector → Node: transfer complete
    OTA_ABORT = 0x04,   // Either direction: cancel transfer
    OTA_ACK   = 0x05,   // Node → Collector: chunk acknowledged
    OTA_READY = 0x06    // Node → Collector: ready for next chunk
};

/// Maximum firmware data bytes per ESP-NOW OTA packet.
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

/// Payload carried inside the data[] field of an OTA_START packet.
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
```

- [ ] **Step 3: Update `firmware/hive-node/include/types.h`**

Remove `HivePayload` struct, add include for shared header. Keep `NodeState` and `StorageMeta`:

```cpp
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
```

- [ ] **Step 4: Verify build**

Run: `cd firmware/hive-node && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 5: Commit**

```bash
git add firmware/shared/ firmware/hive-node/include/types.h
git commit -m "refactor: extract HivePayload to shared header, add OTA protocol types"
```

---

## Task 3: ESP32-S3 Board & Partition Config

**Files:**
- Modify: `firmware/hive-node/platformio.ini`
- Create: `firmware/hive-node/partitions_ota.csv`

- [ ] **Step 1: Rewrite `firmware/hive-node/platformio.ini`**

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

lib_ldf_mode = deep+

build_flags =
    -DCORE_DEBUG_LEVEL=3
    -DCONFIG_PM_ENABLE
    -DCONFIG_BT_NIMBLE_ROLE_PERIPHERAL
    -DCONFIG_BT_NIMBLE_ROLE_BROADCASTER
    -DCONFIG_BT_NIMBLE_ROLE_CENTRAL=0
    -DCONFIG_BT_NIMBLE_ROLE_OBSERVER=0
    -DCONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
    -I../shared
```

- [ ] **Step 2: Create `firmware/hive-node/partitions_ota.csv`**

```csv
# Name,     Type, SubType, Offset,   Size,     Flags
nvs,        data, nvs,     0x9000,   0x5000,
otadata,    data, ota,     0xE000,   0x2000,
app0,       app,  ota_0,   0x10000,  0x380000,
app1,       app,  ota_1,   0x390000, 0x380000,
littlefs,   data, spiffs,  0x710000, 0xF0000,
```

- [ ] **Step 3: Verify build**

Run: `cd firmware/hive-node && pio run`
Expected: BUILD SUCCESS targeting ESP32-S3. Flash usage should be ~1.1 MB out of 3.5 MB.

- [ ] **Step 4: Commit**

```bash
git add firmware/hive-node/platformio.ini firmware/hive-node/partitions_ota.csv
git commit -m "feat: switch to ESP32-S3 with 8MB flash and OTA partition table"
```

---

## Task 4: Update config.h for ESP32-S3 Pin Map

**Files:**
- Modify: `firmware/hive-node/include/config.h`

- [ ] **Step 1: Rewrite `firmware/hive-node/include/config.h`**

```cpp
#pragma once

#include <cstdint>

// =============================================================================
// Pin Definitions — Freenove ESP32-S3 Lite
// Pins grouped by subsystem, ordered to minimize wire crossing.
// =============================================================================

// I2C bus (SHT31 x2)
constexpr uint8_t PIN_I2C_SDA = 8;
constexpr uint8_t PIN_I2C_SCL = 9;

// HX711 weight ADC — adjacent pins for clean 2-wire run
constexpr uint8_t PIN_HX711_DOUT   = 10;
constexpr uint8_t PIN_HX711_CLK    = 11;
constexpr uint8_t PIN_MOSFET_HX711 = 12;

// Battery ADC (ADC1_CH0)
constexpr uint8_t PIN_BATTERY_ADC = 1;

// Onboard RGB LED — disabled at boot to save power
constexpr uint8_t PIN_ONBOARD_RGB = 48;

// Phase 2 — IR array (reserved, do not use in Phase 1)
constexpr uint8_t PIN_MUX_S0     = 4;   // Address lines sequential
constexpr uint8_t PIN_MUX_S1     = 5;
constexpr uint8_t PIN_MUX_S2     = 6;
constexpr uint8_t PIN_MUX_S3     = 7;
constexpr uint8_t PIN_MUX_EN_TX  = 13;  // Enable lines adjacent
constexpr uint8_t PIN_MUX_EN_RX  = 14;
constexpr uint8_t PIN_MUX_SIG_TX = 15;  // Signal lines together
constexpr uint8_t PIN_MUX_SIG_RX = 2;   // ADC1_CH1, internal pull-up for digital read
constexpr uint8_t PIN_MOSFET_IR  = 16;

// =============================================================================
// I2C Addresses
// =============================================================================

constexpr uint8_t SHT31_ADDR_INTERNAL = 0x44;
constexpr uint8_t SHT31_ADDR_EXTERNAL = 0x45;

// =============================================================================
// Timing Constants
// =============================================================================

constexpr uint8_t  DEFAULT_DAY_START_HOUR    = 6;
constexpr uint8_t  DEFAULT_DAY_END_HOUR      = 20;
constexpr uint8_t  DEFAULT_READ_INTERVAL_MIN = 30;
constexpr uint16_t MOSFET_STABILIZE_MS       = 100;
constexpr uint16_t BLE_ADVERTISE_TIMEOUT_MS  = 5000;
constexpr uint8_t  ESPNOW_MAX_RETRIES        = 3;
constexpr uint16_t ESPNOW_RETRY_DELAY_MS     = 2000;

// OTA
constexpr uint8_t  OTA_VALIDATION_TIMEOUT_SEC = 60;
constexpr uint16_t OTA_NVS_SAVE_INTERVAL      = 50;  // Save progress every N chunks

// =============================================================================
// Storage
// =============================================================================

constexpr uint16_t MAX_STORED_READINGS = 500;

// =============================================================================
// BLE UUIDs — derived from "HiveSense" ASCII
// =============================================================================

constexpr const char* BLE_SERVICE_UUID       = "4E6F7200-7468-6976-6553-656E73650000";
constexpr const char* BLE_CHAR_SENSOR_LOG    = "4E6F7200-7468-6976-6553-656E73650001";
constexpr const char* BLE_CHAR_READING_COUNT = "4E6F7200-7468-6976-6553-656E73650002";
constexpr const char* BLE_CHAR_HIVE_ID       = "4E6F7200-7468-6976-6553-656E73650003";
constexpr const char* BLE_CHAR_CLEAR_LOG     = "4E6F7200-7468-6976-6553-656E73650004";

// =============================================================================
// NVS Keys
// =============================================================================

constexpr const char* NVS_NAMESPACE      = "hivesense";
constexpr const char* NVS_KEY_HIVE_ID    = "hive_id";
constexpr const char* NVS_KEY_COLLECTOR  = "collector_mac";
constexpr const char* NVS_KEY_DAY_START  = "day_start";
constexpr const char* NVS_KEY_DAY_END    = "day_end";
constexpr const char* NVS_KEY_INTERVAL   = "read_interval";
constexpr const char* NVS_KEY_WEIGHT_OFF = "weight_off";
constexpr const char* NVS_KEY_WEIGHT_SCL = "weight_scl";

// =============================================================================
// Payload Version
// =============================================================================

constexpr uint8_t PAYLOAD_VERSION = 1;
```

- [ ] **Step 2: Verify build**

Run: `cd firmware/hive-node && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 3: Commit**

```bash
git add firmware/hive-node/include/config.h
git commit -m "feat: update GPIO pin map for Freenove ESP32-S3 Lite"
```

---

## Task 5: Disable Onboard RGB LED & Update Power Manager

**Files:**
- Modify: `firmware/hive-node/src/power_manager.cpp`
- Modify: `firmware/hive-node/src/power_manager.h`

- [ ] **Step 1: Update `power_manager.h`**

Add to the PowerManager namespace after existing declarations:

```cpp
    /// Disable the onboard WS2812 RGB LED to save power.
    void disableOnboardLed();
```

- [ ] **Step 2: Update `power_manager.cpp`**

Add the implementation. The Freenove S3 Lite uses a WS2812 on GPIO 48. Setting the pin LOW and holding it there prevents the LED from drawing current:

```cpp
void disableOnboardLed() {
    pinMode(PIN_ONBOARD_RGB, OUTPUT);
    digitalWrite(PIN_ONBOARD_RGB, LOW);
    Serial.println("[POWER] Onboard RGB LED disabled");
}
```

Add a call to `disableOnboardLed()` at the end of `PowerManager::initialize()`:

```cpp
void initialize() {
    pinMode(PIN_MOSFET_HX711, OUTPUT);
    digitalWrite(PIN_MOSFET_HX711, LOW);

    pinMode(PIN_MOSFET_IR, OUTPUT);
    digitalWrite(PIN_MOSFET_IR, LOW);

    disableOnboardLed();

    Serial.println("[POWER] MOSFET gates initialized — all OFF");
}
```

- [ ] **Step 3: Update `power_manager.cpp` for S3 power management**

The ESP32-S3 uses a different pm config struct. Replace the `esp_pm_config_esp32_t` in `enableLightSleep()`:

```cpp
void enableLightSleep() {
    esp_sleep_enable_timer_wakeup(
        static_cast<uint64_t>(DEFAULT_READ_INTERVAL_MIN) * 60ULL * 1000000ULL
    );

    esp_pm_config_esp32s3_t pmConfig = {
        .max_freq_mhz = 80,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    esp_pm_configure(&pmConfig);

    Serial.println("[POWER] Light sleep enabled — CPU will idle between activity");
}
```

Also update the include to add `esp_pm.h` if not already present.

- [ ] **Step 4: Verify build**

Run: `cd firmware/hive-node && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 5: Commit**

```bash
git add firmware/hive-node/src/power_manager.cpp firmware/hive-node/src/power_manager.h
git commit -m "feat: disable onboard RGB LED, update power manager for ESP32-S3"
```

---

## Task 6: OTA Update Receive Module

**Files:**
- Create: `firmware/hive-node/src/ota_update.h`
- Create: `firmware/hive-node/src/ota_update.cpp`

- [ ] **Step 1: Create `firmware/hive-node/src/ota_update.h`**

```cpp
#pragma once

#include "ota_protocol.h"

/// Receives chunked firmware updates over ESP-NOW from the collector.
/// Progress is stored in NVS to survive sleep cycles.
/// Uses esp_ota_ops to write to the inactive OTA partition.
namespace OtaUpdate {

    /// Check NVS for an in-progress OTA transfer.
    bool isTransferInProgress();

    /// Handle an incoming OTA packet from the collector.
    /// Called from the ESP-NOW receive callback.
    /// Returns true if the packet was handled (even if ignored).
    bool handleOtaPacket(const OtaPacket& packet);

    /// Resume a partial transfer after waking from sleep.
    /// Returns true if OTA is active and node should signal OTA_READY.
    bool resumeTransfer();

    /// Abort the current transfer, clear NVS progress, free OTA handle.
    void abortTransfer();

    /// Run health checks after a fresh OTA boot.
    /// Marks firmware valid on success, rolls back on failure.
    void validateNewFirmware();

}  // namespace OtaUpdate
```

- [ ] **Step 2: Create `firmware/hive-node/src/ota_update.cpp`**

```cpp
#include "ota_update.h"
#include "config.h"

#include <Arduino.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <Preferences.h>
#include <rom/crc.h>

namespace {

const esp_partition_t* updatePartition = nullptr;
esp_ota_handle_t       otaHandle = 0;
bool                   otaInProgress = false;
uint16_t               lastReceivedChunk = 0;
uint16_t               totalChunks = 0;
uint32_t               expectedCrc32 = 0;
uint32_t               runningCrc32 = 0;

/// Save current OTA progress to NVS.
void saveProgress() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool(NVS_KEY_OTA_ACTIVE, true);
    prefs.putUShort(NVS_KEY_OTA_TOTAL, totalChunks);
    prefs.putUShort(NVS_KEY_OTA_RECEIVED, lastReceivedChunk);
    prefs.putULong(NVS_KEY_OTA_CRC32, expectedCrc32);
    prefs.end();
}

/// Clear OTA progress from NVS.
void clearProgress() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool(NVS_KEY_OTA_ACTIVE, false);
    prefs.putUShort(NVS_KEY_OTA_TOTAL, 0);
    prefs.putUShort(NVS_KEY_OTA_RECEIVED, 0);
    prefs.putULong(NVS_KEY_OTA_CRC32, 0);
    prefs.putString(NVS_KEY_OTA_VERSION, "");
    prefs.end();
}

/// Begin OTA write session on the inactive partition.
bool beginOtaWrite() {
    updatePartition = esp_ota_get_next_update_partition(nullptr);
    if (updatePartition == nullptr) {
        Serial.println("[OTA] ERROR: No update partition available");
        return false;
    }

    esp_err_t err = esp_ota_begin(updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &otaHandle);
    if (err != ESP_OK) {
        Serial.printf("[OTA] ERROR: esp_ota_begin failed: %s\n", esp_err_to_name(err));
        return false;
    }

    Serial.printf("[OTA] Write session opened on partition '%s'\n", updatePartition->label);
    return true;
}

/// Handle OTA_START command — initialize a new transfer.
bool handleStart(const OtaPacket& packet) {
    if (packet.data_len < sizeof(OtaStartPayload)) {
        Serial.println("[OTA] ERROR: OTA_START payload too small");
        return false;
    }

    const OtaStartPayload* startPayload =
        reinterpret_cast<const OtaStartPayload*>(packet.data);

    totalChunks = startPayload->total_chunks;
    expectedCrc32 = startPayload->crc32;
    lastReceivedChunk = 0;
    runningCrc32 = 0;

    Serial.printf("[OTA] Starting update: %u bytes, %u chunks, version=%s\n",
                  startPayload->firmware_size, totalChunks, startPayload->version);

    if (!beginOtaWrite()) {
        return false;
    }

    // Save version to NVS
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString(NVS_KEY_OTA_VERSION, startPayload->version);
    prefs.end();

    otaInProgress = true;
    saveProgress();
    return true;
}

/// Handle OTA_CHUNK command — write one chunk to OTA partition.
bool handleChunk(const OtaPacket& packet) {
    if (!otaInProgress) {
        Serial.println("[OTA] ERROR: Received chunk but no transfer in progress");
        return false;
    }

    if (packet.chunk_index != lastReceivedChunk + 1) {
        // Out-of-order or duplicate — request resend from lastReceivedChunk + 1
        Serial.printf("[OTA] Chunk %u out of order (expected %u)\n",
                      packet.chunk_index, lastReceivedChunk + 1);
        return false;
    }

    esp_err_t err = esp_ota_write(otaHandle, packet.data, packet.data_len);
    if (err != ESP_OK) {
        Serial.printf("[OTA] ERROR: esp_ota_write failed: %s\n", esp_err_to_name(err));
        abortTransfer();
        return false;
    }

    // Update running CRC32
    runningCrc32 = crc32_le(runningCrc32, packet.data, packet.data_len);

    lastReceivedChunk = packet.chunk_index;

    // Periodically save progress to NVS to survive sleep
    if (lastReceivedChunk % OTA_NVS_SAVE_INTERVAL == 0) {
        saveProgress();
    }

    Serial.printf("[OTA] Chunk %u/%u written\n", lastReceivedChunk, totalChunks);
    return true;
}

/// Handle OTA_END command — finalize transfer, verify CRC, set boot partition.
bool handleEnd(const OtaPacket& packet) {
    if (!otaInProgress) {
        return false;
    }

    // Verify CRC32
    if (runningCrc32 != expectedCrc32) {
        Serial.printf("[OTA] ERROR: CRC32 mismatch — expected 0x%08X, got 0x%08X\n",
                      expectedCrc32, runningCrc32);
        abortTransfer();
        return false;
    }

    esp_err_t err = esp_ota_end(otaHandle);
    if (err != ESP_OK) {
        Serial.printf("[OTA] ERROR: esp_ota_end failed: %s\n", esp_err_to_name(err));
        abortTransfer();
        return false;
    }

    err = esp_ota_set_boot_partition(updatePartition);
    if (err != ESP_OK) {
        Serial.printf("[OTA] ERROR: esp_ota_set_boot_partition failed: %s\n",
                      esp_err_to_name(err));
        abortTransfer();
        return false;
    }

    clearProgress();
    otaInProgress = false;

    Serial.println("[OTA] Update complete — rebooting into new firmware");
    Serial.flush();
    esp_restart();

    return true;  // Unreachable
}

}  // anonymous namespace

namespace OtaUpdate {

bool isTransferInProgress() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    bool active = prefs.getBool(NVS_KEY_OTA_ACTIVE, false);
    prefs.end();
    return active;
}

bool handleOtaPacket(const OtaPacket& packet) {
    switch (packet.command) {
        case OtaCommand::OTA_START: return handleStart(packet);
        case OtaCommand::OTA_CHUNK: return handleChunk(packet);
        case OtaCommand::OTA_END:   return handleEnd(packet);
        case OtaCommand::OTA_ABORT:
            Serial.println("[OTA] Abort received from collector");
            abortTransfer();
            return true;
        default:
            return false;
    }
}

bool resumeTransfer() {
    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);
    bool active = prefs.getBool(NVS_KEY_OTA_ACTIVE, false);

    if (!active) {
        prefs.end();
        return false;
    }

    totalChunks = prefs.getUShort(NVS_KEY_OTA_TOTAL, 0);
    lastReceivedChunk = prefs.getUShort(NVS_KEY_OTA_RECEIVED, 0);
    expectedCrc32 = prefs.getULong(NVS_KEY_OTA_CRC32, 0);
    prefs.end();

    if (!beginOtaWrite()) {
        clearProgress();
        return false;
    }

    // Seek the OTA write handle to where we left off.
    // esp_ota_begin starts from scratch — we need to skip already-written bytes.
    // On resume, the collector resends from lastReceivedChunk + 1, and the
    // OTA partition is erased by esp_ota_begin. This means we lose progress
    // on resume and must re-receive all chunks. This is acceptable because:
    // 1. Full transfer takes 1-2 wake cycles (~30-60 min)
    // 2. Partial progress within a session is still fast
    // 3. The alternative (raw partition writes) is fragile
    lastReceivedChunk = 0;
    runningCrc32 = 0;
    saveProgress();

    otaInProgress = true;
    Serial.printf("[OTA] Transfer resumed — requesting all %u chunks\n", totalChunks);
    return true;
}

void abortTransfer() {
    if (otaHandle != 0) {
        esp_ota_abort(otaHandle);
        otaHandle = 0;
    }
    updatePartition = nullptr;
    otaInProgress = false;
    clearProgress();
    Serial.println("[OTA] Transfer aborted — progress cleared");
}

void validateNewFirmware() {
    esp_ota_img_states_t state;
    const esp_partition_t* running = esp_ota_get_running_partition();

    if (esp_ota_get_state_info(running, &state) != ESP_OK) {
        return;  // Not an OTA boot — nothing to validate
    }

    if (state != ESP_OTA_IMG_PENDING_VERIFY) {
        return;  // Already validated or not an OTA boot
    }

    Serial.println("[OTA] New firmware — running validation checks...");

    // Health checks: verify critical hardware responds
    bool healthy = true;

    // Check I2C bus (SHT31 sensors)
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    Wire.beginTransmission(SHT31_ADDR_INTERNAL);
    if (Wire.endTransmission() != 0) {
        Serial.println("[OTA] FAIL: Internal SHT31 not responding");
        healthy = false;
    }

    Wire.beginTransmission(SHT31_ADDR_EXTERNAL);
    if (Wire.endTransmission() != 0) {
        Serial.println("[OTA] FAIL: External SHT31 not responding");
        healthy = false;
    }

    if (healthy) {
        esp_ota_mark_app_valid_cancel_rollback();
        Serial.println("[OTA] Firmware validated — marked as stable");
    } else {
        Serial.println("[OTA] Validation FAILED — rolling back to previous firmware");
        Serial.flush();
        esp_ota_mark_app_invalid_rollback_and_reboot();
        // Does not return
    }
}

}  // namespace OtaUpdate
```

Note: `validateNewFirmware()` includes `Wire.h` — add `#include <Wire.h>` to the includes at the top of the file.

- [ ] **Step 3: Verify build**

Run: `cd firmware/hive-node && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 4: Commit**

```bash
git add firmware/hive-node/src/ota_update.h firmware/hive-node/src/ota_update.cpp
git commit -m "feat: add OTA update receive module with CRC32 validation and rollback"
```

---

## Task 7: Integrate OTA into State Machine

**Files:**
- Modify: `firmware/hive-node/src/state_machine.cpp`
- Modify: `firmware/hive-node/src/main.cpp`

- [ ] **Step 1: Update `state_machine.cpp`**

Add include at top:
```cpp
#include "ota_update.h"
```

Add OTA validation call in `determineInitialState()`, before the existing logic:

```cpp
NodeState determineInitialState() {
    // On first boot after OTA, validate the new firmware
    OtaUpdate::validateNewFirmware();

    esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();

    if (wakeupReason == ESP_SLEEP_WAKEUP_TIMER) {
        // Check if OTA transfer is in progress
        if (OtaUpdate::isTransferInProgress()) {
            Serial.println("[SM] Timer wakeup — OTA in progress, resuming");
            return NodeState::OTA_RECEIVE;
        }
        Serial.println("[SM] Timer wakeup — starting sensor read");
        return NodeState::SENSOR_READ;
    }

    Serial.println("[SM] Fresh boot — initializing");
    return NodeState::SENSOR_READ;
}
```

Add the `OTA_RECEIVE` case to `executeState()`:

```cpp
        case NodeState::OTA_RECEIVE: {
            Serial.println("[SM] === OTA_RECEIVE ===");

            OtaUpdate::resumeTransfer();

            // Still do a sensor read and ESP-NOW send while OTA is active
            // The collector handles interleaving OTA chunks with the normal cycle
            return NodeState::SENSOR_READ;
        }
```

- [ ] **Step 2: Verify build**

Run: `cd firmware/hive-node && pio run`
Expected: BUILD SUCCESS

- [ ] **Step 3: Commit**

```bash
git add firmware/hive-node/src/state_machine.cpp
git commit -m "feat: integrate OTA receive state into state machine with rollback validation"
```

---

## Task 8: Integration Build & Verify

**Files:**
- No new files — full build verification

- [ ] **Step 1: Clean build**

Run: `cd firmware/hive-node && pio run --target clean && pio run`
Expected: BUILD SUCCESS. Note flash and RAM usage.

- [ ] **Step 2: Verify flash fits in OTA partition**

Expected: app size < 3.5 MB (the OTA partition size). Should be ~1.1-1.2 MB.

- [ ] **Step 3: Verify all source files present**

```bash
find firmware/hive-node -name "*.cpp" -o -name "*.h" | grep -v ".pio" | sort
```

Expected files:
```
firmware/hive-node/include/config.h
firmware/hive-node/include/module.h
firmware/hive-node/include/types.h
firmware/hive-node/src/battery.cpp
firmware/hive-node/src/battery.h
firmware/hive-node/src/comms_ble.cpp
firmware/hive-node/src/comms_ble.h
firmware/hive-node/src/comms_espnow.cpp
firmware/hive-node/src/comms_espnow.h
firmware/hive-node/src/main.cpp
firmware/hive-node/src/ota_update.cpp
firmware/hive-node/src/ota_update.h
firmware/hive-node/src/power_manager.cpp
firmware/hive-node/src/power_manager.h
firmware/hive-node/src/sensor_hx711.cpp
firmware/hive-node/src/sensor_hx711.h
firmware/hive-node/src/sensor_sht31.cpp
firmware/hive-node/src/sensor_sht31.h
firmware/hive-node/src/state_machine.cpp
firmware/hive-node/src/state_machine.h
firmware/hive-node/src/storage.cpp
firmware/hive-node/src/storage.h
firmware/shared/hive_payload.h
firmware/shared/ota_protocol.h
```

- [ ] **Step 4: Commit integration verification**

```bash
git add -A firmware/
git commit -m "feat: complete OTA and S3 migration — all modules integrated"
```

---

## Task 9: Update Project State

**Files:**
- Modify: `.mex/ROUTER.md`
- Modify: `.mex/context/decisions.md`
- Modify: `.mex/context/conventions.md`

- [ ] **Step 1: Update ROUTER.md**

Update phase to: `Phase 1 Firmware Complete + OTA — ESP32-S3, Awaiting Hardware`

Update completed section to include:
- ESP32-S3 migration (Freenove Lite, 8MB flash)
- NimBLE BLE stack
- OTA receive module with CRC32 and rollback
- Multi-firmware directory structure

- [ ] **Step 2: Add decisions to decisions.md**

Add entries for:
- ESP32-S3 over WROOM-32 (8MB flash for OTA, BLE 5.0, readily available)
- NimBLE over Bluedroid (528KB flash savings, enables OTA dual partitions)
- Collector-relay OTA over BLE-only OTA (remote updates without yard visit)
- GitHub Releases for firmware distribution (version-controlled, free, existing workflow)

- [ ] **Step 3: Update conventions.md**

Update project structure section to reflect new `firmware/hive-node/`, `firmware/collector/`, `firmware/shared/` layout.

- [ ] **Step 4: Commit**

```bash
git add .mex/
git commit -m "chore: update .mex project state — S3 migration and OTA complete"
```
