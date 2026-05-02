# CombSense iOS ↔ Firmware Scale Contract

> **Source of truth:** the iOS code in `~/Code/hivesense/CombSense/`, branch `feat-scale-calibration-wizard` (PR #86, merged into `dev`). Anything in older spec/plan docs that conflicts with this is wrong — follow this doc.
>
> **Generated from:**
> - `Services/MQTT/ScaleCommand.swift`
> - `Services/MQTT/ScaleStatusEvent.swift`
> - `Services/MQTT/ScaleEventCorrelator.swift`
> - `Services/MQTT/MQTTPublishError.swift`
> - `Services/MQTTService.swift`
> - `Features/Hive/CalibrationWizardViewModel.swift`
> - `Features/Hive/QuickRetareButton.swift`
> - `CombSenseTests/Services/MQTT/ScaleCommandTests.swift`
> - `CombSenseTests/Services/MQTT/ScaleStatusEventTests.swift`

---

## 1. Topic Strings

`{deviceId}` = the value the app stores in `Hive.sensorMacAddress`. iOS treats this as an opaque string — no parsing, no length assumption. The mock responder uses 8-hex-char IDs like `c5129c68`, but the firmware should accept any URL-safe string the user pairs with.

| Purpose | Topic | Direction | Notes |
|---|---|---|---|
| **Scale commands** (iOS → firmware) | `combsense/hive/{deviceId}/scale/cmd` | iOS publishes | Single command topic, all commands tagged by `cmd` field |
| **Scale status / responses / streaming** (firmware → iOS) | `combsense/hive/{deviceId}/scale/status` | iOS subscribes | All firmware-originated events use this one topic — responses, raw stream, errors, modify lifecycle |

### Topics iOS does **NOT** use for the scale contract

The following were referenced in the original `2026-04-30-scale-calibration-and-tare.md` plan or in the firmware-side question, but **were not shipped**:

- ❌ `combsense/hive/{deviceId}/scale/events` — does not exist. Phase 5 ("permanent event log / quick re-tare confirmation pulled from a separate sticky-retained topic") was never implemented. iOS shows tare/calibration confirmations from `scale/status` reactions and from the SwiftData mirror written by `MQTTService.handleScaleStatus`.
- ❌ `combsense/hive/{deviceId}/scale/config`
- ❌ `combsense/hive/{deviceId}/scale/ack`
- ❌ `combsense/hive/{deviceId}/scale/error`

`modify_*` events, `error` events, and `verify_result` events all come back on `scale/status`.

### Other CombSense topics (for context)

The firmware on this device may also be expected to publish on the existing per-hive telemetry topics handled by `MQTTService.handleMessage`:

- `combsense/hive/{deviceId}/reading` — JSON blob `{"id","t","t1","t2","b","rssi","vbat_mV"}`, "nan" tolerated and rewritten to null
- `combsense/hive/{deviceId}/weight` — single Double, stored to `SensorReading.weightKg`
- `combsense/hive/{deviceId}/battery`, `temp/internal`, `temp/external`, `humidity/internal`, `humidity/external`, `bees/in`, `bees/out`, `bees/activity`

These are **independent of the scale contract** but share the namespace. iOS subscribes to `combsense/hive/+/#` (single subscription, all subtopics).

---

## 2. Command Payloads (iOS → firmware on `scale/cmd`)

**Format:** All commands are JSON objects with a required `"cmd"` field naming the command. Other fields depend on the command. No envelope wrapping.

**QoS:** `1` (exactly once per delivery attempt — see section 6 caveat about your QoS-0-only PubSubClient)
**Retain:** `false`
**Correlation ID:** **None.** iOS does NOT include a `request_id` or `correlation_id`. Correlation is purely "first matching event type wins" — see section 5.

### 2.1 `tare`

```json
{"cmd":"tare"}
```

Used by both the calibration wizard step 1 and the Quick Re-tare button on the Overview tab. Firmware must zero the scale at the current load and respond with `tare_saved`.

### 2.2 `calibrate`

```json
{"cmd":"calibrate","known_kg":10.0}
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `cmd` | string | yes | `"calibrate"` |
| `known_kg` | number (double) | yes | The reference weight currently sitting on the scale, in kilograms. Always >0. iOS converts pounds→kg before sending. |

Firmware must compute the scale factor and respond with `calibration_saved`.

### 2.3 `verify`

```json
{"cmd":"verify","expected_kg":5.0}
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `cmd` | string | yes | `"verify"` |
| `expected_kg` | number (double) | yes | Reference weight currently on scale. iOS converts pounds→kg before sending. |

Firmware reads the current weight, computes error vs expected, responds with `verify_result`. Firmware does NOT need to bucket the result — iOS does that locally (see `CalibrationWizardViewModel.bucket(forErrorPct:)`).

### 2.4 `stream_raw`

```json
{"cmd":"stream_raw","duration_sec":90}
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `cmd` | string | yes | `"stream_raw"` |
| `duration_sec` | integer | yes | How long to stream. iOS sends `90`. |

iOS sends this on calibration wizard appear and **renews every 60 seconds** while the wizard is open. Firmware should publish `raw_stream` events at ~1 Hz for `duration_sec` seconds, then stop on its own. Receiving a new `stream_raw` while one is already running should reset/replace the timer (the mock does this).

### 2.5 `stop_stream`

```json
{"cmd":"stop_stream"}
```

iOS sends this when the calibration wizard is dismissed. Firmware must stop publishing `raw_stream` events.

### 2.6 `modify_start`

```json
{"cmd":"modify_start","label":"added_super_deep"}
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `cmd` | string | yes | `"modify_start"` |
| `label` | string | yes | Free-form short tag describing the manipulation. Examples shipped in tests: `"added_super_deep"`, `"extracted_honey"`, `"inspection_only"`. |

> **Implementation status:** `modify_start` / `modify_end` / `modify_cancel` commands are defined in `ScaleCommand` and round-trip-tested, but **no UI in the iOS app currently sends them** — they're shipped as a forward-compatibility stub for Phase 4 ("Modify Hive flow"). The firmware should still implement them; just be aware nothing on the iOS side will actually publish these today.

### 2.7 `modify_end`

```json
{"cmd":"modify_end","label":"added_super_deep"}
```

Same `label` as the matching `modify_start`. See note above on implementation status.

### 2.8 `modify_cancel`

```json
{"cmd":"modify_cancel"}
```

No payload fields. See note above on implementation status.

---

## 3. Status Payloads (firmware → iOS on `scale/status`)

**Format:** All events are JSON objects with required `"event"` and `"ts"` fields. Other fields depend on event type.

**Required common fields:**
- `event` — string discriminator (see table)
- `ts` — RFC 3339 / ISO 8601 timestamp. iOS accepts both `"2026-04-30T14:23:00Z"` (no fractional) and `"2026-04-30T14:23:00.123Z"` (with fractional). UTC `Z` suffix expected; offsets like `+00:00` will work via ISO8601DateFormatter but UTC-Z is what the mock and tests use.

**Retain:** iOS makes **no use of the retain flag** on incoming status events. The status events are consumed live and the meaningful ones (`tare_saved`, `calibration_saved`) are mirrored into SwiftData (`Hive.lastTareAt`, `Hive.lastCalibratedAt`). Firmware should publish with `retain=false` for all stream events. For terminal-state events (`tare_saved`, `calibration_saved`) the firmware MAY publish with `retain=true` if you want a fresh-app-install to learn last-known state from the broker, but iOS won't behave differently — its persistence is local.

> **Recommendation:** Publish all `scale/status` events with `retain=false`. iOS does not depend on retain semantics.

### 3.1 `raw_stream`

```json
{"event":"raw_stream","raw_value":5678901,"kg":47.3,"stable":true,"ts":"2026-04-30T14:23:00Z"}
```

| Field | Type | Notes |
|---|---|---|
| `raw_value` | integer | Raw HX711 reading (uncalibrated). Displayed in monospaced font in the wizard for diagnostic purposes. |
| `kg` | number | Calibrated kg (firmware applies current scale_factor + tare_offset). |
| `stable` | bool | Firmware-decided "is the reading settled?" flag. iOS gates the Tare/Verify buttons on this. Suggested rule from the mock: `stable=false` for first 5s, then `stable=true` once jitter is below a threshold. |
| `ts` | RFC3339 string | When firmware sampled the reading. |

Published at ~1 Hz during a `stream_raw` window.

### 3.2 `tare_saved`

```json
{"event":"tare_saved","raw_offset":1234567,"ts":"2026-04-30T14:23:00Z"}
```

| Field | Type | Notes |
|---|---|---|
| `raw_offset` | integer | The raw HX711 value firmware captured as the new zero. iOS stores this in `HiveCalibration.tareOffset` after a wizard completes. |
| `ts` | RFC3339 string | When the tare was saved. iOS writes this to `Hive.lastTareAt`. |

This is the **response to a `tare` command** AND the durable confirmation that the firmware persisted the new offset.

### 3.3 `calibration_saved`

```json
{"event":"calibration_saved","scale_factor":4567.89,"predicted_accuracy_pct":1.8,"ts":"2026-04-30T14:23:00Z"}
```

| Field | Type | Notes |
|---|---|---|
| `scale_factor` | number | The new scale factor (raw counts per kg, or whatever convention firmware prefers). iOS stores in `HiveCalibration.scaleFactor`. iOS does NOT interpret it — it's an opaque firmware-side number. |
| `predicted_accuracy_pct` | number | Firmware's prediction of accuracy %. Decoded by iOS but **currently not used** in the UI (the wizard step 3 verify step computes its own actual error). Send a sensible value (e.g. derived from regression residual or just a stub like `1.8` from the mock). |
| `ts` | RFC3339 string | When the calibration was saved. iOS writes this to `Hive.lastCalibratedAt`. |

Response to a `calibrate` command.

### 3.4 `verify_result`

```json
{"event":"verify_result","measured_kg":4.97,"expected_kg":5.0,"error_pct":0.6,"ts":"2026-04-30T14:23:00Z"}
```

| Field | Type | Notes |
|---|---|---|
| `measured_kg` | number | What firmware measured with the current calibration applied. |
| `expected_kg` | number | Echo back what iOS sent in the `verify` command's `expected_kg` field. |
| `error_pct` | number | Firmware-computed `\|measured - expected\| / \|expected\| * 100`. iOS verifies via `CalibrationWizardViewModel.errorPct(...)` and uses firmware's value to bucket: `<3% pass`, `3–5% marginal`, `>5% fail`. |
| `ts` | RFC3339 string | When the verify was performed. |

Response to a `verify` command.

### 3.5 `modify_started`

```json
{"event":"modify_started","label":"added_super_deep","pre_event_kg":47.3,"ts":"2026-04-30T14:23:00Z"}
```

Response to `modify_start`. Forward-compat — iOS has no UI consumer yet.

### 3.6 `modify_complete`

```json
{"event":"modify_complete","label":"added_super_deep","pre_kg":47.3,"post_kg":58.1,"delta_kg":10.8,"duration_sec":287,"tare_updated":true,"ts":"2026-04-30T14:23:00Z"}
```

Response to `modify_end` (success path). All fields required.

### 3.7 `modify_warning`

```json
{"event":"modify_warning","label":"inspection_only","delta_kg":0.2,"warning":"no_significant_change_detected","ts":"2026-04-30T14:23:00Z"}
```

Response to `modify_end` when firmware detects something fishy. `warning` is a free-form short string. Test fixture uses `"no_significant_change_detected"`.

### 3.8 `modify_timeout`

```json
{"event":"modify_timeout","label":"added_super_deep","ts":"2026-04-30T14:23:00Z"}
```

Firmware-initiated — the modify window expired without a `modify_end`.

### 3.9 `error`

```json
{"event":"error","code":"hx711_unresponsive","details":"no DOUT pulse for 1s","ts":"2026-04-30T14:23:00Z"}
```

| Field | Type | Notes |
|---|---|---|
| `code` | string | Short machine-readable error code. Test fixture: `"hx711_unresponsive"`. Use snake_case. |
| `details` | string | Human-readable detail. iOS doesn't currently display this directly to users (the wizard shows generic per-step error messages and a 10-second timeout message); but please send a useful string for log inspection. |

Generic firmware error event. iOS decodes it but currently **does not display these to the user during the wizard** — the wizard's per-step error messages are inline ("MQTT not connected", "No response — try again", local timeout). However, future iOS work may surface these.

---

## 4. Events Payload (`scale/events`)

**This topic does not exist in the shipped contract.** See section 1. iOS has no subscription to `scale/events`, no decoder for an "events log" payload, and no schema. Do not implement it firmware-side until a future iOS feature requires it.

If you want a sticky-retained "last-known-tare" or "last-known-calibration" message for fresh installs to bootstrap from, you have two options that don't break the contract:

1. **Publish `tare_saved` / `calibration_saved` to `scale/status` with `retain=true`** — iOS will redundantly write to `Hive.lastTareAt`/`lastCalibratedAt` on subscribe but that's idempotent and harmless.
2. **Defer until a real spec exists.** Recommended.

---

## 5. Timing Expectations

| Phase | Timeout | Retry behavior |
|---|---|---|
| iOS publishes command → awaits matching event | **10 seconds** (hardcoded in `CalibrationWizardViewModel.sendTare/sendCalibrate/sendVerify` and `QuickRetareButton.sendTare`) | None automatic. UI shows "No response — try again." and the user re-taps. |
| Calibration wizard `stream_raw` window | **90 seconds** per request | iOS auto-renews every 60s while the wizard is open by re-sending `stream_raw`. Firmware should treat a new `stream_raw` as resetting the duration timer. |
| MQTT connection keepalive | 60 seconds (CocoaMQTT-side) | Auto-reconnect with 5s backoff when iOS-side disconnects. |

**Wake cycle question:** The contract assumes the firmware **stays connected throughout a single command-response round-trip** (≤10s end to end including network RTT and HX711 settling). If the firmware needs to deep-sleep between cycles, it MUST stay awake long enough after subscribing to `scale/cmd` to receive a command, process it, publish the response, and let the broker confirm delivery. Specifically:

- The 10s timeout starts when iOS calls `client.publish(...)` (not when iOS knows firmware received it). So the budget for firmware = 10s minus broker forward latency minus iOS app→broker latency. Realistically you have ~8s to wake, read HX711, settle, and respond.
- iOS does **not** retry. If firmware is asleep when a command arrives and the broker doesn't queue it (QoS 0 won't queue, QoS 1+ may depending on broker / clean-session settings), the user will see "No response — try again."
- Recommended: keep firmware awake while iOS app is open. The app sends `stream_raw` on wizard open; that's a strong signal the user is actively working with the scale. Or: have firmware listen on a long enough wake window, and accept that brief deep-sleeps will require the user to retry once.

