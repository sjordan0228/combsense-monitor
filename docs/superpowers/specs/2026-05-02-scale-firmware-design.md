# Scale Calibration Firmware — Design Spec

**Repo:** `hivesense-monitor` (firmware target: `firmware/sensor-tag-wifi/`)
**Date:** 2026-05-02
**Branch target:** dev (PR into main per project workflow)
**Targets shipping firmware version:** `>= e4d9ea0 + 1`

---

## 1. Goal

Add scale (weight) sampling, calibration, and bidirectional MQTT command-response support to the `sensor-tag-wifi` firmware, so the iOS CombSense app's calibration wizard and Quick Re-tare button can drive an HX711 + 4-cell load cell over the air via the MQTT contract at `.mex/scale-mqtt-contract.md`.

The firmware must:
1. Sample weight on each wake cycle and publish it alongside existing fields
2. Honor the iOS-side MQTT command/response contract on `scale/cmd` ↔ `scale/status`
3. Honor the keep-alive-until retained config addendum (section 10 of the contract) so the deep-sleep model can coexist with iOS's interactive wizard

## 2. Scope

### In scope (Phase 1)

- HX711 driver integration via `bogde/HX711` library
- New module `firmware/sensor-tag-wifi/src/scale.{h,cpp}` containing the scale-side state machine
- New build env `xiao-c6-ds18b20-scale` for the XIAO ESP32-C6
- Extension of `Reading` struct + payload serializer to include weight in kg
- Subscribe + handle scale/cmd commands: `tare`, `calibrate`, `verify`, `stream_raw`, `stop_stream`, `modify_start`, `modify_end`, `modify_cancel`
- Publish scale/status events: `awake`, `tare_saved`, `calibration_saved`, `verify_result`, `raw_stream`, `modify_started`, `modify_complete`, `modify_warning`, `modify_timeout`, `error`
- Extended-awake mode driven by retained `scale/config` keep-alive
- Clock-skew-tolerant `keep_alive_until` evaluation (±5 min)
- Native unit tests for calibration math, stable detector, JSON serialization, keep-alive logic
- Bench validation against `/tmp/scale-mock-responder.py`

### Out of scope (deferred to Phase 1.5+)

