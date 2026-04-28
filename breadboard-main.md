# CombSense Hive Node — Breadboard Prototype (Main Board)

**Status:** Active build path. Breadboard prototype to validate the full sensor stack + firmware before any PCB work.
**Date:** 2026-04-28
**Pivot from:** [carrier-pcb.md](carrier-pcb.md) — PCB design parked as production roadmap.
**Pair doc:** [breadboard-ir.md](breadboard-ir.md) — IR junction board.
**Goal:** Get all sensors talking to firmware on bare hardware. Validate cable lengths, signal integrity, mic noise pickup, scale calibration, IR detection. Finalize firmware behavior. *Then* (and only then) consider a PCB.

---

## 1. Architecture

Two breadboards joined by one cable home-run — same physical pattern as the planned PCB:

| Board | Role | Location |
|---|---|---|
| **Main breadboard** (this doc) | MCU dev board, battery, charger, mic, scale, temp probe, status LEDs | Inside the hive enclosure |
| **IR junction breadboard** ([breadboard-ir.md](breadboard-ir.md)) | 8× IR break-beam pairs, pull-ups, emitter MOSFET | Near the hive entrance |

Connection: 12-conductor Dupont jumper bundle, M-M, 30–60cm long. Conductor order matches the GPIO-pinmap order so firmware doesn't change.

---

## 2. Parts list

### Core electronics