---

## 6. MQTT QoS / Retain Policy

### What iOS uses

| Action | QoS | Retain |
|---|---|---|
| Publish `scale/cmd` | **1** (`CocoaMQTTQoS.qos1`) | **false** (default) |
| Subscribe `combsense/hive/+/#` | **default** (CocoaMQTT defaults to QoS 1 on subscribe; not explicitly set) | n/a |

iOS uses `client.publish(topic, withString: payload, qos: .qos1)` — see `MQTTService.publish(scaleCommand:forHiveId:)` line 270. No retain flag passed → CocoaMQTT defaults to `false`.

### What iOS expects from firmware

| Topic | Expected QoS | Expected Retain |
|---|---|---|
| `scale/status` | Anything ≥0; iOS subscribes with default QoS | **false** for stream events; **false recommended** for terminal events. iOS does not branch on retain. |
| Per-metric telemetry (`reading`, `weight`, `battery`, etc.) | Anything ≥0 | n/a |

### ⚠️ Your firmware uses PubSubClient (QoS 0 only) — what this means

- **Outbound `scale/status` from firmware at QoS 0:** Fine. iOS subscribes happily and will receive the message if it's connected at the moment of publish. If iOS is offline at that moment, the message is lost — but the calibration wizard requires the user to be looking at the screen, so this is acceptable.
- **Inbound `scale/cmd` to firmware at QoS 0:** Risky — iOS publishes with QoS 1, but if the broker downgrades to your subscription's QoS (0), and the firmware is briefly off-network or busy, the command can be silently dropped. This is the most likely failure mode you'll need to deal with. Mitigations:
  - Keep firmware reliably subscribed and online when the user is in the wizard.
  - Subscribe with `clean_session=false` so the broker queues commands while you're briefly offline. (Requires QoS 1 subscription though, which PubSubClient can't do — this is a real gap.)
  - Accept that the user will sometimes see "No response — try again" and that's the contracted UX.

