# CLAUDE.md

## Project

**STM-Auto** — firmware for the SWEE.BRZ oil monitor on an STM32F103C8T6 "Blue Pill"
(PlatformIO + Arduino-STM32). It reads oil **temperature** (`PA1`) and **pressure** (`PA0`),
opens/closes **twin flap servos** (in tandem) on a temperature threshold, and shows status on a
128x64 **SSD1306 OLED**.

> **Behavioural spec lives in `DECISION_MATRIX.md`** (French, WIP): life phases, the
> priority-ordered situation matrix, adaptive pressure floors, hysteresis, ack, voice messages.
> `main.cpp` implements only a subset so far; the rest is the roadmap. Read it before changing
> decision logic.

## Commands

```bash
pio run -e bluepill_f103c8            # build
pio run -e bluepill_f103c8 -t upload  # flash via ST-Link (upload_protocol = stlink)
pio device monitor -b 115200          # serial monitor
pio run -t clean                      # clean
```

No unit tests (`test/` is a PlatformIO placeholder). Libs (`platformio.ini`): Adafruit SSD1306 + GFX.

## Architecture

All logic is in `src/main.cpp`:

- `setup()` — serial @115200, status LED, OLED init (blocks in `blinkErrorLED()` on failure),
  servo attach + close, 12-bit ADC.
- `loop()` @200 ms — temp resistance → °C, pressure voltage → bar, `updateServo()` at
  `TEMPERATURE_THRESHOLD`, `updateDisplay()`.
- Shared helpers `readPinVoltage()` and `interpolate()` (piecewise-linear) serve both sensors.

**Conventions to preserve:**
- Interpolation x-tables (`resTable`, `vPressureTable`) **must stay strictly increasing**, each in
  sync with its y-table (`tempTable`, `barTable`) — `interpolate()` assumes it.
- Keep the sensor fail-safes: resistance ≥ `TEMP_MAX_SAFE_RESISTANCE` → `TEMPERATURE ERROR`;
  pressure volt < `PRESS_MIN_SAFE_VOLTAGE` → `PRESSURE ERROR`.

## Pins (`src/pins.h` is the source of truth)

| Function | Pin(s) | Function | Pin(s) |
|---|---|---|---|
| Pressure sensor | `PA0` | Buzzer (tone→TIM3) | `PB6` |
| Temp sensor | `PA1` | OLED SCL/SDA (I2C2) | `PB10`/`PB11` |
| Flap servos ×2 (Servo→TIM2) | `PA6`/`PA7` | WS2812B RGB LED (SPI2 MOSI+DMA) | `PB15` |
| MP3 (USART1) | `PA9`/`PA10` | Backup LED (onboard) | `PC13` |
| CAN RX/TX | `PA11`/`PA12` | ACK touch button | `PB5` |

Notes: `Servo` lib owns **TIM2**, `tone()` owns **TIM3** (shared timers — don't reuse them for PWM).
CAN uses the USB pins → remove the PA12 USB pull-up. WS2812B needs a level shifter (or ~4.3V supply).

`main.cpp` includes `pins.h` and uses these macros directly — change a pin in `pins.h` only.
Wired but not yet coded: second flap servo (`PIN_SERVO_FLAP_2`), MP3, buzzer, button, WS2812B, CAN.

## Layout & workflow

- `src/` firmware · `kicad/stm-auto/` schematic+PCB · `lib`/`include`/`test` placeholders · `.pio/` build (gitignored).
- Feature-branch + PR into `main`; Conventional Commits referencing the issue (`#12 feat!: ...`).
- Keep `pins.h` and `DECISION_MATRIX.md` in sync with code — both lead the firmware.
