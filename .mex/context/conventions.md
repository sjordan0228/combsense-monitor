---
name: conventions
description: Coding conventions and project patterns
triggers:
  - "convention"
  - "code style"
  - "naming"
  - "best practice"
  - "verify"
edges:
  - target: context/architecture.md
    condition: when conventions apply to a specific component or layer
  - target: context/stack.md
    condition: when a convention is tied to a specific library or framework
  - target: patterns/INDEX.md
    condition: when looking for a task-specific verify checklist
last_updated: 2026-05-03
---

# Conventions

## Project Structure

```
combsense-monitor/
  firmware/
    hive-node/          — Freenove ESP32-S3 Lite hive node firmware
    collector/          — LilyGO T-SIM7080G yard collector firmware
    sensor-tag/         — BLE remote sensor (XIAO C6, Feather S3, Freenove S3 Lite)
    sensor-tag-wifi/    — WiFi/MQTT home-yard sensor (XIAO C6, S3-Zero)
      src/              — scale.{h,cpp}, scale_math, scale_commands, config_runtime, capabilities, config_ack, ...
      include/config.h  — pin maps, buffer capacity, MAGIC constants
    shared/             — Headers shared between hive-node/collector (HivePayload, OTA protocol)
  hardware/             — Schematics, PCB layouts, 3D print files
  tools/                — provision_tag.py, set_config.py, mqtt_simulator.py
  deploy/
    tsdb/               — Telegraf config, Influx downsample Flux tasks, Grafana JSON
    web/                — Django systemd unit, provision.sh, nginx config
  web/                  — Django combsense-web app
  .mex/                 — AI context scaffold (ROUTER, AGENTS, context/, patterns/)
    scale-mqtt-contract.md    — iOS↔firmware scale calibration MQTT protocol
    config-mqtt-contract.md   — iOS↔firmware per-hive config/capabilities MQTT protocol
  README.md             — Hardware datasheet (primary design doc)
```

## Engineering Principles

- **SOLID, DRY, KISS** — prioritize long-term maintainability over clever one-liners
- **Descriptive semantic naming** — `calculateTotalBalance` not `calc`, `readInternalTemperature` not `readTemp1`
- **Small focused functions** — single responsibility per function
- **Guard clauses over nesting** — avoid deeply nested loops or complex ternaries
- **Const-correctness** — mark parameters `const&` when not modified
- **RAII** — tie resource lifetime to scope (file handles, peripheral power, buffers)
- **Modern C++ idioms** — prefer STL algorithms over manual loops where intention is clearer
- **Meaningful inline comments** — explain "why" only where not obvious from the code
- **Docstrings for public methods** — document interface, parameters, and return values
- **Explicit ownership** — clear who creates, owns, and destroys resources

## Firmware Conventions (ESP32/Arduino)

- Use PlatformIO for build management
- C/C++ with Arduino framework
- Modular files: one file per sensor/subsystem
- Each module exposes `initialize()`, `readMeasurements()`, `enterSleep()` interface
- Central state machine dispatcher calls into modules
- Use deep sleep and power gating aggressively
- MOSFET power control lives in each module's `enterSleep()`/`initialize()`
- Store config in NVS (non-volatile storage)
- ESP-NOW payload struct defined in shared header
- Prefer `std::accumulate`, `std::transform` etc. over raw loops when clearer
- Use `explicit` on single-argument constructors
- Use `const std::string&` over `String` where possible (avoid Arduino String fragmentation)

## Branching Strategy

- `main` — stable baseline
- `dev` — integration branch
- Feature branches off `dev` with descriptive names
- PRs to `dev`, `dev → main` at milestones

## MQTT Contract-First Pattern

For any iOS↔firmware bidirectional feature (scale calibration, config, capabilities):
1. Write the MQTT contract document in `.mex/` first (command/response schemas, topic names, versioning)
2. Both iOS and firmware implement against the contract — not against each other's code
3. Contract changes require a version bump in the document and an OTA deploy before iOS ships

## Verify Checklist

Run after every firmware change:
1. `pio run -s 2>&1 | tail -5` — silent build across all envs
2. `pio test -e native 2>&1 | grep -E "(PASS|FAIL|Tests|Ignored)"` — 152 native tests must pass
3. Upload to test device and verify serial output
4. Sensor readings are reasonable; scale reads non-NaN after calibration
5. Deep sleep current measured (target: <15 µA bare chip)
6. For MQTT protocol changes: verify Telegraf conf marks new fields `optional = true`
