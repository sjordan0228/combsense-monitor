# IR Junction — Breadboard Prototype

**Status:** Active build path. Pairs with [breadboard-main.md](breadboard-main.md).
**Date:** 2026-04-28
**Pivot from:** [ir-daughtercard.md](ir-daughtercard.md) — PCB version parked as production roadmap.
**Goal:** A small breadboard that mounts near the hive entrance with all 8 IR break-beam pairs, pull-up resistors, and emitter power gating. Single ribbon cable connects back to the main breadboard.

---

## 1. Role

Same role as the PCB daughtercard:

- 8× IR break-beam pairs land here, near the hive entrance
- Pull-ups, emitter power gate, and ESD live near the detectors (lower noise on long cable runs)
- One 12-conductor cable home-run to the main breadboard
- Powered + controlled entirely by signals from the main breadboard

This isolates the entrance hardware from the brain — same architectural pattern as the PCB plan.

---

## 2. Confirmed inputs (from [carrier-pcb.md](carrier-pcb.md))

| Item | Value |
|---|---|
| IR pair part | Adafruit ADA2167, 5mm IR break-beam pair |
| Wires per pair | **5 wires** — emitter R/B (Vcc/GND), detector R/B/W (Vcc/GND/signal) |
| Detector output | Open-collector phototransistor → needs pull-up to 3V3 |
| Detector behavior | HIGH = beam unbroken, LOW = beam blocked |
| Number of pairs | 8 |
| Cable home-run | 12-conductor Dupont bundle (rainbow ribbon) |
| Logic level | 3V3 (matches main breadboard) |

---

## 3. Cable home-run pinout

12 conductors. **Order must match GPIO order on the main breadboard side** so firmware doesn't need a remap.

| Wire # | Signal | Main board pin |
|---|---|---|
| 1 | Detector #1 signal | GPIO33 |
| 2 | Detector #2 signal | GPIO34 |
| 3 | Detector #3 signal | GPIO35 |
| 4 | Detector #4 signal | GPIO36 |
| 5 | Detector #5 signal | GPIO37 |
| 6 | Detector #6 signal | GPIO38 |
| 7 | Detector #7 signal | GPIO39 |
| 8 | Detector #8 signal | GPIO40 |
| 9 | Emitter enable (MOSFET gate drive) | GPIO41 |
| 10 | 3V3 (always-on) | 3V3 rail |
| 11 | GND | GND rail |
| 12 | Spare | (unused) |

**Practical:** label both ends of the bundle with masking tape and a Sharpie. Mismatch on this one cable is the most likely build error.

---

## 4. Parts list

### Core

