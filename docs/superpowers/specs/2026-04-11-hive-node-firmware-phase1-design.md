# Hive Node Firmware — Phase 1 Design Spec

**Date:** 2026-04-11
**Scope:** ESP32-WROOM-32 hive node firmware — state machine, SHT31, HX711, ESP-NOW, BLE, LittleFS storage, power management. IR bee counter deferred to Phase 2.
**Framework:** Arduino via PlatformIO, espressif/arduino-esp32 v2.x (ESP-IDF 4.4)
**Architecture:** Modular with central state machine dispatcher

---

## 1. Project Structure

```
firmware/
├── platformio.ini
├── include/
│   ├── config.h              — Pin definitions, timing constants, hive ID
│   ├── types.h               — HivePayload struct, enums, shared types
│   └── module.h              — Base module interface (init/read/sleep)
├── src/
│   ├── main.cpp              — State machine dispatcher, setup/loop
│   ├── state_machine.cpp/.h  — State definitions, transitions, RTC time tracking
│   ├── power_manager.cpp/.h  — Deep sleep, light sleep, MOSFET gate control
│   ├── sensor_sht31.cpp/.h   — SHT31 temp/humidity (both internal + external)
│   ├── sensor_hx711.cpp/.h   — HX711 weight reading with tare/calibration
│   ├── comms_espnow.cpp/.h   — ESP-NOW transmit with retry logic
│   ├── comms_ble.cpp/.h      — BLE GATT server, sensor log download, clear
│   ├── storage.cpp/.h        — LittleFS circular buffer for sensor readings
│   └── battery.cpp/.h        — ADC voltage read, percentage estimation
└── test/                     — PlatformIO unit tests (native platform)
```

### Module Interface Pattern

Every module in `src/` follows a consistent namespace-based interface:

```cpp
namespace ModuleName {
    bool initialize();                              // Power on, configure, validate
    bool readMeasurements(HivePayload& payload);    // Fill relevant payload fields
    void enterSleep();                              // Power off, release resources
}
```

The state machine dispatcher calls these in sequence. No module knows about any other module — they only interact through the shared `HivePayload` struct.

---

## 2. State Machine & Power Management

### Operating States

```
┌──────────────────────────────────────��──────────────┐
│                    BOOT                              │
│  Determine wake reason (timer vs first boot)        │
│  Read RTC time → decide: daytime or nighttime?      │
│  Initialize only what's needed for current state     │
└──────────────┬──────────────────────────────────────┘
               │
       ┌───────┴───────┐
       ▼               ▼
  DAYTIME_IDLE    NIGHTTIME_SLEEP
  (light sleep     (deep sleep
   between          30-min wake)
   activities)
       │               │
       ▼               ▼
  ┌─────────────────────────┐
  │      SENSOR_READ        │
  │  Power on MOSFET gates  │
  │  SHT31 x2 → payload    │
  │  HX711 → payload       │
  │  Battery ADC → payload  │
  │  Timestamp → payload    │
  │  Store to LittleFS      │
  │  Power off MOSFETs      │
  └────────────┬────────────┘
               ▼
  ┌─────────────────────────┐
  │      ESPNOW_TRANSMIT    │
  │  Spin up WiFi radio     │
  │  Send payload (3 tries) │
  │  Wait for ACK           │
  │  Shut down radio        │
  └────────────┬────────────┘
               ▼
  ┌─────────────────────────┐
  │      BLE_CHECK          │
  │  Brief BLE scan/advert  │
  │  If phone nearby:       │
  │    → BLE_SYNC state     │
  │  Else: return to idle   │
  └────────────┬────────────┘
               ▼
       Return to DAYTIME_IDLE
       or NIGHTTIME_SLEEP
```

### State Transition Rules

- **Wake reason determines path.** `esp_sleep_get_wakeup_cause()` differentiates timer wake (sensor read cycle) from first boot (full initialization). No redundant init on timer wakes.
- **RTC memory for persistent state.** Current state, last-read timestamp, and boot count survive light sleep in `RTC_DATA_ATTR` variables. Deep sleep resets only what should reset.
- **30-minute timer is the heartbeat.** Both daytime and nighttime use the same 30-minute sensor read cycle. The difference: light sleep between reads during daytime (~0.8 mA) vs deep sleep at night (~10 uA).
- **Daytime/nighttime window.** Configurable in NVS (default: 6 AM-8 PM). No recompile needed per deployment.
- **BLE check is opportunistic.** After ESP-NOW transmit, the node advertises BLE for ~5 seconds. No connection = shut down BLE and return to idle.
- **MOSFET gates per-read.** HX711 and load cells fully powered off between sensor reads. Power-on, stabilize (~100ms), read, power-off. Each module's `initialize()` handles its own MOSFET gate.

