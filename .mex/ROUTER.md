---
name: router
description: Session bootstrap and navigation hub. Read at the start of every session before any task.
edges:
  - target: AGENTS.md
    condition: when needing project identity, non-negotiables, or commands
  - target: context/architecture.md
    condition: when working on system design, integrations, or understanding how components connect
  - target: context/stack.md
    condition: when working with specific technologies, libraries, or making tech decisions
  - target: context/conventions.md
    condition: when writing new code, reviewing code, or unsure about project patterns
  - target: context/decisions.md
    condition: when making architectural choices or understanding why something is built a certain way
  - target: context/setup.md
    condition: when setting up the dev environment or running the project for the first time
  - target: patterns/INDEX.md
    condition: when starting a task — check the pattern index for a matching pattern file
last_updated: 2026-05-03
---

## Infrastructure

- **Mosquitto broker:** 192.168.1.82:1883 (user `hivesense`)
- **combsense-tsdb LXC:** 192.168.1.19 — Proxmox LXC 124, NFS-backed, Debian 12 (unprivileged)
  - **InfluxDB 2.8** on `:8086`, org `combsense`
    - Buckets: `combsense` (raw, 30d) / `combsense_1h` (hourly, 365d) / `combsense_1d` (daily, ∞)
    - Measurement: `sensor_reading`. Tag: `sensor_id`. Fields: `t1`, `t2`, `b`, `sensor_ts`.
    - Downsample tasks live in Influx metadata; canonical copies in `deploy/tsdb/downsample-1h.flux` and `deploy/tsdb/downsample-1d.flux`
  - **Grafana 13** on `:3000` — `combsense-home-yard` dashboard provisioned (temp °F, battery, last-seen); JSON canonical at `deploy/tsdb/grafana/home-yard-sensors.json`
  - **Telegraf** — MQTT consumer `combsense/hive/+/reading` → Influx; config mirrored at `deploy/tsdb/telegraf-combsense.conf`
  - Tokens at `/root/.combsense-tsdb-creds` (mode 600): `admin_token`, `telegraf_write_token`, `ios_read_token`
  - Systemd sandboxing drop-ins at `/etc/systemd/system/{grafana-server,telegraf}.service.d/override.conf` (required for unprivileged LXC — see memory)
  - Daily Influx backup via `combsense-backup.timer` → `/var/backups/combsense-tsdb/`, 14-day retention
- **combsense-web LXC:** 192.168.1.61 — Proxmox LXC 125, NFS-backed, Debian 12 (unprivileged)
  - **PostgreSQL 15** on 127.0.0.1:5432 (db: `combsense`, user: `combsense`)
  - **Redis 7** on 127.0.0.1:6379 (for Celery later)
  - **Django (gunicorn)** on 127.0.0.1:8000 via `combsense-web.service`
  - **nginx 1.22** on :80 / :443 — reverse-proxies gunicorn, serves `/static/` directly, self-signed TLS for `dashboard.combsense.com` (cert at `/etc/ssl/combsense/`)
  - Credentials at `/root/.combsense-web-creds` (mode 600)
  - Systemd drop-in at `/etc/systemd/system/combsense-web.service.d/override.conf` (unprivileged LXC workaround)
- **Remote:** only `origin` on GitHub (`sjordan0228/combsense-monitor`). Branches: `main` (prod), `dev` (integration).

# Session Bootstrap

Read this file fully before doing anything else in this session.

## Current Project State