---

## 7. Swift Type Definitions (verbatim)

### `ScaleCommand.swift`

```swift
import Foundation

/// Outbound commands published to `combsense/hive/<id>/scale/cmd`.
/// JSON shape: `{"cmd": "<name>", ...payload}`.
enum ScaleCommand: Codable, Equatable {
    case streamRaw(durationSec: Int)
    case stopStream
    case tare
    case calibrate(knownKg: Double)
    case verify(expectedKg: Double)
    case modifyStart(label: String)
    case modifyEnd(label: String)
    case modifyCancel

    private enum CodingKeys: String, CodingKey {
        case cmd
        case durationSec = "duration_sec"
        case knownKg     = "known_kg"
        case expectedKg  = "expected_kg"
        case label
    }

    func encode(to encoder: Encoder) throws {
        var c = encoder.container(keyedBy: CodingKeys.self)
        switch self {
        case .streamRaw(let d):
            try c.encode("stream_raw", forKey: .cmd)
            try c.encode(d, forKey: .durationSec)
        case .stopStream:
            try c.encode("stop_stream", forKey: .cmd)
        case .tare:
            try c.encode("tare", forKey: .cmd)
        case .calibrate(let k):
            try c.encode("calibrate", forKey: .cmd)
            try c.encode(k, forKey: .knownKg)
        case .verify(let e):
            try c.encode("verify", forKey: .cmd)
            try c.encode(e, forKey: .expectedKg)
        case .modifyStart(let l):
            try c.encode("modify_start", forKey: .cmd)
            try c.encode(l, forKey: .label)
        case .modifyEnd(let l):
            try c.encode("modify_end", forKey: .cmd)
            try c.encode(l, forKey: .label)
        case .modifyCancel:
            try c.encode("modify_cancel", forKey: .cmd)
        }
    }

    init(from decoder: Decoder) throws {
        let c = try decoder.container(keyedBy: CodingKeys.self)
        let cmd = try c.decode(String.self, forKey: .cmd)
        switch cmd {
        case "stream_raw":
            self = .streamRaw(durationSec: try c.decode(Int.self, forKey: .durationSec))
        case "stop_stream":
            self = .stopStream
        case "tare":
            self = .tare
        case "calibrate":
            self = .calibrate(knownKg: try c.decode(Double.self, forKey: .knownKg))
        case "verify":
            self = .verify(expectedKg: try c.decode(Double.self, forKey: .expectedKg))
        case "modify_start":
            self = .modifyStart(label: try c.decode(String.self, forKey: .label))
        case "modify_end":
            self = .modifyEnd(label: try c.decode(String.self, forKey: .label))
        case "modify_cancel":
            self = .modifyCancel
        default:
            throw DecodingError.dataCorruptedError(
                forKey: .cmd, in: c,
                debugDescription: "Unknown ScaleCommand cmd: \(cmd)"
            )
        }
    }
}
```

