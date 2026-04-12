---
name: router
description: Session bootstrap and navigation hub. Read at the start of every session before any task.
last_updated: 2026-04-11
firmware_task: 8_complete
---

# Session Bootstrap

Read this file fully before doing anything else in this session.

## Current Project State

**Phase: Design Complete, Hardware Not Ordered**

The hardware datasheet (README.md) covers the full system design:
- ESP32-WROOM-32 hive nodes with SHT31 temp/humidity, HX711 weight, IR bee counter
- LilyGO T-SIM7080G yard collector with cellular MQTT
- HiveMQ Cloud MQTT broker (free tier)
- BLE direct + MQTT cloud dual communication
- iOS app integration spec (Section 8 of README.md)

**In progress:**
- ESP32 firmware (Arduino/PlatformIO) — Task 8 complete: ESP-NOW communication module (comms_espnow.cpp). WiFi STA init, NVS collector MAC load, up to 3 retries with 1 s ACK timeout and 2 s inter-retry delay, RSSI population on success. Builds clean.

**Not yet built:**
- Yard collector firmware
- HiveSense iOS app sensor integration (BLE, MQTT, SensorReading model, SensorsTab)
- 3D printed enclosures and sensor gate
- HiveMQ Cloud account setup

**Related repos:**
- `sjordan0228/hivesense` — iOS app (SwiftUI + SwiftData)
- `sjordan0228/hivesense-monitor` — this repo (hardware + firmware)

## Routing Table

| Task type | Load |
|-----------|------|
| Understanding the hardware design | `README.md` (the full datasheet) |
| Working on ESP32 firmware | `firmware/` directory |
| Working on collector firmware | `collector/` directory (not yet created) |
| Making a design decision | `context/decisions.md` |
| Writing or reviewing code | `context/conventions.md` |

## Behavioural Contract

For every task, follow this loop:

1. **CONTEXT** — Load the relevant context file(s) from the routing table above.
2. **BUILD** — Do the work.
3. **VERIFY** — Build and test.
4. **GROW** — Update context files if anything changed.