**Phase: home-yard pipeline live + scale calibration shipped (PR #33 open) + per-hive feature flags in progress (feat-feature-flags branch). Apiary-side hive-node + collector await hardware.**

### Completed
- Hardware datasheet and design spec (README.md)
- Hive node firmware (`firmware/hive-node/`) — Freenove ESP32-S3 Lite (8MB flash)
  - State machine with power-aware sleep/wake cycle
  - SHT31 dual temp/humidity, HX711 weight, battery ADC
  - ESP-NOW with packet wrapping, TIME_SYNC receive, OTA routing
  - BLE GATT server (NimBLE) with sensor log sync and pairing
  - LittleFS circular buffer storage (500 readings)
  - OTA update receive module with CRC32 and auto-rollback
  - BLE tag reader for wireless internal sensor tag
  - Build: 27.7% flash (1.0 MB of 3.5 MB)
- Yard collector firmware (`firmware/collector/`) — LilyGO T-SIM7080G
  - ESP-NOW receiver with payload buffering and MAC tracking
  - Cellular module (TinyGSM, SIM7080G) with NTP sync
  - MQTT batch publisher to HiveMQ Cloud
  - OTA relay (download from GitHub, chunk to hive nodes via ESP-NOW)
  - Self-OTA (download and apply own firmware)
  - Time sync broadcast to hive nodes every publish cycle
  - Build: 24.5% flash (900 KB of 3.5 MB)
- Shared protocol headers (`firmware/shared/`)
- Wireless sensor tag firmware (`firmware/sensor-tag/`) — XIAO ESP32-C6 (default), Adafruit Feather S3 (`feather-s3` env), Freenove S3 Lite MINI-1 (`freenove-s3-lite` env) — all BLE remote sensors
  - BLE advertisement with temp/humidity from SHT31
  - Deep sleep cycle (60s interval), CR2032 powered
  - Build: 38% flash (1.2 MB of 3.0 MB)
- Sensor tag WiFi variant (`firmware/sensor-tag-wifi/`) — XIAO ESP32-C6 for home-yard deployments
  - Compile-time sensor abstraction (SHT31 dual / DS18B20 dual)
  - Direct MQTT to local Mosquitto, RTC ring buffer for offline resilience (288 readings = 24h @ 5-min cadence)
  - BSSID caching in RTC for fast reconnect
  - 18650 + solar powered, 5-min sample cadence by default
  - Native Unity tests: **152 passing** across test_payload, test_scale_payload, test_scale_math, test_scale_commands, test_config_parser, test_config_runtime, test_config_ack, test_config_get, test_capabilities, test_ota_decision, test_ota_manifest, test_ota_sha256, test_ota_validation, test_battery_math
  - `Reading` struct is 28 B (was 24 B; `weight_kg` field added by scale subsystem). `static_assert(sizeof(Reading) == 28)`. RTC ring MAGIC `0xCB50A004`. Ring capacity `RTC_BUFFER_CAPACITY` (288); rtcCount/rtcHead widened to uint16_t.
  - Epoch timestamps via NTP sync in `drainBuffer()` — persists across deep sleep via RTC; pre-sync readings emit `t=0` which Telegraf replaces with arrival time
  - NaN temperatures serialize as JSON `null` (not `nan`) so Telegraf/Swift/Postgres parsers accept them
  - **Fleet-visibility payload:** publishes `v` (firmware version), `vbat_mV` (raw battery ADC), and `rssi` (dBm post-connect) alongside `t1`/`t2`/`b`/`weight_kg`. `vbat_mV` is captured at sample time and is the single source of truth for `battery_pct`. RSSI captured once after WiFi associates.
  - **Scale subsystem** (PR #33, branch `dev`): HX711 + 4× 50 kg load cells. Modules: `scale.{h,cpp}`, `scale_math.{h,cpp}`, `scale_commands.{h,cpp}`. HX711 on D8/D9 (GPIO 19/20). 8 commands (tare, calibrate, verify, stream_raw, stop_stream, modify_start/end/cancel) over `combsense/hive/<id>/scale/cmd`. 10 status events over `scale/status`. Extended-awake mode via retained `scale/config` keep-alive. NaN-out-uncalibrated, MAD-based stable detector, trimmed-mean tare/calibrate. iOS↔firmware MQTT contract at `.mex/scale-mqtt-contract.md`.
  - **Per-hive feature flags** (branch `feat-feature-flags`): `config_runtime.{h,cpp}` with `Config::isEnabled(name)`, `capabilities.{h,cpp}` (boot publish to `combsense/hive/<id>/capabilities`), `config_ack.{h,cpp}` + `config_ack_result.h` (rich ack format). 6 ack categories: ok, unchanged, unknown_key, excluded:<key>, invalid:<detail>, conflict:<other_key>. Mutual exclusion: `feat_ds18b20` ⊕ `feat_sht31`. Boot order: apply config → sample → publish capabilities → publish reading. MQTT contract at `.mex/config-mqtt-contract.md` v1.1.
  - **Per-hive runtime config/state topics:** `/config/get` + `/config/state` pair (§7 of config contract). `last_boot_ts` persists as true cold-boot epoch via RTC_DATA_ATTR with MAGIC validity.
  - USB-CDC serial console provisioning (WiFi/MQTT/OTA creds via `tools/provision_tag.py --ota-host ...`)
  - HTTP-pull OTA on wake (manifest at `http://192.168.1.61/firmware/sensor-tag-wifi/<variant>/manifest.json`, sha256-verified, dual 1.5 MB OTA slots, bootloader auto-rollback if first publish after flash fails). Publish via `deploy/web/publish-firmware.sh <sht31|ds18b20|s3-ds18b20>`. nginx LAN-only allowlist on combsense-web LXC.
  - **Hardware variants:** XIAO ESP32-C6 (envs `xiao-c6-sht31`, `xiao-c6-ds18b20`, `xiao-c6-ds18b20-scale`), Waveshare ESP32-S3-Zero (env `waveshare-s3zero-ds18b20`, `waveshare-s3zero-ds18b20-scale`). Pin map via `-DPIN_I2C_SDA_GPIO` / `-DPIN_I2C_SCL_GPIO` build flags in `platformio.ini`. S3 board flagged as `esp32-s3-devkitc-1`.
  - **S3-Zero variant: NOT RECOMMENDED for solar/deep-sleep deployment.** Stock AMS1117-3.3 LDO needs >4.3V VIN; cannot run from raw 18650 VBAT. MH-CD42 charge+boost auto-shuts when load <45 mA for >32 s. Env kept for future revival; default to C6 `ds18b20` for new deployments.
  - **OTA transport:** raw `WiFiClient` + `IPAddress::fromString` — bypasses `esp_http_client` / `esp-tls` / `getaddrinfo`, which on C6 routes through OpenThread DNS64 and fails (EAI_FAIL/202) for IPv4 literals. WiFi window held across publish + OTA check before disconnect.
  - **HW_BOARD per-env build flag** — `xiao-c6`, `waveshare-s3zero` selects pin maps and board-specific quirks.
- **TSDB stack** (`combsense-tsdb` LXC, `deploy/tsdb/` for canonical configs)
  - Telegraf MQTT → Influx pipeline, arrival-time stamped, firmware `t` preserved as `sensor_ts` field
  - sensor-tag-wifi fleet-visibility fields parsed (`v`→`fw_version` string, `vbat_mV` int, `rssi` int) — all `optional=true` so older payloads still parse during rollout
  - Downsample tasks: 15m cadence into `combsense_1h`, 6h cadence from `_1h` into `combsense_1d`
  - Daily `influx backup` via systemd timer, 14-day retention on disk
- **iOS history feature** (in `sjordan0228/combsense`)
  - `HistoryService` — Flux query client with auto-bucket resolution (raw → 1h → 1d based on range) and CSV parser that accepts both fractional and whole-second RFC3339 timestamps
  - `HiveHistoryView` — Swift Charts view with 24h/7d/30d/1y range picker, reached via NavigationLink from hive detail
  - Settings pane extended with Influx URL + org (AppStorage) and read token (Keychain)

- **combsense-web Plan D live** (`web/`, `deploy/web/`, `combsense-web` LXC, nginx TLS reverse proxy on 192.168.1.61)
  - nginx 1.22 terminates TLS on :443 with self-signed cert at `/etc/ssl/combsense/`
  - :80 redirects to HTTPS; ACME challenge passthrough at `/.well-known/acme-challenge/`
  - gunicorn on 127.0.0.1:8000 behind nginx proxy; static files served directly by nginx
  - env has `DJANGO_SECURE_COOKIES=1`, `DJANGO_CSRF_TRUSTED_ORIGINS`, `DJANGO_DEBUG=0`
  - provision.sh requires `BRANCH=dev` until Plan D merges to main
  - Phase 2: swap to Let's Encrypt cert via certbot, uncomment HSTS header
- **combsense-web Plan A complete** (`web/`, `deploy/web/`, `combsense-web` LXC)
  - `web/combsense/` project package: env-driven `settings.py`, `urls.py` routes admin + accounts + core
  - `web/accounts/`: custom User model (email login, `role` field), `EmailAuthenticationForm`, `CombSenseLoginView`, `CombSenseLogoutView` (POST-only), `accounts:login` / `accounts:logout` / 4 password-reset URLs namespaced
  - `EMAIL_BACKEND` reads from `DJANGO_EMAIL_BACKEND` env (console fallback for dev; Plan D wires SMTP); warning comment on silent prod failure mode
  - `web/core/`: `core:home` template-rendered view (login_required) with logout form + conditional admin link
  - `web/templates/`: `web/templates/base.html` shell, `web/templates/registration/login.html`, password-reset templates; `web/core/templates/core/home.html`
  - `web/requirements.txt` (Django 5.2.13 LTS — upgraded from 5.0.9 to fix Python 3.14 context copy regression)
  - **Tests:** 19 passing across `accounts` and `core` (8 model, 5 auth view, 3 password reset, 3 home view)
  - **Deploy artifacts** (`deploy/web/`): `deploy/web/combsense-web.service` (gunicorn systemd unit, `WorkingDirectory=/opt/combsense-web/web`), `deploy/web/combsense-web.service.d/override.conf` (unprivileged LXC sandboxing workaround), `deploy/web/env.template`, `deploy/web/provision.sh` (idempotent bootstrap via `env --file`; runs migrate/collectstatic from `${INSTALL_DIR}/web`), `deploy/web/README.md` (operator runbook)
  - `web/.venv/` (Python 3.14 locally; LXC runs Python 3.11; not committed); `web/.env` (not committed)

### In Progress / Open
- **Scale calibration** (PR #33, branch `dev`) — open against `main`; HX711 + 4× load-cell scale with iOS MQTT command contract. Open follow-up issues: #28 (temp compensation), #29 (brownout-gate weight publish), #30 (cmdModifyEnd N-sample mean), #31 (disable WiFi PA during calibration), #32 (MAX17048 fuel gauge driver).
- **Per-hive feature flags** (branch `feat-feature-flags`) — PR-1 + PR-2 ready, not yet merged. Runtime `Config::isEnabled()` + capabilities publish + rich ack format.

### Not yet built
- combsense-web Plan B onward: MQTT ingest watcher (auto-claim devices), hive list/detail, Influx reader, Chart.js rendering, alerts, OTA dispatch
- **Easy Bee Counter** (Phase 2, pivoted) — using upstream open-source 2019-easy-bee-counter PCB by hydronics2 (24 gates / 48 sensors). Repo at `/Users/sjordan/Code/2019-easy-bee-counter/` (sibling, not part of this monorepo). Separate board, own MCU, publishes `combsense/hive/<id>/bees/in`, `bees/out`, `bees/activity`. NOT integrated into sensor-tag-wifi.
- CombSense iOS app BLE/MQTT live-reading integration (separate from history)
- 3D printed enclosures and sensor gate
- HiveMQ Cloud account setup (local Mosquitto covers home yard; cellular remote still ahead)

**Related repos:**
- `sjordan0228/combsense` — iOS app (SwiftUI + SwiftData)
- `sjordan0228/combsense-monitor` — this repo (hardware + firmware + TSDB configs)

## Routing Table

| Task type | Load |
|-----------|------|
| Understanding the hardware design | `README.md` (the full datasheet) |
| Working on ESP32 firmware | `firmware/hive-node/` directory |
| Working on collector firmware | `firmware/collector/` directory |
| Working on home-yard WiFi variant | `firmware/sensor-tag-wifi/` directory |
| Sensor-tag-wifi OTA | `firmware/sensor-tag-wifi/src/ota.cpp` and friends (`ota_decision.cpp`, `ota_manifest.cpp`, `ota_sha256.cpp`, `ota_state.cpp`) + `deploy/web/publish-firmware.sh` |
| Sensor-tag-wifi pin map / variants | `firmware/sensor-tag-wifi/include/config.h` + `platformio.ini` build_flags |
| Scale subsystem (HX711, tare/calibrate, MQTT commands) | `firmware/sensor-tag-wifi/src/scale.{h,cpp}`, `scale_math.{h,cpp}`, `scale_commands.{h,cpp}` + `.mex/scale-mqtt-contract.md` |
| Per-hive feature flags / runtime config / capabilities | `firmware/sensor-tag-wifi/src/config_runtime.{h,cpp}`, `capabilities.{h,cpp}`, `config_ack.{h,cpp}` + `.mex/config-mqtt-contract.md` |
| Shared firmware headers | `firmware/shared/` directory |
| Making a design decision | `.mex/context/decisions.md` |
| Writing or reviewing code | `.mex/context/conventions.md` |
| TSDB / Influx / Telegraf / downsampling | `deploy/tsdb/` + Infrastructure section above |
| iOS history feature | `sjordan0228/combsense` repo (separate session) |
| Django web dashboard (combsense-web) | `web/` directory |
| combsense-web LXC ops | deploy/web/README.md |
| Easy Bee Counter (Phase 2) | `/Users/sjordan/Code/2019-easy-bee-counter/` (sibling repo) |

## Behavioural Contract

For every task, follow this loop:

1. **CONTEXT** — Load the relevant context file(s) from the routing table above.
2. **BUILD** — Do the work on `dev` (not `main`). Feature branches off `dev`.
3. **VERIFY** — Build and test.
4. **GROW** — Update context files (this file, `.mex/context/decisions.md`, `.mex/context/conventions.md`) when architecture, deployment topology, or decisions change. Not just ROUTER — the underlying files too. Session-by-session task progress, validation runs, and bench-test journal entries are captured automatically by memsearch (`.memsearch/memory/`) — do not duplicate them here.