### Power Budget

| State | Duration | Current | What's Powered |
|---|---|---|---|
| NIGHTTIME_SLEEP | ~10 hrs | ~10 uA (bare chip) | RTC only |
| DAYTIME_IDLE | Between 30-min reads | ~0.8 mA | RTC + light sleep |
| SENSOR_READ | ~2-3 seconds | ~26 mA | ESP32 active + sensors |
| ESPNOW_TRANSMIT | ~1-3 seconds | ~120 mA peak | WiFi radio |
| BLE_CHECK | ~5 seconds | ~12 mA | BLE radio |
| BLE_SYNC | Variable (phone connected) | ~12 mA | BLE radio + flash read |

---

## 3. GPIO Pin Map (ESP32-WROOM-32)

Constraints: GPIO 6-11 reserved for internal flash. GPIO 34-39 input-only (no pull-ups). Strapping pins (0, 2, 5, 12, 15) used carefully or avoided.

| GPIO | Assignment | Direction | Notes |
|---|---|---|---|
| **I2C Bus** | | | |
| 21 | SDA (SHT31 x2) | Bidirectional | Default I2C SDA, hardware pull-up recommended |
| 22 | SCL (SHT31 x2) | Output | Default I2C SCL, hardware pull-up recommended |
| **HX711** | | | |
| 16 | HX711_DOUT | Input | Data out from HX711 |
| 17 | HX711_CLK | Output | Clock to HX711 |
| 18 | MOSFET_HX711 | Output | N-channel MOSFET gate — powers HX711 + load cells |
| **Battery** | | | |
| 34 | BATTERY_ADC | Input (ADC1_CH6) | Input-only, ADC1 (works with WiFi active) |
| **IR Array — Phase 2 (reserved, not used in Phase 1)** | | | |
| 25 | MUX_S0 | Output | CD74HC4067 shared address bit 0 |
| 26 | MUX_S1 | Output | CD74HC4067 shared address bit 1 |
| 27 | MUX_S2 | Output | CD74HC4067 shared address bit 2 |
| 14 | MUX_S3 | Output | CD74HC4067 shared address bit 3 |
| 32 | MUX_EN_TX | Output | TX mux enable (active low) |
| 33 | MUX_EN_RX | Output | RX mux enable (active low) |
| 4 | MUX_SIG_TX | Output | TX mux common — drives emitter |
| 35 | MUX_SIG_RX | Input (ADC1_CH7) | RX mux common — reads receiver |
| 19 | MOSFET_IR | Output | N-channel MOSFET gate — powers IR array |
| **Status** | | | |
| 2 | STATUS_LED | Output | Onboard LED. Strapping pin — safe as output after boot |

### Pin Selection Rationale

- **GPIO 34 for battery ADC** — input-only pins are ideal for ADC. ADC1 channel, no conflict with WiFi.
- **GPIO 35 for IR RX signal (Phase 2)** — also ADC1, input-only.
- **GPIO 12 avoided** — strapping pin affecting flash voltage. Risky for general use.
- **GPIO 0, 5, 15 left free** — strapping pins used during boot, best avoided for peripherals.
- **Phase 2 pins reserved now** — defined in `config.h` so Phase 1 code doesn't accidentally claim them.

---

## 4. Data Types & Storage

### HivePayload Struct

The single data unit flowing through the entire system. Matches the README spec with added timestamp and version fields.

```cpp
struct HivePayload {
    uint8_t  version;            // Payload format version (1 for Phase 1)
    char     hive_id[16];        // Null-terminated hive identifier
    uint32_t timestamp;          // Unix epoch seconds
    float    weight_kg;
    float    temp_internal;      // deg C — SHT31 at address 0x44
    float    temp_external;      // deg C — SHT31 at address 0x45
    float    humidity_internal;  // %RH
    float    humidity_external;  // %RH
    uint16_t bees_in;            // Phase 2 — zeroed in Phase 1
    uint16_t bees_out;           // Phase 2 — zeroed in Phase 1
    uint16_t bees_activity;      // Phase 2 — zeroed in Phase 1
    uint8_t  battery_pct;        // 0-100
    int8_t   rssi;               // ESP-NOW signal strength
};
```

### LittleFS Circular Buffer