### `ScaleStatusEvent.swift`

```swift
import Foundation

/// Inbound events received on `combsense/hive/<id>/scale/status`.
/// JSON shape: `{"event": "<name>", ..., "ts": "<RFC3339>"}`.
enum ScaleStatusEvent: Codable, Equatable {
    case rawStream(rawValue: Int, kg: Double, stable: Bool, ts: Date)
    case tareSaved(rawOffset: Int, ts: Date)
    case calibrationSaved(scaleFactor: Double, predictedAccuracyPct: Double, ts: Date)
    case verifyResult(measuredKg: Double, expectedKg: Double, errorPct: Double, ts: Date)
    case modifyStarted(label: String, preEventKg: Double, ts: Date)
    case modifyComplete(label: String, preKg: Double, postKg: Double, deltaKg: Double, durationSec: Int, tareUpdated: Bool, ts: Date)
    case modifyWarning(label: String, deltaKg: Double, warning: String, ts: Date)
    case modifyTimeout(label: String, ts: Date)
    case error(code: String, details: String, ts: Date)

    var timestamp: Date {
        switch self {
        case .rawStream(_, _, _, let t),
             .tareSaved(_, let t),
             .calibrationSaved(_, _, let t),
             .verifyResult(_, _, _, let t),
             .modifyStarted(_, _, let t),
             .modifyComplete(_, _, _, _, _, _, let t),
             .modifyWarning(_, _, _, let t),
             .modifyTimeout(_, let t),
             .error(_, _, let t):
            return t
        }
    }

    private enum CodingKeys: String, CodingKey {
        case event
        case rawValue            = "raw_value"
        case kg
        case stable
        case ts
        case rawOffset           = "raw_offset"
        case scaleFactor         = "scale_factor"
        case predictedAccuracyPct = "predicted_accuracy_pct"
        case measuredKg          = "measured_kg"
        case expectedKg          = "expected_kg"
        case errorPct            = "error_pct"
        case label
        case preEventKg          = "pre_event_kg"
        case preKg               = "pre_kg"
        case postKg              = "post_kg"
        case deltaKg             = "delta_kg"
        case durationSec         = "duration_sec"
        case tareUpdated         = "tare_updated"
        case warning
        case code
        case details
    }

    // (Decoder + Encoder implementations: see CombSense/Services/MQTT/ScaleStatusEvent.swift —
    //  ~80 lines, accepts both ISO8601 with-fractional and without-fractional timestamps.)
}
```