| # | Part | Qty | Source | ~Cost |
|---|---|---|---|---|
| 1 | **Half-size breadboard** (400 tie-points, ~85×55mm) | 1 | Amazon | $4 |
| 2 | **8× IR break-beam pairs** (Adafruit ADA2167, 5mm) | 8 | [Adafruit](https://www.adafruit.com/product/2167) | ~$2.50 each = $20 |
| 3 | **2N7000 N-channel MOSFET** TO-92 (logic-level capable, 200mA) | 1 + spare | Amazon, Mouser | $0.30 |
| 4 | **4.7kΩ ¼W through-hole resistors** (detector pull-ups) | 8 | Resistor kit | (covered by main board kit) |
| 5 | **220Ω ¼W through-hole resistors** (emitter LED current limit, ~10mA each) | 8 | Resistor kit | (covered) |
| 6 | **100kΩ ¼W through-hole resistor** (MOSFET gate pulldown — safe-off when GPIO floats at boot) | 1 | Resistor kit | (covered) |
| 7 | **Dupont jumper wire bundle** — 40-pin rainbow ribbon, 30–60cm, M-M | 1 | Amazon | (covered by main board kit) |

### Mounting + outdoor

| # | Part | Qty | Source | ~Cost |
|---|---|---|---|---|
| 8 | **Weather-resistant project box** ~80×60×30mm with clear lid | 1 | Amazon | $8 |
| 9 | **Cable gland** PG7 (one for the ribbon exit) | 1 | Amazon | $1 |
| 10 | **Cable glands** PG7 ×8 (one per IR-pair leads entering the box) — *or* drill holes + silicone-seal them | 8 | Amazon | $5 (or $0 with silicone) |
| 11 | **3M VHB tape** or zip ties for mounting the box near the hive | 1 roll | Amazon | $5 |

**Approximate total:** ~$45–55 for the IR junction.

---

## 5. Wiring map (the IR breadboard)

The breadboard has two power rails (3V3 and GND, both fed from the main board via the ribbon) and eight repeated detector circuits + one emitter circuit.

### Power-in from the ribbon

| Ribbon wire | Lands on |
|---|---|
| Wire 10 (3V3) | 3V3 rail of the IR breadboard |
| Wire 11 (GND) | GND rail of the IR breadboard |
| Wire 9 (emitter enable) | MOSFET gate (with 100kΩ pulldown to GND) |
| Wires 1–8 (detector signals) | Each connects to its own detector pull-up node (see below) |

### Per-pair circuit (×8, identical)

For pair N (where N = 1–8):

```
EMITTER half:
  Pair N emitter Red (V+)  → 220Ω resistor → 3V3 rail
  Pair N emitter Black (GND) → "switched ground" rail (MOSFET drain)

DETECTOR half:
  Pair N detector Red (V+)    → 3V3 rail
  Pair N detector Black (GND) → GND rail
  Pair N detector White (signal) → 4.7kΩ pull-up to 3V3
                                → also wired to ribbon wire N (back to main board GPIO 32+N)
```

The 4.7kΩ pulls the signal HIGH when the phototransistor is dark (beam unbroken). When IR light hits the phototransistor, it conducts and pulls the signal LOW.

### Single MOSFET emitter gate

| Pin | Connection |
|---|---|
| Drain | "Switched ground" rail (where all 8 emitter Black wires tie) |
| Source | GND rail |
| Gate | Ribbon wire 9 (IR emitter enable from GPIO41 on main) |
| Gate pulldown | 100kΩ from gate to GND — keeps MOSFET OFF when GPIO floats during boot |

When firmware drives GPIO41 HIGH, MOSFET conducts → emitter ground rail is connected to GND → emitter LEDs light up. When LOW, all emitters are off.

### Current math (sanity check)

- Each IR LED at 220Ω + 1.2V V_F: I = (3.3V − 1.2V) / 220Ω ≈ 9.5mA per LED
- 8 LEDs × 9.5mA = ~76mA total through the MOSFET
- 2N7000 rated 200mA continuous → comfortable margin
- LED brightness at 10mA is enough for ~95mm beam-break detection (typical hive entrance reducer width)

If beam range is too short during testing: drop the resistor to 150Ω (~14mA) or 100Ω (~21mA, near LED max) and use a slightly beefier MOSFET (BS170 or IRLB8721) to handle the higher current.

---

## 6. Build order

### Phase A — Power rails first
1. Plug a Dupont jumper from main board's `3V3` pin → IR breadboard's `+` rail (use ribbon wire 10 if you've already wired the ribbon, otherwise a temporary jumper for bench testing).
2. Same for GND → `−` rail.
3. Confirm 3.3V across the rails with a multimeter before adding any IR pairs.

### Phase B — One pair as a test
4. Wire pair #1 only (per the per-pair circuit above).
5. Skip the MOSFET for now — temporarily wire the emitter Black directly to GND so the LED is always on.
6. Use multimeter on pair #1's detector signal node:
   - Hold pair so emitter and detector face each other → signal should be ~0V (LOW)
   - Block the beam with your finger → signal should be ~3.3V (HIGH)
7. If that works, you've validated the detector pull-up topology.

### Phase C — Add the MOSFET
8. Wire the 2N7000: drain → switched-ground rail (move pair #1's emitter Black to this rail), source → GND, gate → 100kΩ to GND, gate also exposed to a temporary wire.
9. Tap gate temporarily to 3V3 (manually): pair #1 LED should light. Tap to GND: LED should go off.
10. Once verified, run gate to ribbon wire 9.

### Phase D — Add remaining 7 pairs
11. Wire pairs #2 through #8 identically. Leave the MOSFET in place — they all share the switched ground rail.
12. Each pair's detector signal goes to its own ribbon wire (1–8).

### Phase E — Connect to main board and test from firmware
13. Plug ribbon into main breadboard at the GPIO33–41 + 3V3 + GND points.
14. Firmware: configure GPIO33–40 as `INPUT_PULLUP` (the external 4.7kΩ + the internal pull-up are in parallel — fine, just slightly stronger pull-up).
15. Drive GPIO41 HIGH from firmware → all 8 LEDs light up → all 8 detector signals should read LOW (beam present).
16. Block one beam at a time → that channel should flip to HIGH. Confirm in firmware logs.
17. Drive GPIO41 LOW → LEDs off → all 8 signals float HIGH (no beam, but detector pulled up). This is the "sleep" state between IR scan windows.

### Phase F — Outdoor mount
18. Place IR breadboard in project box.
19. Drill 8 small holes in the box wall — pass each IR pair's 5 wires through (or use cable glands). Silicone-seal afterward.
20. Cable gland for the ribbon exit on the side facing the main board.
21. Mount box near hive entrance with VHB tape or zip ties. Aim each IR pair across the entrance opening (emitter on one side, detector on the other, ~95mm apart).

---

## 7. Mounting strategy

For a Langstroth hive's entrance reducer (~20mm × ~95mm opening):

- **Option A** — single-line array: place all 8 emitter LEDs across the top of the entrance, all 8 detectors across the bottom (or vice versa). Beam crossings = bee crossings. Simplest, but only counts in/out direction if you also process timing patterns.
- **Option B** — paired across the gap: 4 pairs near the top (in/out detection by sequence), 4 near the bottom. Higher resolution but more wiring.

For breadboard prototyping, **Option A** is enough to validate the firmware's detection and counting logic.

---

## 8. Open items

1. **Project box dimensions** — confirm once half-size breadboard arrives. ~80×60×30mm should fit but verify.
2. **Cable management** — 8 IR pair cables × 5 wires each = 40 wires entering the box. Silicone seal vs cable glands vs grommet strip vs pre-fab harness. Decide based on first build.
3. **MOSFET gate-rise timing** — if firmware enables IR scan and reads detector immediately, the MOSFET turn-on is fast (microseconds), but the IR LEDs themselves and the phototransistor stabilization take ~100µs. Add a small delay (1ms is plenty) between enabling emitters and sampling detectors.
4. **Sun interference** — direct sunlight can saturate IR phototransistors and produce false "beam blocked" readings. May need to add a software filter (sample twice — once with emitters on, once off — and only count crossings when the difference is consistent with a real interruption). Tackle in firmware if it shows up in field testing.

---

## 9. References

- [breadboard-main.md](breadboard-main.md) — main breadboard, paired with this one
- [ir-daughtercard.md](ir-daughtercard.md) — production PCB version of this junction board
- **Adafruit ADA2167** — IR break-beam pair (5mm); verify wire colors against the actual product before wiring
- **2N7000 datasheet** — V_GS(th), R_DS(on) at 3V3 drive
- **Beekeeping reference**: Langstroth entrance reducer dimensions (~20×95mm)
