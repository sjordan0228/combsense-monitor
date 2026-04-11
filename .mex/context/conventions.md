---
name: conventions
description: Coding conventions and project patterns
last_updated: 2026-04-11
---

# Conventions

## Project Structure

```
hivesense-monitor/
  firmware/           — ESP32 hive node firmware (PlatformIO/Arduino)
  collector/          — LilyGO T-SIM7080G collector firmware
  hardware/           — Schematics, PCB layouts, 3D print files
  docs/               — Additional documentation
  README.md           — Hardware datasheet (primary design doc)
```

## Firmware Conventions (ESP32/Arduino)

- Use PlatformIO for build management
- C/C++ with Arduino framework
- Modular files: one file per sensor/subsystem
- Use deep sleep and power gating aggressively
- Store config in NVS (non-volatile storage)
- ESP-NOW payload struct defined in shared header

## Branching Strategy

- `main` — stable baseline
- `dev` — integration branch
- Feature branches off `dev` with descriptive names
- PRs to `dev`, `dev → main` at milestones

## Verify Checklist

Run after every firmware change:
1. PlatformIO build succeeds
2. Upload to test ESP32 and verify serial output
3. Sensor readings are reasonable
4. Deep sleep current measured (target: <15 µA bare chip)
5. ESP-NOW transmission confirmed by collector