### `MQTTPublishError.swift`

```swift
import Foundation

/// Errors raised by the MQTT publish path.
enum MQTTPublishError: Error, Equatable {
    case notConnected
    case encodingFailed
}
```

### `ScaleEventCorrelator.swift` (the iOS-side waiter — for understanding the timing model only; firmware doesn't implement this)

```swift
import Foundation

enum ScaleCorrelatorError: Error, Equatable {
    case timeout
}

/// Coordinates request/response correlation for the `scale/cmd` ↔ `scale/status`
/// MQTT contract. Call `awaitEvent(...)` after publishing a command; the
/// continuation resumes when the next event matching the predicate is delivered
/// for the given hive, or throws `.timeout` if it doesn't arrive in time.
@MainActor
final class ScaleEventCorrelator {

    private struct Waiter {
        let id: UUID
        let predicate: (ScaleStatusEvent) -> Bool
        let continuation: CheckedContinuation<ScaleStatusEvent, Error>
    }

    private var waiters: [String: [Waiter]] = [:]

    func awaitEvent(
        forHiveId hiveId: String,
        matching predicate: @escaping (ScaleStatusEvent) -> Bool,
        timeout: Duration
    ) async throws -> ScaleStatusEvent { /* ...see source... */ }

    func deliver(_ event: ScaleStatusEvent, forHiveId hiveId: String) {
        guard var hiveWaiters = waiters[hiveId] else { return }
        var remaining: [Waiter] = []
        for waiter in hiveWaiters {
            if waiter.predicate(event) {
                waiter.continuation.resume(returning: event)
            } else {
                remaining.append(waiter)
            }
        }
        waiters[hiveId] = remaining.isEmpty ? nil : remaining
    }
    // ... + removeWaiter() for cancellation/timeout safety
}
```

