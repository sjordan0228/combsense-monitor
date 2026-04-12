---
name: decisions
description: Architectural decisions and their rationale
last_updated: 2026-04-11
---

# Decisions

## ~~ESP32-WROOM-32 over ESP32-S3~~ → Switched to ESP32-S3
**Decision:** Use Freenove ESP32-S3 Lite (8MB flash) for hive nodes.
**Why:** 8MB flash enables dual OTA partitions. BLE 5.0 improves sync throughput. Native USB. WROOM-32E with 8MB was hard to source; the S3 Lite is readily available at ~$7.

## SHT31 over DHT22
**Decision:** Use Sensirion SHT31 for temperature and humidity.
**Why:** Built-in heater burns off condensation in 50-80% RH hive environment. DHT22 degrades within months inside a hive. SHT31 also has I2C (more reliable than DHT22's single-wire), dual address support (two sensors on same bus), and better accuracy (±0.3°C vs ±0.5°C). Extra $2/sensor is worth the reliability.

## ESP-NOW over WiFi/LoRa
**Decision:** Use ESP-NOW for hive node → collector communication.
**Why:** No router needed (apiaries have no WiFi). 200m range is enough for a yard. Connectionless — no pairing overhead. Built into ESP32 at no extra cost. Lower power than WiFi.

## HiveMQ Cloud over Home Assistant
**Decision:** Use HiveMQ Cloud (free tier) as MQTT broker instead of self-hosted Home Assistant.
**Why:** No server to maintain. Free tier covers 100 connections and 10GB/month — more than enough. iPhone app subscribes directly. Can always migrate to self-hosted Mosquitto ($5/mo VPS) if needed.

## BLE + MQTT dual path
**Decision:** Support both BLE direct (at the yard) and MQTT cloud (remote).
**Why:** Beekeepers need data at the yard without internet (BLE). They also want to check from home (MQTT). Both paths feed the same SwiftData model in the iOS app.

## IR beam-break over thermal/ToF for bee counting
**Decision:** Use IR break-beam pairs for v1 bee traffic counter.
**Why:** Simple, cheap ($2/pair), proven, weather-independent (unlike thermal which fails when ambient matches bee body temp). 4 directional lanes with entrance reducer gives good relative traffic data. Can upgrade to thermal/ToF later.

## ESP32-S3 over WROOM-32
**Decision:** Switch hive node target from ESP32-WROOM-32 (4MB) to Freenove ESP32-S3 Lite (8MB).
**Why:** 8MB flash enables dual OTA partitions with 2.5 MB headroom. BLE 5.0 improves sync throughput. Native USB eliminates UART chip for lower sleep current. ESP32-WROOM-32E with 8MB was hard to source; the S3 Lite is readily available at ~$7.

## NimBLE over Bluedroid
**Decision:** Replace ESP32 BLE Arduino (Bluedroid) with NimBLE-Arduino for BLE stack.
**Why:** Saves 528 KB flash — critical for fitting dual OTA partitions. API is nearly identical. Disabling unused roles (central, observer) saves additional flash. No functional downside for peripheral-only use.

## Collector-Relay OTA
**Decision:** OTA updates flow from iOS app → MQTT → collector → ESP-NOW → hive node.
**Why:** Hive nodes are remote with no internet. Collector has cellular. App-triggered (not automatic) gives explicit control over which nodes update and when. GitHub Releases hosts firmware binaries — free, versioned, existing workflow.

## Resumable Chunked OTA
**Decision:** OTA transfers use sequential 244-byte chunks over ESP-NOW with NVS progress tracking.
**Why:** ESP-NOW max payload is 250 bytes. Sequential ACK is simple and reliable. NVS progress survives sleep cycles. esp_ota_begin erases the partition so resume restarts from chunk 0, but full transfer completes in 1-2 wake cycles (~30-60 min) which is acceptable.

## TinyGSM for modem abstraction
**Decision:** Use TinyGSM library for SIM7080G modem control on the collector.
**Why:** Abstracts AT commands behind familiar Client interface. Supports SIM7080G's native MQTT stack — TLS terminates on the modem, not the ESP32. Well-tested with PlatformIO.

## Batch MQTT publishing
**Decision:** Collector buffers ESP-NOW payloads in RAM and publishes once per 30-min cycle.
**Why:** Modem is the biggest power draw. One modem-on per cycle (~30 sec) instead of per-node saves significant battery. iOS app already expects 30-min latency.

## Always-listening ESP-NOW (no scheduled windows)
**Decision:** Collector stays in light sleep with ESP-NOW radio active at all times.
**Why:** ESP32 internal RTC drifts 5-10 min/day. Scheduled listen windows would desync with hive nodes within days. NTP fixes collector drift but nodes have no internet. Time sync broadcast helps but adds fragility. Always-listening at ~5-8 mA is sustainable with the 5-10W solar panel.

## Time sync every publish cycle
**Decision:** Collector broadcasts TIME_SYNC via ESP-NOW after every MQTT publish, not once daily.
**Why:** Negligible cost (~100 µs per broadcast). Keeps hive node clocks accurate to within 30 minutes. Simpler logic — no "did I sync today" tracking.

## Multiplexer for IR array
**Decision:** Use 2× CD74HC4067 multiplexers for 8 IR pairs instead of direct GPIO.
**Why:** Saves 10 GPIO pins (20 → 10). Enables pulsed operation for power savings. $1/chip. Channels 8-15 available for future expansion to 16 pairs.
