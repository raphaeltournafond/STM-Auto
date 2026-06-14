# STM-Auto — Wiring decisions (KiCad schematic)

Locked component-level wiring choices for the SWEE.BRZ board. Pin assignments are in
`src/pins.h` (source of truth); this file records **how** each peripheral is wired and **why**.
KiCad 7 (`stm-auto.kicad_sch`). Convention: rails/buses on **global labels** (`5V`, `V3.3V`,
`GND`, `CANH/CANL`, `RX/TX`), local connections on direct wires.

## Status

| Peripheral | Pin(s) | State |
|---|---|---|
| STM32F103C8Tx, LM2596 reg, OLED (GME12864), servos, pressure/temp sensors, fuse, zeners | — | drawn |
| CAN (SN65HVD230) | PA11/PA12 | drawn |
| WS2812B RGB LED | PB15 | decided (below) |
| Buzzer (piezo) | PB6 | decided (below) |
| ACK touch button | PB5 | decided (below) |
| MP3 (DFPlayer Mini) | PA9/PA10 | decided (below) |

---

## WS2812B RGB LED — `LED:WS2812B`

Status indicator. Data from **PB15** (SPI2 MOSI + DMA in firmware). Symbol pins:
`1 VDD · 2 DOUT · 3 VSS · 4 DIN`.

**Problem:** at 5 V VDD the LED needs V_IH = 0.7×VDD = 3.5 V on DIN, but PB15 only drives 3.3 V →
out of spec / unreliable.

**Decision — series diode drop to ~4.3 V:** one series silicon diode from `5V` to VDD lowers V_IH
to ~3.0 V, so 3.3 V data clears it with margin. **Any ~0.7 V Si diode works** (1N4148, 1N400x, or a
spare zener forward-biased — Vz irrelevant in forward direction). Avoid Schottky (~0.3 V, too little
margin) and LEDs (too much drop).
*No-diode alternative:* power VDD from `V3.3V` (V_IH = 2.3 V, data fine) — but 3.3 V is just under the
3.5 V VDD spec min; OK for one status LED, slightly out of spec.

Connections:
- `5V` → diode anode; diode cathode → VDD (pin 1)
- 100 nF decoupling VDD→GND (`Device:C`)
- 330 Ω series on DIN: PB15 → R → DIN (pin 4) — anti-ringing
- VSS (pin 3) → GND
- DOUT (pin 2) → **No-Connect flag** (single LED, no daisy-chain)
- **PWR_FLAG** on the ~4.3 V net (VDD sits behind a diode → keeps ERC quiet)

Footprint: `LED_SMD:LED_WS2812B_PLCC4_5.0x5.0mm_P3.2mm`

## Buzzer — `Device:Buzzer` (passive piezo)

Alert tone from **PB6** via `tone()` (TIM3 in software — no clash with Servo/TIM2). Symbol pins:
`1 (+) · 2 (−)`.

**Decision — direct GPIO drive.** A passive piezo is ~a capacitor (µA-level), so PB6 drives it
directly: no transistor, no flyback.

Connections:
- PB6 → (optional 100 Ω series, limits capacitive inrush) → BZ1 pin 1 (+)
- BZ1 pin 2 (−) → GND

Volume note: modest at 3.3 V. Upgrade path if too quiet = transistor + buzzer on 5 V — does **not**
change the PB6 assignment, so the board stays terminated.

Footprint: `Buzzer_Beeper:*` (pick by physical pin pitch).

## ACK touch button — `Connector_Generic:Conn_01x03` (TTP223 module)

3-pin capacitive touch module (VCC / OUT / GND). No TTP223 in standard libs → generic 3-pin connector
(or download a module symbol). OUT → **PB5**.

**Decisions:**
- **VCC on `V3.3V`, not 5 V** — OUT swings to the module's VCC; at 3.3 V it's unambiguously safe for
  PB5 (no 5 V-tolerance question).
- **PB5 = plain `INPUT`** (no pull) — the module actively drives the line both ways; a pull would
  fight it.
- Default mode = momentary, active-high → **rising edge** on touch (don't bridge the module's
  mode-jumpers). Matches firmware ack logic (DECISION_MATRIX §6).
- No external parts required (module is self-decoupled). Optional: 100 nF on VCC, ~1 kΩ series on OUT.

Footprint: `Connector_PinHeader_2.54mm:PinHeader_1x03_P2.54mm_Vertical`

## MP3 — DFPlayer Mini / DFR0299 (clone: MP3-TF-16P)

Voice prompts over **USART1**: PA9 = MCU TX → module RX, PA10 = MCU RX ← module TX. 16-pin module;
generic `Connector_Generic:Conn_01x16` (or download symbol).

**Decisions — 5 V supply, built-in amp to a small speaker:**
- VCC (pin 1) → `5V`; add **100 nF + ~100 µF** near the module (audio noise / amp brownout).
- GND (pins 7, 10) → GND.
- Module RX (pin 2): **PA9 → 1 kΩ series → RX** — the 1 kΩ is the mandatory DFPlayer noise/garble fix.
- Module TX (pin 3) → PA10 direct (5 V TX into 5 V-tolerant PA10).
- Audio: SPK_1 (pin 6) and SPK_2 (pin 8) → small 4–8 Ω speaker (`Device:Speaker`).

**Gotchas:**
- **SPK is a bridge (BTL) output — speaker goes ONLY between SPK_1 and SPK_2. Never tie SPK to GND**
  (can damage the amp). #1 DFPlayer mistake.
- Label these nets **`MP3_RX` / `MP3_TX`** to avoid clashing with the existing `RX`/`TX` labels; mind
  the TX↔RX cross-over.

Footprint: 2.54 mm header matching the physical module.