- Consolidation with `firmware/hive-node/` HX711 implementation — separate refactor PR
- Feather S3 build target — wait for hardware
- Telegraf parser update for the new `w` field — separate ops change
- Grafana panel for weight — separate dashboard change
- Real Li-ion SOC curve (issue #10) — independent of this work
- Spam-Re-tare-friendly debounce — accept the strict iOS interpretation for v1

## 3. References

- `.mex/scale-mqtt-contract.md` — canonical iOS↔firmware contract (sections 1–10)
- `.mex/scale-mock-responder.py` — Python reference implementation of firmware-side behavior, used for bench validation
- `firmware/sensor-tag-wifi/src/main.cpp` — sample/wake/MQTT cycle owns the integration point
- `firmware/sensor-tag-wifi/src/serial_console.cpp` — defines NVS keys `weight_off` and `weight_scl` (already present, unused today)
- `firmware/hive-node/` — has working HX711 code we model the driver layer after, but do NOT touch in Phase 1

## 4. Architecture

```
                ┌──────────────────────────────────────────────────────┐
                │                 firmware/sensor-tag-wifi              │
                │                                                      │
   wake ───────►│ main.cpp (existing)                                  │
                │   ├─ sensor_ds18b20::sample()                        │
                │   ├─ Battery::readVbat()                             │
                │   └─ Scale::sample()  ◄── NEW                        │
                │                                                      │
                │   MqttClient::connect()                              │
                │     ├─ subscribe scale/cmd     ◄── NEW               │
                │     ├─ subscribe scale/config  ◄── NEW               │
                │     └─ on retained scale/config:                     │
                │         Scale::enterExtendedAwakeMode()  ◄── NEW     │
                │                                                      │
                │   while (extended_awake):    ◄── NEW                 │
                │     pubsub.loop()                                    │
                │     Scale::heartbeatTick()                           │
                │     Scale::dispatch(commands)                        │
                │     if (Scale::keepAliveExpired()): break            │
                │                                                      │
                │   normal: publish reading, deep_sleep(300s)          │
                └──────────────────────────────────────────────────────┘

                ┌──────────────────────────────────────────────────────┐
                │            firmware/sensor-tag-wifi/src/scale.{h,cpp} │
                │                                                      │
                │  ┌────────────────┐  ┌──────────────────────────┐    │
                │  │ HX711 driver   │  │ State machine            │    │
                │  │ (bogde lib)    │  │  - idle                  │    │
                │  └────────────────┘  │  - extended-awake        │    │
                │                      │  - streaming (1 Hz pub)  │    │
                │  ┌────────────────┐  │  - modify (label active) │    │
                │  │ Calibration    │  └──────────────────────────┘    │
                │  │ math (pure C++)│                                  │
                │  └────────────────┘                                  │
                │                      ┌──────────────────────────┐    │
                │  ┌────────────────┐  │ MQTT cmd/event JSON      │    │
                │  │ Stable detector│  │ (ArduinoJson)            │    │
                │  └────────────────┘  └──────────────────────────┘    │
                │                                                      │
                │  ┌────────────────┐                                  │
                │  │ NVS persistence│                                  │
                │  │ (Preferences)  │                                  │
                │  └────────────────┘                                  │
                └──────────────────────────────────────────────────────┘
```

The scale module is self-contained. main.cpp gains 4 call sites: one each for init/sample/extended-awake-loop/deinit. The MQTT subscribe and command-dispatch path lives entirely inside `scale.cpp`.

## 5. Hardware

### XIAO ESP32-C6 pin assignment (this build)

| HX711 pin | C6 silkscreen | Breadboard row | GPIO | Build flag |
|---|---|---|---|---|
| DT (data out) | D6 | row 7 LEFT | 16 | `-DPIN_HX711_DT=16` |
| SCK (clock in) | D7 | row 7 RIGHT | 17 | `-DPIN_HX711_SCK=17` |
| VCC | 3V3 | row 3 RIGHT | — | — |
| GND | GND | − rail | — | — |

D6 and D7 are UART0 TX/RX on the C6 silkscreen. This firmware uses native USB-CDC (`-DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1`), so UART0 is unused. Reusing those pins is safe.

### HX711 module to load cell wiring

Standard 4-cell Wheatstone bridge wiring. Per `firmware/hive-node/` reference + the Adafruit HX711 + 4× 50 kg load cell kit (Amazon B07B4DNJ2L). Detailed wiring lives in the iOS calibration wizard's first-time setup instructions — out of scope for firmware spec.

### Power

HX711 module draws ~1.5 mA active. We power it via 3V3 rail (same as DS18B20). Module's PD (power-down) feature is exposed to firmware via `HX711::power_down()` / `power_up()` — driver calls it between samples to drop draw to ~50 µA.

## 6. Module API

### `firmware/sensor-tag-wifi/src/scale.h`

```cpp
#pragma once

#include <cstdint>

namespace Scale {

/// Power up HX711 and load weight_off / weight_scl from NVS.
void init();

/// Power down HX711. Called before deep-sleep.
void deinit();

/// Read the HX711 once, apply calibration. Used by the regular
/// per-wake reading cycle (not by stream_raw / tare / verify which
/// have their own sampling logic).
///
/// Returns true on success. Sets:
///   raw   — raw HX711 ADC reading
///   kg    — calibrated weight in kilograms
///
/// On HX711 timeout / unresponsive: returns false, kg=NaN, publishes
/// an `error` event via the MQTT path if connected.
bool sample(int32_t& raw, double& kg);

/// Subscribe to scale/cmd and scale/config on the given MQTT client.
/// Idempotent — safe to call after every reconnect.
void subscribe();

/// Process a received MQTT message. Call from the PubSubClient
/// callback for any topic matching scale/cmd or scale/config.
void onMessage(const char* topic, const char* payload, unsigned int len);

/// Drive the extended-awake state machine. Caller invokes in a tight
/// loop while extended-awake mode is active (between deep-sleeps).
/// Internally:
///   - calls pubsub.loop() to flush MQTT
///   - publishes heartbeat if interval elapsed
///   - emits stream_raw events at 1 Hz if streaming is active
///   - exits extended-awake if keep_alive_until has expired
void tick();

/// True if scale is in extended-awake mode (keep_alive_until > now,
/// or fallback NTP-not-synced grace period active).
bool inExtendedAwakeMode();

/// Returns the current keep_alive_until in epoch seconds, or 0 if
/// no retained config is in effect.
int64_t keepAliveUntil();

}  // namespace Scale
```

### Header exposing build-time pin defaults

Add to `firmware/sensor-tag-wifi/include/config.h`:

```cpp
#ifndef PIN_HX711_DT
#define PIN_HX711_DT 16
#endif
#ifndef PIN_HX711_SCK
#define PIN_HX711_SCK 17
#endif
constexpr uint8_t PIN_HX711_DT_  = PIN_HX711_DT;
constexpr uint8_t PIN_HX711_SCK_ = PIN_HX711_SCK;

constexpr uint8_t HX711_GAIN              = 128;
constexpr uint8_t HX711_TARE_SAMPLE_COUNT = 16;
constexpr uint8_t HX711_VERIFY_SAMPLE_COUNT = 16;
constexpr uint8_t HX711_STABLE_WINDOW_LEN = 5;
constexpr int32_t HX711_STABLE_TOLERANCE_RAW = 50;  // ±50 counts
constexpr uint16_t HX711_STREAM_INTERVAL_MS = 1000;
constexpr uint16_t HEARTBEAT_INTERVAL_MS = 60000;
constexpr int64_t  CLOCK_SKEW_TOLERANCE_SEC = 300;
constexpr int64_t  KEEPALIVE_NTP_FALLBACK_SEC = 600;  // grace if NTP not yet synced when retained config arrives
constexpr uint16_t HX711_READ_TIMEOUT_MS = 1000;
constexpr uint16_t MODIFY_DEFAULT_TIMEOUT_SEC = 600;  // 10 min
```

## 7. State machine

```
               ┌────────────────────┐
               │  POWER_DOWN        │   (HX711 PD high, no work)
               │  CPU deep sleep    │
               └─────────┬──────────┘
                         │ wake (RTC alarm or external)
                         ▼
               ┌────────────────────┐
               │  WAKE              │   sensor_ds18b20::sample()
               │  sample sensors    │   Scale::sample()
               │  read NVS          │   Battery::readVbat()
               └─────────┬──────────┘
                         │
                         ▼
               ┌────────────────────┐
               │  CONNECT WIFI/MQTT │
               │  subscribe to      │
               │   scale/cmd        │
               │   scale/config     │
               └─────────┬──────────┘
                         │
                         ▼
               ┌────────────────────┐
               │  CHECK NTP         │  See section 14 — extended-awake
               │                    │  requires NTP synced (else skip
               └─────────┬──────────┘  extended-awake, normal cycle only).
                         │ NTP synced
                         ▼
               ┌────────────────────┐
               │  CHECK RETAINED    │  Wait up to 1.5s after subscribe
               │  scale/config      │  for retained delivery.
               └─────────┬──────────┘
                         │
              ┌──────────┴──────────┐
              │                     │
              ▼                     ▼
      ┌──────────────┐     ┌────────────────────┐
      │ NORMAL CYCLE │     │  EXTENDED-AWAKE    │
      │ publish      │     │  publish reading   │
      │ reading,     │     │  publish "awake"   │
      │ deep sleep   │     │  loop:             │
      └──────────────┘     │   pubsub.loop()    │
                           │   dispatch cmds    │
                           │   1 Hz raw_stream  │
                           │     (if streaming) │
                           │   60s heartbeat    │
                           │   exit if          │
                           │     keep_alive     │
                           │     expired        │
                           └─────────┬──────────┘
                                     │ exit
                                     ▼
                            (back to NORMAL CYCLE
                             then deep sleep)
```

### Decision: how long does firmware wait for the retained `scale/config` after subscribe?

**1.5 seconds.** Rationale:
- MQTT 3.1.1 retained messages are delivered immediately on subscribe
- Mosquitto + LAN typical RTT < 50 ms
- 1.5 s is generous but not slow (the user's 6 min timeout has plenty of headroom)
- After the wait, if no retained message, default to NORMAL CYCLE

Implementation: `pubsub.loop()` for 1500 ms after subscribe completes; track whether `scale/config` callback fired during that window.

### Decision: idle-timeout in extended-awake mode

**No idle timeout.** Stay awake until `keep_alive_until` expires (modulo clock-skew tolerance). The keep-alive is iOS's responsibility; if the iOS app crashes, the iOS-side keep-alive value will naturally expire within 30 minutes and firmware exits.

This is simpler than nesting an idle timeout inside an external timeout.

### Decision: extended-awake never re-enters from a single wake

If keep_alive_until expires mid-loop, firmware completes any pending command response, then exits to NORMAL CYCLE. It does not re-check the retained scale/config to see if iOS extended it. Next wake re-evaluates fresh.

This keeps the loop simple. The cost is an extra 5-min latency if iOS extends mid-session, which is acceptable per the contract's "wizard latency budget."

## 8. Calibration math

All math in pure C++, unit-testable in the `native` env.

### `kg = (raw - weight_off) / weight_scl`

- `weight_off`: int64_t, raw HX711 counts representing zero-load. NVS key: `weight_off`. Default: 0.
- `weight_scl`: double, raw counts per kg. NVS key: `weight_scl`. Default: 1.0 (so uncalibrated readings just return raw counts as kg, an obvious-bad value the user/iOS will recognize).

### `tare` command

1. Set HX711 power-up if not already
2. Sample 16 raw values, ~10 ms apart (HX711 is rate-limited to 10 Hz at default 80 Hz crystal)
3. Compute mean → `new_offset`
4. Write `weight_off = new_offset` to NVS
5. Update in-memory cached value
6. Publish `{"event":"tare_saved","raw_offset":<new_offset>,"ts":"<rfc3339>"}`

If sampling fails (HX711 timeout): publish `{"event":"error","code":"hx711_unresponsive","details":"tare failed: no DOUT pulse for 1s","ts":"..."}` and don't write NVS.

### `calibrate` command (with `known_kg`)

1. Sample 16 raw values, mean → `avg_raw`
2. Compute `new_scale = (avg_raw - weight_off) / known_kg`
3. Reject if `|new_scale|` < 1.0 (sanity: scale factor of <1 raw count per kg means the cell isn't connected or the weight isn't actually on the scale)
4. Write `weight_scl = new_scale` to NVS
5. Update in-memory cached value
6. Compute `predicted_accuracy_pct`: simple stub for v1 — return `(stddev_of_samples / avg_raw) * 100`. iOS doesn't actually display this number per contract section 3.3, so any sensible value works.
7. Publish `{"event":"calibration_saved","scale_factor":<new_scale>,"predicted_accuracy_pct":<n>,"ts":"..."}`

If sanity check fails: publish `{"event":"error","code":"calibrate_invalid","details":"scale_factor=<n> below threshold","ts":"..."}`.

### `verify` command (with `expected_kg`)

1. Sample 16 raw values, mean → `avg_raw`
2. Compute `measured_kg = (avg_raw - weight_off) / weight_scl`
3. Compute `error_pct = abs(measured_kg - expected_kg) / abs(expected_kg) * 100` (handle expected_kg == 0 by short-circuiting to a special error)
4. Publish `{"event":"verify_result","measured_kg":<m>,"expected_kg":<e>,"error_pct":<p>,"ts":"..."}`

Firmware does NOT bucket the result (pass/marginal/fail). iOS does that locally per contract section 3.4.

## 9. NVS schema additions

These keys already exist in the `serial_console.cpp` known-keys list but are unused today.

| NVS key | Type | Default | Owner | Notes |
|---|---|---|---|---|
| `weight_off` | int64_t | 0 | scale module | Stored as int64 string in NVS via Preferences. |
| `weight_scl` | double | 1.0 | scale module | Stored as double via `Preferences.getFloat()` family. |

`Scale::init()` reads both at startup and caches them. `tare` and `calibrate` write back through.

## 10. Payload extension (per-wake reading)

**Important:** iOS today does NOT decode `w` from the `reading` JSON payload (it has no `w` field in `ReadingPayload`). It reads weight ONLY from the dedicated `combsense/hive/<id>/weight` topic. If firmware emits only `reading.w`, iOS silently loses weight readings.

**Decision:** Publish to BOTH topics. The dual-publish is one extra line of firmware code and lets iOS work today with zero changes; the `reading.w` field is forward-looking for Telegraf parsing and a future iOS consolidation PR.

### `combsense/hive/<id>/reading` (extended)

Current payload:

```json
{"id":"c513131c","v":"e4d9ea0","t":1777759713,"t1":23.38,"t2":null,"vbat_mV":3505,"rssi":-87,"b":22}
```

Add one field:

```json
{"id":"c513131c","v":"e4d9ea0","t":1777759713,"t1":23.38,"t2":null,"vbat_mV":3505,"rssi":-87,"b":22,"w":47.32}
```

| Field | Type | Notes |
|---|---|---|
| `w` | number (double) | Calibrated kg from `Scale::sample()`. **Omit field entirely if Scale isn't compiled in (no -DSENSOR_SCALE).** Set to `null` if scale was compiled in but sample failed (HX711 unresponsive). Set to a number otherwise. |

### `combsense/hive/<id>/weight` (separate dedicated topic)

Plain numeric payload (no JSON wrapper), e.g. `47.32`. Same `w` value as in the reading payload. Published immediately after the `reading` publish, retain=false.

This is what iOS's `MQTTService.handleMessage` `case "weight":` branch reads today (sets `SensorReading.weightKg`).

### Telegraf

Telegraf parser needs to learn about `w` in the `reading` JSON — separate ops change in `deploy/tsdb/telegraf-combsense.conf`, not blocking firmware ship.

### Future cleanup

Once iOS lands a follow-up PR adding `w: Double?` to `ReadingPayload` and the corresponding field-extraction line, firmware can drop the dedicated `weight` topic publish. Out of scope for this spec.

## 11. MQTT command/event handlers

This section maps the contract verbatim to firmware behavior. Each command is a single function in `scale.cpp`.

| Command | Handler | Pre-conditions | Post-conditions |
|---|---|---|---|
| `tare` | `cmdTare()` | HX711 powered up | Publishes `tare_saved` (or `error`); writes NVS |
| `calibrate` | `cmdCalibrate(known_kg)` | HX711 powered up; weight on scale | Publishes `calibration_saved` (or `error`); writes NVS |
| `verify` | `cmdVerify(expected_kg)` | HX711 powered up; reference weight on scale | Publishes `verify_result` (or `error`) |
| `stream_raw` | `cmdStreamRaw(duration_sec)` | HX711 powered up | Sets `streaming = true` and `stream_until = now + duration_sec`. `tick()` will publish raw_stream events at 1 Hz until `stream_until` passes. A new stream_raw resets `stream_until`. |
| `stop_stream` | `cmdStopStream()` | — | Sets `streaming = false`. No event published — the stream just stops. |
| `modify_start` | `cmdModifyStart(label)` | HX711 powered up | Sample current weight as `pre_event_kg`. Store `(label, pre_kg, modify_started_ts)` in RAM. Set `modify_timeout_at = now + MODIFY_DEFAULT_TIMEOUT_SEC`. Publishes `modify_started`. |
| `modify_end` | `cmdModifyEnd(label)` | A `modify_start` is in progress with matching `label` | Sample current weight as `post_kg`. Compute `delta_kg = post_kg - pre_kg`. **If `\|delta_kg\| < 0.2`** publish `modify_warning` with `warning="no_significant_change_detected"`. Else publish `modify_complete` with `tare_updated=false` (firmware does NOT auto-re-tare for v1; user re-tares explicitly via wizard). Clear in-memory modify state. If label doesn't match the in-flight `modify_start`, publish `error` with `code="modify_label_mismatch"` and don't clear state. |
| `modify_cancel` | `cmdModifyCancel()` | A modify_start is in progress | Clear in-memory modify state. No event published — the modify is silently dropped. |

### `tick()` responsibilities

Called in a loop while in extended-awake mode:

```cpp
void Scale::tick() {
    pubsub.loop();  // process any inbound MQTT

    int64_t now = ntpSyncedEpochSec();

    // Heartbeat
    if (now - lastHeartbeatAt >= HEARTBEAT_INTERVAL_MS / 1000) {
        publishHeartbeat();
        lastHeartbeatAt = now;
    }

    // Streaming
    if (streaming && now < stream_until) {
        if (millis() - lastStreamAt >= HX711_STREAM_INTERVAL_MS) {
            int32_t raw;
            double kg;
            bool ok = Scale::sample(raw, kg);
            if (ok) {
                publishRawStream(raw, kg, stableDetector.isStable());
            }
            lastStreamAt = millis();
        }
    } else if (streaming && now >= stream_until) {
        // Stream window expired naturally
        streaming = false;
    }

    // Modify timeout
    if (modifyActive && now >= modify_timeout_at) {
        publishModifyTimeout(modify_label);
        modifyActive = false;
    }

    // Keep-alive expiry → caller breaks out of loop next iteration
}
```

### Heartbeat payload

```json
{"event":"awake","keep_alive_until":"2026-05-02T20:30:00Z","ts":"2026-05-02T20:01:14Z"}
```

Published:
- Once on entering extended-awake mode (immediately after subscribe + retained scale/config processed)
- Every 60 s thereafter while in extended-awake mode

iOS uses the first one as the wake-up confirmation; later ones are dropped harmlessly. Firmware publishes regardless — useful for broker-side diagnostics.

## 12. Stable detector

```cpp
class StableDetector {
public:
    void push(int32_t raw);  // appends to ring buffer
    bool isStable() const;   // true if last N samples within ±tolerance

private:
    int32_t ring_[HX711_STABLE_WINDOW_LEN] = {};
    uint8_t count_ = 0;
    uint8_t head_ = 0;
};

bool StableDetector::isStable() const {
    if (count_ < HX711_STABLE_WINDOW_LEN) return false;
    int32_t min = ring_[0], max = ring_[0];
    for (uint8_t i = 1; i < HX711_STABLE_WINDOW_LEN; i++) {
        min = std::min(min, ring_[i]);
        max = std::max(max, ring_[i]);
    }
    return (max - min) <= HX711_STABLE_TOLERANCE_RAW;
}
```

Simple ±50-count window over last 5 raw readings. iOS uses `stable=true` to gate the Tare/Verify buttons, so it must clearly transition from `false` (initial samples) to `true` (cell quiet).

The detector resets on `init()` and on every `tare`/`calibrate`/`verify` command (those use their own averaging, separate from streaming).

## 13. Clock-skew tolerance

When parsing `keep_alive_until` from the retained `scale/config`:

```cpp
bool isKeepAliveValid(int64_t keepAliveUntil, int64_t now) {
    if (keepAliveUntil > now) return true;                                      // future, definitely valid
    if (keepAliveUntil > now - CLOCK_SKEW_TOLERANCE_SEC) return true;           // small skew, accept
    return false;                                                               // truly past, treat as cleared
}
```

Where `CLOCK_SKEW_TOLERANCE_SEC = 300` (5 min). This handles iOS clock drift up to ±5 min.

If NTP hasn't synced at the moment we evaluate the retained config:
- Treat the keep-alive as valid for a fixed `KEEPALIVE_NTP_FALLBACK_SEC = 600` (10 min) from receipt
- This is the "fallback" path described in contract section 10.3

## 14. RFC3339 timestamp emission and NTP gating

All status events MUST include `ts` as RFC3339 with `Z` suffix, no fractional seconds:

```cpp
// Helper
size_t formatRFC3339(int64_t epoch, char* buf, size_t bufsz) {
    time_t t = (time_t)epoch;
    struct tm utc;
    gmtime_r(&t, &utc);
    return strftime(buf, bufsz, "%Y-%m-%dT%H:%M:%SZ", &utc);
}
```

iOS accepts both with and without fractional, per contract section 3 — we send without for cleanliness.

### NTP gating for extended-awake mode

iOS persists `ts` from `tare_saved` → `Hive.lastTareAt`, from `calibration_saved` → `Hive.lastCalibratedAt`, from a calibration completion → `HiveCalibration.performedAt`. If those events fire with epoch-zero timestamps (1970), iOS happily decodes them and SwiftData ends up with wrong-dated calibration records.

**Rule: firmware does not enter extended-awake mode until NTP has synced for the current wake.**

```
WAKE → connect WiFi → start NTP query
                 │
       ┌─────────┴─────────┐
       │                   │
   NTP synced          NTP failed
   within 5s           after retries
       │                   │
       ▼                   ▼
   connect MQTT        connect MQTT,
   subscribe to        publish reading,
   scale/cmd +         skip extended-awake,
   scale/config        deep_sleep(300s)
       │
       ▼
   wait 1.5s for retained scale/config
       │
   ┌───┴───┐
   │       │
   no      yes (and keep_alive_until valid)
   │       │
   ▼       ▼
 normal  extended-awake mode
 cycle    (NTP guaranteed synced — all ts values accurate)
```

This means the only events that can ever fire under extended-awake have accurate timestamps. The `awake` heartbeat, `raw_stream`, `tare_saved`, `calibration_saved`, `verify_result`, `modify_*`, and `error` events are all guaranteed NTP-synced.

If NTP fails for an entire wake cycle, the user sees "Sensor offline — check power and try again" in iOS (because firmware doesn't enter extended-awake → iOS times out at 360s waiting for `awake`). They retry. If NTP keeps failing, the right diagnosis is network/NTP infrastructure, not a calibration bug. Acceptable failure mode.

### Outside extended-awake (the regular reading publish)

The per-wake `reading` publish currently uses `t` field as Unix epoch seconds (already 0 if NTP failed — see existing log: `"t":0` on first reading after wake). That's fine — Telegraf timestamps the row at write-time. No change.

## 15. HX711 power management

```
init()      → HX711::power_up() → wait for first DOUT pulse → ready
sample()    → HX711::wait_ready_timeout(1000) → HX711::read() → return raw
deinit()    → HX711::power_down()
```

Between samples (within a wake cycle), HX711 stays powered up — power-cycling adds 0.5 s settling time per read which is too slow for streaming.

Before deep_sleep, call `Scale::deinit()` to drop HX711 to ~50 µA standby.

## 16. Build flag wiring

Add to `firmware/sensor-tag-wifi/platformio.ini`:

```ini
[env:xiao-c6-ds18b20-scale]
extends = env:xiao-c6-ds18b20
lib_deps =
    ${env:xiao-c6-ds18b20.lib_deps}
    bogde/HX711@^0.7.5
build_flags =
    ${env:xiao-c6-ds18b20.build_flags}
    -DSENSOR_SCALE
    -DPIN_HX711_DT=16
    -DPIN_HX711_SCK=17
    -DOTA_VARIANT=\"ds18b20-scale\"
```

The `xiao-c6-ds18b20` env stays unchanged. The currently-deployed `c513131c` tag continues to pull `ds18b20`-variant OTA firmware — to upgrade it to scale, the user (or admin) flips its OTA manifest variant string after the new build is verified.

## 17. Tests

### Native env (`pio test -e native`)

| Test file | Coverage |
|---|---|
| `test/test_scale_calibration.cpp` | tare math, calibrate math, verify math, edge cases (zero expected_kg, scale_factor sanity bounds) |
| `test/test_stable_detector.cpp` | ring-buffer correctness, stable transitions on increasing/decreasing series, edge cases (empty, partial fill) |
| `test/test_keep_alive.cpp` | clock-skew tolerance edge cases (future, past < 5min, past > 5min, NTP not synced fallback) |
| `test/test_scale_payload.cpp` | JSON serialize for every status event variant; JSON parse for every command variant; round-trip tests using `.mex/scale-mock-responder.py` example payloads |
| `test/test_scale_state.cpp` | state-machine transitions: idle → ext-awake → idle; tare during stream; modify_start → modify_end happy path |

### On-device bench validation

After native tests pass:

1. Wire HX711 + 4-cell load cell to bench tag (XIAO C6) per section 5
2. Flash `xiao-c6-ds18b20-scale` via PlatformIO
3. Confirm in serial console: HX711 detected, default values loaded from NVS
4. Use `.mex/scale-mock-responder.py` as a behavioral reference (read its source); do NOT run it concurrently with the bench firmware — both would respond to commands and double-publish. Drive the firmware manually:
5. Publish via `mosquitto_pub`:
   - `combsense/hive/<bench_id>/scale/config` with retain `{"keep_alive_until":"<now+10min>"}` — verify firmware enters extended-awake mode and publishes `awake`
   - `scale/cmd` `{"cmd":"stream_raw","duration_sec":30}` — verify ~30 raw_stream events
   - `scale/cmd` `{"cmd":"tare"}` while streaming — verify `tare_saved` and stream continues
   - `scale/cmd` `{"cmd":"calibrate","known_kg":1.0}` with a 1 kg reference — verify `calibration_saved` with sane scale factor
   - `scale/cmd` `{"cmd":"verify","expected_kg":1.0}` — verify `verify_result` with low error_pct
   - `scale/config` empty payload — verify firmware exits extended-awake on next tick
6. End-to-end iOS test: hand off to iOS session for wizard / Quick Re-tare validation against the bench tag

## 18. OTA strategy

The new build uses a different `OTA_VARIANT` (`ds18b20-scale`) so the existing `c513131c` tag (variant `ds18b20`) is not auto-upgraded. To migrate the bench tag onto the new firmware:

1. Build + upload the binary to the OTA host
2. Update the `ds18b20-scale` manifest entry pointing at the new binary
3. To upgrade the bench tag, manually flash `xiao-c6-ds18b20-scale` once (so the variant string in NVS becomes `ds18b20-scale`)
4. Future updates auto-pull from the `ds18b20-scale` manifest

Production tags (eventual home-yard scale-equipped tags) follow the same path.

## 19. Open items / decisions deferred

1. **`predicted_accuracy_pct` formula** — stub for v1 (sample stddev / avg_raw * 100). Real predictive accuracy needs more thought; iOS doesn't display it today so the stub is fine.
2. **HX711 DOUT-pin pull-up** — verify at implementation time whether the HX711 module has a built-in pull-up. If not, enable internal pull-up on PIN_HX711_DT in `init()`.
3. **Bench wiring physical conflict** — DT/SCK on D6/D7 sit near where the bench DS18B20 was originally wired. Verify the breadboard has free rows when wiring up.

## 20. Risk register

| Risk | Mitigation |
|---|---|
| HX711 timeout under low cell voltage | Detect via `wait_ready_timeout(1000)`, publish `error` event, set `kg=null` in next reading, continue normal cycle |
| MQTT publish fails inside extended-awake (broker briefly unreachable) | PubSubClient retries internally; if disconnected, the next wake will attempt fresh subscribe + receive any queued retained config |
| `stream_raw` storms with iOS sending 90 s windows back-to-back | Hard-cap stream_until to now + 120 s regardless of received duration_sec — prevents firmware getting stuck streaming forever from a stale command |
| Cell voltage sags into protection cutoff during extended-awake | Per `project_tp4056_protection_cutoff.md` memory — known issue, no firmware mitigation possible. iOS user will see "Sensor offline — check power and try again" |
| iOS sends `modify_end` with wrong label | Reject silently or publish error event — TBD; for v1, publish `error` with code `modify_label_mismatch` |
| NVS write storm during stream of taps | Tare/calibrate/verify all write NVS. NVS write endurance is ~100k cycles per key. Even 1000 calibrations is < 1% of life. No mitigation needed. |

## 21. Acceptance criteria

This work is complete when:

- [ ] `pio test -e native` passes for all new test files
- [ ] `xiao-c6-ds18b20-scale` env builds clean with no warnings
- [ ] Bench tag with HX711 + load cell wired up:
  - [ ] Publishes `reading` payload with `w` field every wake cycle
  - [ ] Enters extended-awake mode on retained `scale/config`
  - [ ] Publishes `awake` event on entry + every 60 s
  - [ ] Honors all 8 commands per contract sections 2.1–2.8
  - [ ] Publishes correct status event for each command per contract sections 3.1–3.9
  - [ ] Exits extended-awake on cleared `scale/config` or expired keep_alive_until
- [ ] iOS calibration wizard end-to-end test passes against the bench tag (handoff to iOS session)
- [ ] `dev` branch has the change in a green PR ready for merge to `main`
- [ ] Memory updated with any new operational knowledge discovered during bench validation

---

**Spec version:** 1.0
**Approved by user:** 2026-05-02 (verbal — "looks good!")
**Implementation phase:** ready for plan
