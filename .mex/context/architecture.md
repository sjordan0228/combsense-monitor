---
name: architecture
description: How the major pieces of this project connect and flow. Load when working on system design, integrations, or understanding how components interact.
triggers:
  - "architecture"
  - "system design"
  - "how does X connect to Y"
  - "integration"
  - "flow"
edges:
  - target: context/stack.md
    condition: when specific technology details are needed
  - target: context/decisions.md
    condition: when understanding why the architecture is structured this way
  - target: context/setup.md
    condition: when bringing up a new component or LXC service
last_updated: 2026-05-03
---

# Architecture

## System Overview

Two parallel ingestion paths feed the same observability layer.

**Remote-yard path (cellular):** hive nodes (ESP32-S3) sample sensors → broadcast `HivePayload` over ESP-NOW (250-byte frames) → yard collector (LilyGO T-SIM7080G) buffers + batch-publishes via cellular MQTT to HiveMQ Cloud → iOS app subscribes for live; Telegraf relays to Influx for history.

**Home-yard path (WiFi):** sensor-tag-wifi nodes (XIAO ESP32-C6) connect WiFi on each wake → publish JSON to local Mosquitto (`192.168.1.82`) → Telegraf consumes `combsense/hive/+/reading` → InfluxDB 2.8 (combsense-tsdb LXC) → Grafana dashboards + iOS history (Flux query). Direct path; no collector. Offline samples buffered in RTC ring (28-byte `Reading` × 288 slots = 24h of coverage at 5-min cadence).

**Direct-at-yard path (BLE):** hive nodes expose a NimBLE GATT server with full sensor-log sync — iOS app connects when within ~30 ft, no internet required.

**OTA dispatch:** sensor-tag-wifi pulls firmware from `combsense-web` nginx (HTTP, sha256-verified, dual 1.5 MB OTA slots, bootloader auto-rollback). Hive nodes get OTA via collector relay over ESP-NOW.

## Key Components

- **Hive Node firmware** (`firmware/hive-node/`) — Freenove ESP32-S3 Lite (8 MB flash). Power-aware state machine, SHT31 + HX711 + ADC, ESP-NOW publisher, NimBLE GATT, LittleFS circular log (500 readings), OTA receiver with CRC32 + auto-rollback.
- **Yard Collector firmware** (`firmware/collector/`) — LilyGO T-SIM7080G. ESP-NOW receiver with payload buffering + MAC tracking, TinyGSM cellular, MQTT batch publisher (30-min cycle), OTA relay (GitHub → ESP-NOW chunked), TIME_SYNC broadcast every publish.
- **Sensor-tag-wifi firmware** (`firmware/sensor-tag-wifi/`) — XIAO ESP32-C6 (default) or Waveshare ESP32-S3-Zero (NOT recommended for solar/sleep). Compile-time sensor abstraction (SHT31 dual / DS18B20 dual), direct MQTT to local Mosquitto, RTC ring buffer (288 readings, 24h), BSSID caching, HTTP-pull OTA, USB-CDC console provisioning.
  - **Scale subsystem:** `scale.{h,cpp}`, `scale_math.{h,cpp}`, `scale_commands.{h,cpp}`. HX711 + 4× 50 kg load cells over Wheatstone bridge. Bidirectional MQTT protocol at `.mex/scale-mqtt-contract.md`: 8 commands on `scale/cmd`, 10 status events on `scale/status`, `scale/config` keep-alive for extended-awake mode. `Reading` struct carries `weight_kg` (NaN when uncalibrated). Env `xiao-c6-ds18b20-scale` and `waveshare-s3zero-ds18b20-scale`.
  - **Per-hive feature flags subsystem:** `config_runtime.{h,cpp}` (runtime `Config::isEnabled(name)` API, flags persisted in NVS), `capabilities.{h,cpp}` (publishes `combsense/hive/<id>/capabilities` retained at boot), `config_ack.{h,cpp}` (rich ack format with 6 result categories). Protocol at `.mex/config-mqtt-contract.md` v1.1. Mutual exclusion validation: `feat_ds18b20` ⊕ `feat_sht31`. Boot order: apply config → sample → publish capabilities → publish reading.
- **Sensor-tag firmware (BLE)** (`firmware/sensor-tag/`) — XIAO ESP32-C6 (default), Adafruit Feather S3 (`feather-s3` env), or Freenove S3 Lite MINI-1 (`freenove-s3-lite` env). CR2032-powered BLE advertiser, read by hive node tag-reader.
- **Shared headers** (`firmware/shared/`) — `HivePayload` struct, ESP-NOW packet wrapping, OTA protocol constants. Both hive-node and collector include these.
- **TSDB stack** (`deploy/tsdb/`, `combsense-tsdb` LXC at 192.168.1.19) — Telegraf MQTT→Influx, InfluxDB 2.8 with three-bucket retention (raw 30d / 1h 365d / 1d ∞), Grafana 13 with provisioned dashboards.
- **combsense-web** (`web/`, `deploy/web/`, `combsense-web` LXC at 192.168.1.61) — Django 5.2 LTS (gunicorn + nginx TLS), Postgres 15, Redis 7 (for Celery later). Hosts firmware OTA endpoint (`/firmware/sensor-tag-wifi/<variant>/`).

## External Dependencies

- **HiveMQ Cloud** (free tier, planned for cellular yards) — remote-yard MQTT broker reached over the collector's cellular link. Local Mosquitto covers home-yard until the first cellular site lands.
- **Mosquitto** (Proxmox VM at 192.168.1.82, user `hivesense`) — home-yard MQTT broker. Single broker pattern; no per-yard brokers until cellular yards justify the split.
- **InfluxDB 2.8 OSS** (combsense-tsdb LXC) — time-series store. iOS reads via Flux `/api/v2/query` directly — no server-side shim. Three-token auth: admin / telegraf-write / ios-read.
- **Telegraf** (combsense-tsdb LXC) — MQTT consumer subscribed to `combsense/hive/+/reading`. Arrival-time stamping (firmware `t=0` pre-NTP would otherwise reject); firmware `t` preserved as `sensor_ts` field for forensic alignment.
- **GitHub Actions** (`.github/workflows/`) — CI builds for all four firmware envs + native unit tests.
- **Ollama on 192.168.1.16** (qwen3-coder:30b) — pre-PR code review (mandatory) and boilerplate generation (saves Claude tokens).

## What Does NOT Exist Here

- **iOS app code** — separate repo `sjordan0228/combsense` (SwiftUI + SwiftData). This repo only contains the firmware/backend that the app talks to.
- **3D enclosure CAD files** — planned in README BOM but not yet started.
- **Easy Bee Counter firmware** — Phase 2; pivoted to upstream open-source 2019-easy-bee-counter PCB by hydronics2 (24 gates / 48 sensors). Lives in sibling repo `/Users/sjordan/Code/2019-easy-bee-counter/`. Publishes `combsense/hive/<id>/bees/in`, `bees/out`, `bees/activity` — separate board, not integrated into sensor-tag-wifi.
- **HiveMQ Cloud account / credentials** — local Mosquitto covers all current deployments. Will be set up when the first cellular yard goes live.
- **Per-service Docker** — the LXC stack is deliberately native Debian 12 + systemd. Docker on Proxmox NFS has known storage and networking footguns.