Stores readings for BLE sync.

```
/littlefs/
├── meta.bin        — { uint16_t head, uint16_t count, uint8_t version }
└── readings.bin    — HivePayload[MAX_READINGS] fixed-size array
```

- Fixed-size ring buffer of `HivePayload` structs
- Metadata file tracks head index and count
- Max capacity: configurable in `config.h` (default ~500 readings = ~24 KB)
- On BLE sync: read all unsynced entries, send to phone, then clear
- On overflow: oldest entries overwritten (standard ring buffer)
- Binary format — smaller, faster, no parsing overhead

### NVS Configuration

Deployment-specific settings stored as key-value pairs, writable via BLE config characteristic.

| Key | Type | Default | Purpose |
|---|---|---|---|
| `hive_id` | string | `"HIVE-001"` | Identifies this node in MQTT topics and BLE |
| `collector_mac` | blob (6 bytes) | `{0xFF,...}` | ESP-NOW target MAC address |
| `day_start_hour` | uint8 | `6` | Daytime window start (24h) |
| `day_end_hour` | uint8 | `20` | Daytime window end (24h) |
| `read_interval_min` | uint8 | `30` | Minutes between sensor reads |
| `weight_offset` | float | `0.0` | Tare offset for load cells |
| `weight_scale` | float | `1.0` | Calibration factor for load cells |

---

## 5. BLE GATT Service

### Service Characteristics

Custom service with four characteristics.

| Characteristic | UUID | Properties | Data |
|---|---|---|---|
| Sensor Log | `0001` | Read, Notify | Array of `HivePayload` structs from LittleFS |
| Reading Count | `0002` | Read | `uint16_t` — number of stored readings |
| Hive ID | `0003` | Read, Write | `char[16]` — writable by phone for pairing |
| Clear Log | `0004` | Write | Write `0x01` to clear storage after sync |

### BLE Flow (Firmware Side)

1. After ESP-NOW transmit, node starts BLE advertising for ~5 seconds
2. Advertisement name: `"HiveSense-{hive_id}"` with reading count in manufacturer data
3. If no connection within the window, BLE shuts down, node returns to idle
4. If phone connects: node stays awake, serves reads, waits for clear command
5. On disconnect or clear command received: BLE shuts down, return to idle

### Transfer Protocol

- Default BLE MTU is 23 bytes; negotiate up to 512
- `HivePayload` is ~48 bytes — fits in a single notification at 512 MTU
- Sensor Log characteristic sends payloads one at a time via notifications with small delay between each
- Phone counts received payloads against Reading Count to confirm complete transfer
- Phone sends Clear command only after confirmed complete transfer

### Custom UUIDs

Base UUID: `4E6F7200-7468-6976-6553-656E73650000` (derived from "HiveSense" ASCII)

| Characteristic | Full UUID |
|---|---|
| Service | `4E6F7200-7468-6976-6553-656E73650000` |
| Sensor Log | `4E6F7200-7468-6976-6553-656E73650001` |
| Reading Count | `4E6F7200-7468-6976-6553-656E73650002` |
| Hive ID | `4E6F7200-7468-6976-6553-656E73650003` |
| Clear Log | `4E6F7200-7468-6976-6553-656E73650004` |

Defined in `config.h` to match iOS app expectations.

---

## 6. Phase 2 Scope (Not Implemented)

The following are explicitly excluded from Phase 1 and reserved for Phase 2:

- IR beam-break array (8 pairs via CD74HC4067 multiplexers)
- Hybrid multiplexing strategy (multiplex vs group mode switching)
- Directional lane counting (beam sequence analysis)
- MOSFET_IR gate control
- `bees_in`, `bees_out`, `bees_activity` fields populated (zeroed in Phase 1)
- Daytime IR scan loop with light sleep between scans

Phase 2 GPIO pins are reserved in `config.h` from the start.

---

## 7. Dependencies

| Library | Purpose | PlatformIO Registry |
|---|---|---|
| `adafruit/Adafruit SHT31 Library` | SHT31 I2C driver | PlatformIO lib registry |
| `bogde/HX711` | HX711 ADC driver | PlatformIO lib registry |
| `LittleFS` | Flash filesystem | Bundled with arduino-esp32 |
| `ESP32 BLE Arduino` | BLE GATT server | Bundled with arduino-esp32 |
| `esp_now.h` | ESP-NOW protocol | Bundled with ESP-IDF |
| `Preferences` | NVS key-value store | Bundled with arduino-esp32 |