**Key point for firmware authors:** correlation is **first-matching-event-wins per hive**. iOS does NOT correlate by request_id (there isn't one). If two `tare` commands are in flight for the same hive, the first `tare_saved` resolves the first command's await, the second `tare_saved` resolves the second's. **In practice iOS only ever has one command in flight per hive at a time** (UI buttons disable while sending), so this isn't a real concern — but firmware shouldn't expect a request_id field.

---

## 8. Real Example Payloads (copy-paste ready)

These are the exact strings iOS produces / accepts, taken from `ScaleCommandTests.swift` and `ScaleStatusEventTests.swift`:

### Commands (iOS publishes)

```json
{"cmd":"tare"}
```
```json
{"cmd":"calibrate","known_kg":10}
```
```json
{"cmd":"verify","expected_kg":5}
```
```json
{"cmd":"stream_raw","duration_sec":60}
```
```json
{"cmd":"stop_stream"}
```
```json
{"cmd":"modify_start","label":"added_super_deep"}
```
```json
{"cmd":"modify_end","label":"extracted_honey"}
```
```json
{"cmd":"modify_cancel"}
```

### Status events (firmware publishes)

```json
{"event":"raw_stream","raw_value":5678901,"kg":47.3,"stable":true,"ts":"2026-04-30T14:23:00Z"}
```
```json
{"event":"tare_saved","raw_offset":1234567,"ts":"2026-04-30T14:23:00Z"}
```
```json
{"event":"calibration_saved","scale_factor":4567.89,"predicted_accuracy_pct":1.8,"ts":"2026-04-30T14:23:00Z"}
```
```json
{"event":"verify_result","measured_kg":4.97,"expected_kg":5.0,"error_pct":0.6,"ts":"2026-04-30T14:23:00Z"}
```
```json
{"event":"modify_started","label":"added_super_deep","pre_event_kg":47.3,"ts":"2026-04-30T14:23:00Z"}
```
```json
{"event":"modify_complete","label":"added_super_deep","pre_kg":47.3,"post_kg":58.1,"delta_kg":10.8,"duration_sec":287,"tare_updated":true,"ts":"2026-04-30T14:23:00Z"}
```
```json
{"event":"modify_warning","label":"inspection_only","delta_kg":0.2,"warning":"no_significant_change_detected","ts":"2026-04-30T14:23:00Z"}
```
```json
{"event":"modify_timeout","label":"added_super_deep","ts":"2026-04-30T14:23:00Z"}
```
```json
{"event":"error","code":"hx711_unresponsive","details":"no DOUT pulse for 1s","ts":"2026-04-30T14:23:00Z"}
```

### Sample MQTT round-trip on the wire

```
# iOS opens calibration wizard:
combsense/hive/c5129c68/scale/cmd  →  {"cmd":"stream_raw","duration_sec":90}

# Firmware streams (1Hz for 90s):
combsense/hive/c5129c68/scale/status  →  {"event":"raw_stream","raw_value":5678850,"kg":47.32,"stable":false,"ts":"2026-05-02T18:00:00Z"}
combsense/hive/c5129c68/scale/status  →  {"event":"raw_stream","raw_value":5678905,"kg":47.32,"stable":false,"ts":"2026-05-02T18:00:01Z"}
... (settles)
combsense/hive/c5129c68/scale/status  →  {"event":"raw_stream","raw_value":5678900,"kg":47.32,"stable":true,"ts":"2026-05-02T18:00:06Z"}

# User taps Tare:
combsense/hive/c5129c68/scale/cmd     →  {"cmd":"tare"}
combsense/hive/c5129c68/scale/status  ←  {"event":"tare_saved","raw_offset":5678900,"ts":"2026-05-02T18:00:08Z"}

# User puts 10kg ref weight on, taps Apply:
combsense/hive/c5129c68/scale/cmd     →  {"cmd":"calibrate","known_kg":10}
combsense/hive/c5129c68/scale/status  ←  {"event":"calibration_saved","scale_factor":120000.0,"predicted_accuracy_pct":1.5,"ts":"2026-05-02T18:00:30Z"}

# User puts 5kg ref weight on, taps Verify:
combsense/hive/c5129c68/scale/cmd     →  {"cmd":"verify","expected_kg":5}
combsense/hive/c5129c68/scale/status  ←  {"event":"verify_result","measured_kg":5.03,"expected_kg":5.0,"error_pct":0.6,"ts":"2026-05-02T18:00:50Z"}

# User taps Done — wizard dismisses:
combsense/hive/c5129c68/scale/cmd  →  {"cmd":"stop_stream"}
```

You can replay this round-trip locally with `/tmp/scale-mock-responder.py` (Python script in the `hivesense` repo) — point it at your broker, send commands with `mosquitto_pub`, and watch what iOS would expect to see.

---

## 9. Drift From Original Plan

Things that changed between `docs/superpowers/plans/2026-04-30-scale-calibration-and-tare.md` and the shipped code. **Trust this doc, not the plan.**

| Plan said | Reality (shipped) |
|---|---|
| `ScaleEventCorrelator` is an `actor` | It's a `@MainActor final class` (lives next to `MQTTService` which is also MainActor; pragmatic choice for SwiftUI-adjacent state) |
| Phase 5 introduces a separate `scale/events` topic for permanent log + Quick Re-tare confirmation | **Not shipped.** No `scale/events` topic exists. `Hive.lastTareAt` mirrors the `tare_saved` ts directly via `MQTTService.handleScaleStatus`. Quick Re-tare uses the same correlator path as the wizard. |
| Modify Hive flow (Phase 4) ships UI for `modify_*` commands | **Types shipped, UI did not.** `ScaleCommand.modifyStart/End/Cancel` are encodable/decodable and round-trip-tested, but no view sends them today. Firmware should still implement; just don't expect traffic from iOS yet. |
| `predicted_accuracy_pct` drives a UI display | Decoded but not shown. Wizard uses its own `verify_result.error_pct` → bucket logic for the user-facing accuracy verdict. |
| `error` events surface to user during wizard | Decoded but not shown to user. Wizard shows generic per-step error text + 10s timeout. (Future iOS work may surface the firmware `error.code`.) |
| Streaming uses 60s window with 30s renew | Uses **90s window** with **60s renew** (`CalibrationWizardView.startStream`, line 73). |

Things the plan DIDN'T mention but that are real:

- `latestRawReading: LatestRawReading?` is a `@Published` snapshot on `MQTTService` — added so the wizard could observe live readings via Combine async sequence (`mqtt.$latestRawReading.values`). It's the only piece of `raw_stream` state iOS keeps. It's reset to `nil` when the wizard dismisses to prevent stale data on reopen.
- iOS persists `Hive.lastCalibratedAt` from `calibration_saved.ts` (added in Phase 3), and writes a `HiveCalibration` SwiftData row on wizard completion (cascade-deletes when the Hive is deleted).

---

**Contract version:** 1.0 (frozen as of iOS PR #86 / branch `feat-scale-calibration-wizard`)
**Last verified against shipped code:** 2026-05-02

---

## 10. scale/config — keep-alive signaling for deep-sleep firmware

> **Status:** New addendum (2026-05-02). This section documents a topic that
> was NOT in the shipped contract v1.0. The firmware team is adding it to
> the C6 sensor-tag-wifi build because the firmware deep-sleeps for 300 s
> between wake cycles, and the existing 10-second command timeout means
> commands silently drop if the firmware happens to be asleep when iOS
> publishes them.
>
> **iOS work needed:** ~50 lines plus a spinner overlay in
> CalibrationWizardView and QuickRetareButton. See section 10.4.

### 10.1 Topic

| Topic | Direction | Retain | QoS |
|---|---|---|---|
| `combsense/hive/{deviceId}/scale/config` | iOS → firmware | **`true`** | 1 |

### 10.2 Payload

```json
{"keep_alive_until":"2026-05-02T20:30:00Z"}
```

| Field | Type | Required | Notes |
|---|---|---|---|
| `keep_alive_until` | RFC3339 string (UTC `Z` suffix) | yes | Firmware stays in extended-awake mode until this timestamp. iOS sets it to `now + 30 minutes`. |

**Empty payload** (zero-length string) on the same topic with `retain=true`
**clears** the retained message — firmware resumes normal deep-sleep cycle
on its next wake.

### 10.3 Firmware-side semantics

On each wake cycle, after MQTT connect, firmware subscribes to
`combsense/hive/<id>/scale/config`. The broker delivers the retained
message immediately if one is set.

Decision logic:

| Retained config state | Firmware behavior |
|---|---|
| No retained message | Normal cycle: publish reading, sleep 300 s |
| `keep_alive_until` is in the **future** (clock-synced via NTP) | **Extended-awake mode**: stay subscribed to `scale/cmd`, process commands, publish status events. Re-evaluate every 60 s — if `keep_alive_until` has passed, exit extended mode. |
| `keep_alive_until` is in the **past** | Treat as cleared. Normal cycle. |
| NTP not yet synced when config received | Fallback: stay in extended-awake mode for **fixed 30 min**, ignoring timestamp. Re-evaluate after NTP completes. |

Within extended-awake mode, firmware publishes a **heartbeat event** on
`scale/status` immediately upon entering the mode and every 60 s
thereafter:

```json
{"event":"awake","keep_alive_until":"2026-05-02T20:30:00Z","ts":"2026-05-02T20:00:05Z"}
```

| Field | Type | Notes |
|---|---|---|
| `event` | string | `"awake"` |
| `keep_alive_until` | RFC3339 string | The timestamp firmware will exit extended mode (echoed from received config; or computed if fallback path was used). |
| `ts` | RFC3339 string | When firmware sent the heartbeat. |

iOS uses the first `awake` event received after publishing
`scale/config` as the signal that firmware is ready to accept commands.

### 10.4 iOS-side responsibilities

#### On wizard open (CalibrationWizardView, QuickRetareButton)

1. Publish to `combsense/hive/<id>/scale/config`:
   - Payload: `{"keep_alive_until":"<now + 30 min, RFC3339 UTC>"}`
   - Retain: `true`
   - QoS: 1
2. Show a blocking spinner / progress overlay in the wizard or on the
   button: **"Waking sensor… up to 5 min"** (or whatever the sample
   interval is).
3. Subscribe-and-await the next `scale/status` event with `event=awake` for
   this hive. (Existing `ScaleEventCorrelator` can handle this — add an
   `awake` case to `ScaleStatusEvent`.)
4. When the `awake` event arrives, dismiss the spinner and proceed with
   the wizard's normal flow (`stream_raw`, `tare`, etc.).
5. If 6 minutes elapse without an `awake` event, show
   **"Sensor offline — check power and try again"** and dismiss the
   wizard.

#### While wizard is open

6. **Periodically refresh the retain.** Every 60 s, publish a fresh
   `scale/config` with `keep_alive_until` advanced to `now + 30 min`. This
   protects against multi-client scenarios and handles wizard sessions
   longer than 30 minutes.

#### On wizard close (success, cancel, or app background)

7. Publish to `combsense/hive/<id>/scale/config`:
   - Payload: `""` (empty string)
   - Retain: `true`
   - QoS: 1
8. This clears the retained message. Firmware on its next wake sees no
   config and returns to normal deep-sleep cycle.

#### On app crash / force-quit

If iOS doesn't get to step 7 (uncaught crash, app killed by user,
background task expires), the retained config persists on the broker
with the original `keep_alive_until`. Firmware will exit extended mode
naturally when the timestamp passes (≤ 30 minutes after the last refresh
in step 6). Battery cost: bounded.

### 10.5 ScaleStatusEvent additions

Add one variant to `ScaleStatusEvent`:

```swift
case awake(keepAliveUntil: Date, ts: Date)
```

Decoder addition (in `init(from:)` switch on `event`):

```swift
case "awake":
    let kauString = try c.decode(String.self, forKey: .keepAliveUntil)
    guard let kau = Self.parseTimestamp(kauString) else {
        throw DecodingError.dataCorruptedError(
            forKey: .keepAliveUntil, in: c,
            debugDescription: "Invalid keep_alive_until: \(kauString)"
        )
    }
    self = .awake(keepAliveUntil: kau, ts: ts)
```

Add `keepAliveUntil = "keep_alive_until"` to `CodingKeys`.

> Note: the firmware-claude's draft used `try c.decode(Date.self, ...)` — but
> the iOS `ScaleStatusEvent` decoder parses timestamps as Strings via the
> static `parseTimestamp` helper (accepts both fractional and non-fractional
> ISO8601). Match the existing pattern shown above, not the draft.

### 10.6 Sample MQTT round-trip with extended-awake mode

```
# User opens calibration wizard:
combsense/hive/c513131c/scale/config  ←  {"keep_alive_until":"2026-05-02T20:30:00Z"}  (retain=true, QoS 1)

# iOS shows "Waking sensor… up to 5 min" spinner.

# Up to 5 min later, firmware wakes from deep sleep, subscribes,
# sees retained config, enters extended-awake mode, publishes:
combsense/hive/c513131c/scale/status  →  {"event":"awake","keep_alive_until":"2026-05-02T20:30:00Z","ts":"2026-05-02T20:01:14Z"}

# iOS dismisses spinner, sends stream_raw:
combsense/hive/c513131c/scale/cmd     →  {"cmd":"stream_raw","duration_sec":90}
combsense/hive/c513131c/scale/status  ←  {"event":"raw_stream","raw_value":5678901,"kg":47.3,"stable":false,"ts":"2026-05-02T20:01:15Z"}
... (1Hz)
combsense/hive/c513131c/scale/status  ←  {"event":"raw_stream","raw_value":5678905,"kg":47.32,"stable":true,"ts":"2026-05-02T20:01:20Z"}

# User taps Tare — succeeds first try (firmware is awake):
combsense/hive/c513131c/scale/cmd     →  {"cmd":"tare"}
combsense/hive/c513131c/scale/status  ←  {"event":"tare_saved","raw_offset":5678905,"ts":"2026-05-02T20:01:25Z"}

# 60 s into the wizard session, iOS refreshes retain:
combsense/hive/c513131c/scale/config  ←  {"keep_alive_until":"2026-05-02T20:31:14Z"}  (retain=true)

# 60 s into firmware's awake mode, firmware sends heartbeat:
combsense/hive/c513131c/scale/status  →  {"event":"awake","keep_alive_until":"2026-05-02T20:31:14Z","ts":"2026-05-02T20:02:14Z"}

# User finishes wizard, dismisses:
combsense/hive/c513131c/scale/cmd     →  {"cmd":"stop_stream"}
combsense/hive/c513131c/scale/config  ←  ""  (empty, retain=true → clears retained)

# Firmware sees empty/cleared config on next 60s re-eval, exits extended mode,
# publishes one final reading, returns to normal deep-sleep cycle.
```

### 10.7 Quick Re-tare button UX

The Quick Re-tare button on the Overview tab uses the same scheme:

1. User taps Re-tare.
2. Button shows "Waking sensor…" spinner.
3. iOS publishes retained `scale/config` (same as wizard).
4. iOS subscribes-and-awaits `awake` event.
5. On `awake`: iOS sends `tare`, awaits `tare_saved`.
6. On `tare_saved`: button shows "Re-tared ✓" briefly, then returns to idle.
7. On wizard-close logic: iOS publishes empty `scale/config` to clear retain.

If the user spams Re-tare repeatedly, the second/third taps will succeed
instantly (firmware is still in extended-awake mode from the first tap's
config publish, until the 30-min timer or an explicit clear).

### 10.8 Drift from v1.0 contract

This addendum DOES NOT change anything in sections 1–9. It only adds:

- One new **topic**: `scale/config`
- One new **status event variant**: `awake`
- One new **iOS-side responsibility**: publish/refresh/clear retain on wizard lifecycle

Backward compatibility: a firmware that does NOT implement scale/config
will simply ignore the retained message and behave per the original
contract — i.e., it'll be subject to the wake-cycle race documented in
section 5. This addendum is opt-in for the firmware.

---

**Addendum version:** 1.1 (drafted 2026-05-02)
**Targets firmware:** sensor-tag-wifi C6 + future Feather S3 builds