| # | Part | Qty | Source | ~Cost |
|---|---|---|---|---|
| 1 | **ESP32-S3-DevKitC-1-N8** dev board (no PSRAM, all GPIOs broken out) | 1 | [Adafruit](https://www.adafruit.com/product/5312) / Mouser / Digikey | $15 |
| 2 | **TP4056 + DW01 protection** charger module (USB-C variant preferred) | 1 | [Amazon](https://www.amazon.com/s?k=TP4056+USB-C+protection) | $2 (5-pack ~$8) |
| 3 | **18650 cell** Panasonic NCR18650B 3500mAh (or equivalent) | 1 | Amazon, Aliexpress | $7 |
| 4 | **18650 PCB-mount holder** Keystone-style (with PCB pads or pre-soldered leads) | 1 | Amazon | $3 |
| 5 | **HX711 + 4× 50kg load cell kit** (already locked in carrier doc) | 1 | [Amazon B07B4DNJ2L](https://www.amazon.com/dp/B07B4DNJ2L) | $13 |
| 6 | **DS18B20 stainless waterproof temp probe**, 1m cable | 1 | [Amazon](https://www.amazon.com/s?k=DS18B20+waterproof+probe) | $4 |
| 7 | **SPH0645 I²S microphone breakout** | 1 | [Adafruit ADA3421](https://www.adafruit.com/product/3421) | $7 |
| 8 | **FellDen 5V 200mA solar panels, 5-pack** (optional, for solar charging) | 1 pack | [Amazon B0BML3PR4Z](https://www.amazon.com/dp/B0BML3PR4Z) | $13 |

### Breadboards + jumpers

| # | Part | Qty | Source | ~Cost |
|---|---|---|---|---|
| 9 | **Full-size breadboard** (830 tie-points, ~165×55mm) — for main | 2 | Amazon (set of 8 ~$15) | $4 each |
| 10 | **Dupont jumper wire kit** (M-M, M-F, F-F, mixed colors, 40-pin rainbow ribbon) | 1 set | Amazon | $8 |
| 11 | **Breadboard power supply module** (3.3V/5V dual rail, optional but useful) | 1 | Amazon | $3 |

### Discrete passives + actives

| # | Part | Qty | Notes |
|---|---|---|---|
| 12 | 4.7kΩ ¼W through-hole resistors | 1 (only DS18B20 pull-up; IR pull-ups live on the IR board) | Resistor kit covers it |
| 13 | 1MΩ ¼W through-hole resistors | 2 | Battery monitor divider |
| 14 | 470Ω ¼W through-hole resistors | 2 | Status LED current limit |
| 15 | 10nF ceramic capacitor | 1 | Battery monitor anti-alias filter |
| 16 | 5mm or 3mm bicolor red/green LED (common cathode or 2× separate LEDs) | 1 (or 2) | Status indicator |
| 17 | **Resistor & cap assortment kit** (covers items 12–15 plus future needs) | 1 | [Amazon](https://www.amazon.com/s?k=resistor+kit+1%2F4w) | $10 |

### Misc

| # | Part | Qty | Notes |
|---|---|---|---|
| 18 | USB-C cable | 1 | For dev-board flashing/power |
| 19 | Small project box ~120×80×40mm (clear lid useful) | 1 | Houses the main breadboard outdoors |
| 20 | Cable gland (PG7 or similar) for the ribbon exit | 1 | Cable strain-relief through the box wall |

**Approximate total:** ~$80–100 for the main board (without solar panel ~$70).

---

## 3. Wiring map

GPIO numbers below match the silkscreen labels on the ESP32-S3-DevKitC-1 (e.g., GPIO15 = pin labeled "IO15"). Same pinmap as [carrier-pcb.md §5](carrier-pcb.md), so firmware works as-is.

### Power rails

| Rail | Source |
|---|---|
| **3V3 rail** (red) | Dev board's `3V3` pin (regulator output, good for ~500mA) |
| **GND rail** (blue/black) | Dev board's `GND` pin (any of them) |
| **5V rail** (yellow) | Dev board's `5V` pin — used only if powering external 5V devices (none in v1) |
| **VBAT rail** (separate) | TP4056 module's `BAT+` output → dev board's `5V` pin (or directly through a USB-C boost converter — see Power chain section) |

### Sensor / I/O wiring

| Function | GPIO (DevKitC label) | Wires to | Notes |
|---|---|---|---|
| Battery monitor | GPIO1 | Junction node of 1MΩ + 1MΩ divider; 10nF cap from junction to GND | Top of divider → BAT+ rail. Bottom → GND. Tap → GPIO1. |
| I²S mic BCLK | GPIO4 | SPH0645 `BCLK` pin | Use twisted/shielded cable if mic is remote. |
| I²S mic LRCL/WS | GPIO5 | SPH0645 `LRCL` pin | |
| I²S mic DOUT | GPIO6 | SPH0645 `DOUT` pin | |
| I²C SDA (expansion) | GPIO8 | (unused unless you add an I²C sensor; reserve a row) | 4.7kΩ pull-up to 3V3 if used. |
| I²C SCL (expansion) | GPIO9 | (unused) | 4.7kΩ pull-up to 3V3 if used. |
| DS18B20 1-Wire DATA | GPIO15 | Probe yellow/white wire + 4.7kΩ pull-up to 3V3 | Probe red → 3V3, probe black → GND. |
| HX711 DT | GPIO16 | HX711 module `DT` pin | |
| HX711 SCK | GPIO17 | HX711 module `SCK` pin | |
| BQ24074 PG | GPIO18 | (skip — TP4056 doesn't have a PG line) | Free for future. |
| BQ24074 CHG | GPIO21 | (skip — TP4056's status LEDs are local; no logic-level output) | Free for future. |
| IR detector #1–#8 | GPIO33–40 | Ribbon wires 1–8 → IR breadboard | See [breadboard-ir.md](breadboard-ir.md). |
| IR emitter enable | GPIO41 | Ribbon wire 9 → IR breadboard MOSFET gate | |
| Status LED red | GPIO47 | LED red cathode → 470Ω → GPIO47 | LED anode → 3V3. **Active LOW.** |
| Status LED green | GPIO48 | LED green cathode → 470Ω → GPIO48 | Anode shared with red on bicolor LED. |

### Mic side-band

The SPH0645 also needs:
- `3V3` → 3V3 rail
- `GND` → GND rail
- `SEL` → GND (left channel) **or** 3V3 (right channel) — pick one and keep consistent with firmware

---

## 4. Power chain

### USB-only path (simplest, recommended for first power-on)

1. USB-C cable from laptop/wall adapter → dev board USB-C jack
2. Dev board's onboard regulator provides 3.3V to its `3V3` pin
3. All sensors run from this rail
4. No battery, no charger needed for initial bring-up

### Battery + USB charging path (next phase)

1. **TP4056 module wiring:**
   - TP4056 `IN+` → USB-C source (the module's USB-C jack handles this directly)
   - TP4056 `IN-` → USB-C source GND
   - TP4056 `BAT+` → 18650 holder positive lead **and** dev board `5V` pin (yes, the dev board's `5V` pin accepts down to ~3.0V on most variants — check your specific board's datasheet)
   - TP4056 `BAT-` → 18650 holder negative lead **and** GND
   - TP4056 `OUT+` / `OUT-` → unused on this build (we tap straight off `BAT+` with the protection circuit in line)

2. **Battery cell:** insert 18650 into holder. Verify 3.7–4.2V across the holder leads with a multimeter before connecting.

3. **Behavior:**
   - USB plugged in: TP4056 charges the cell *and* powers the dev board (via the cell, since TP4056 has no power-path management — minor flicker risk during heavy WiFi TX bursts, acceptable for prototype)
   - USB unplugged: cell powers everything until it hits the DW01 cutoff (~2.4V)

### Solar charging (optional v2)

1. FellDen 5V panel(s) — wire 2–3 in parallel for ~2–3W harvest
2. Panel + → series 1A Schottky diode (1N5817) → TP4056 `IN+` (the diode prevents reverse flow at night when panel is dark)
3. Panel − → TP4056 `IN-`
4. Solar and USB share the TP4056 input — whichever is higher voltage drives the charger. USB-C (5V) wins over solar (5V) tied; solar takes over when USB is disconnected.

**Production note:** the planned PCB uses BQ24074 with proper power-path and dedicated solar/USB inputs. TP4056 is a simpler-but-rougher prototype substitute.

---

## 5. Build order — incremental

Don't wire everything at once. Each step has a "smoke test" before adding the next.

### Phase 1 — Just the brain
1. Plug dev board into a long breadboard, both rails of pins straddling the center channel (use 2 breadboards bridged if needed).
2. USB-C cable → dev board → `idf.py monitor` or `arduino-cli` serial — confirm the existing firmware boots and the device-id prints.
3. Smoke test: existing firmware running, MQTT publishing dummy data over WiFi.

### Phase 2 — Battery monitor
4. Wire 1MΩ + 1MΩ divider from VBAT rail to GND, tap to GPIO1, 10nF from tap to GND.
5. Power dev board from USB only (no battery yet) — GPIO1 reads near 0V (no VBAT yet). Expected.
6. Disconnect USB, attach battery via TP4056 (battery only, no USB) — GPIO1 should now read half of cell voltage (~1.85–2.10V).
7. Confirm `vbat_mV` field in MQTT publish reflects the cell voltage.

### Phase 3 — Temperature probe
8. Wire DS18B20: red → 3V3, black → GND, yellow → GPIO15 + 4.7kΩ to 3V3.
9. Confirm `t1` field in MQTT publish reflects ambient temperature.

### Phase 4 — Microphone
10. Wire SPH0645: 3V3, GND, BCLK→GPIO4, LRCL→GPIO5, DOUT→GPIO6, SEL→GND.
11. Confirm I²S sample stream is reading non-zero data (firmware feature TBD if not already supported — may need to add an audio-sample firmware path).

### Phase 5 — Scale
12. Solder 4-pin male header to HX711 module's MCU side.
13. Plug HX711 module into breadboard.
14. Wire HX711: 3V3, GND, DT→GPIO16, SCK→GPIO17.
15. Wire 4× load cells into the HX711's load-cell pads (Wheatstone bridge per HX711 instructions).
16. Confirm scale reads stable values; calibrate with a known weight.

### Phase 6 — IR detection
17. Build the IR breadboard per [breadboard-ir.md](breadboard-ir.md).
18. Connect ribbon home-run between the two breadboards.
19. Confirm 8 IR detectors register beam-break events on GPIO33–40.

### Phase 7 — Status LEDs
20. Wire bicolor LED: anode → 3V3, red cathode → 470Ω → GPIO47, green cathode → 470Ω → GPIO48.
21. Confirm firmware-driven LED state changes.

### Phase 8 — Outdoor pilot
22. Place main breadboard in project box with cable gland for the ribbon.
23. Mount IR breadboard at the hive entrance.
24. Run for a few days, monitor stability, calibrate thresholds.

---

## 6. Firmware notes

**No firmware changes needed for breadboarding.** The pinmap is identical to the locked PCB pinmap, so the same firmware runs on either platform.

What might need tuning:
- **Audio sampling path** — the existing firmware doesn't have a documented I²S mic capture flow. May need a new module to feed the cloud streaming pipeline.
- **IR debouncing** — bee-traffic pattern is HIGH→LOW→HIGH on each crossing. Firmware needs interrupt handlers on GPIO33–40 with debounce.
- **HX711 driver** — well-trodden Arduino library territory; pick `bogde/HX711` or similar.
- **Battery SOC curve** — issue [#10](https://github.com/sjordan0228/combsense-monitor/issues/10) covers the linear-interp replacement. Breadboard prototype is a good place to characterize the real discharge curve before committing to the lookup table.

---

## 7. Migration path back to PCB

This breadboard prototype is the validation step. When the firmware + sensor stack is proven:

1. The carrier-pcb.md and ir-daughtercard.md files describe the production design.
2. Hand them off to a contract PCB designer (or learn KiCad) — the BOM, pinmap, and power chain are already locked.
3. The breadboard's TP4056 → BQ24074 swap, breakout-modules → bare-IC swap, and "wires shoved into rails" → screw terminals are the only real differences.
4. Firmware is unchanged from breadboard to PCB.

This separation lets you ship a working prototype **today** and a polished kit **later** without redoing any of the firmware work.

---

## 8. Open items (breadboard-specific)

1. **Audio firmware path** — does the existing firmware have any I²S capture code, or do we need to add it from scratch? Investigate before Phase 4.
2. **Project box for outdoor main board** — pick a specific SKU once breadboard layout is set (need the dimensions first).
3. **Cable gland choice** — depends on ribbon thickness. PG7 or PG9 typical.
4. **Breadboard-friendly IR emitter MOSFET** — see [breadboard-ir.md](breadboard-ir.md).

---

## 9. References

- [carrier-pcb.md](carrier-pcb.md) — production PCB design, parked as future roadmap
- [breadboard-ir.md](breadboard-ir.md) — IR junction breadboard, paired with this main board
- [ir-daughtercard.md](ir-daughtercard.md) — production IR daughtercard design (the PCB equivalent of breadboard-ir.md)
- **ESP32-S3-DevKitC-1-N8 datasheet** (Espressif)
- **TP4056 + DW01 reference** (typical Amazon module schematic — search "TP4056 DW01 schematic")
- **Adafruit ADA3421 SPH0645 guide** — I²S wiring + library
- **Adafruit ADA2167** — IR break-beam pair datasheet (verify before IR breadboard build)
