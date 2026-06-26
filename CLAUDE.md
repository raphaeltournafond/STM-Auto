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
pio test -e native                    # run host unit tests (no hardware)
pio run -t clean                      # clean
```

Libs (`platformio.ini`): Adafruit SSD1306 + GFX (firmware); Unity (native tests, auto-installed).

## Architecture

Pure logic lives in `lib/` (Arduino-free, unit-tested); `src/main.cpp` owns hardware I/O and state
and calls into it:

- **`lib/decision/decision.{h,cpp}`** — `evaluateSituation()` (priority matrix §3), `nextFlapState()`
  (§5 hysteresis), `nextLowPressAlarm()` (§4), `temperatureBand()`, `situationAlert()`,
  `interpolate()` and the sensor scale tables. All pure functions taking state as parameters.
- **`lib/ws2812/ws2812.{h,cpp}`** — `alertColor()` and `encodePixelGRB()` (one WS2812B pixel → 9 SPI
  bytes, 3 SPI bits per WS bit). Pure; the SPI2 transmit lives in `main.cpp` (`ws2812Send()`).
- **`lib/buzzer/buzzer.{h,cpp}`** — `buzzerPattern()` (situation → §3 sound pattern) and `buzzerOnAt()`
  (periodic on/off envelope). Pure; `tone()` drive lives in `main.cpp` (`applyBuzzer()`).
- **`lib/ack/ack.{h,cpp}`** — `updateAck()` acknowledge state machine (§6): mutes sound while keeping
  LED+screen, re-arms on a higher-priority alert, auto-returns when clear. Pure; the PB5 read + edge
  detect live in `main.cpp`.
- **`lib/voice/voice.{h,cpp}`** — `voiceTrack()` (situation → §7 MP3 file) and `dfplayerFrame()`
  (DFPlayer Mini command frame + checksum). Pure; the USART1 write lives in `main.cpp`.
- **`lib/lifecycle/lifecycle.{h,cpp}`** — INIT helpers (§2): `tempSensorPlausible()`/
  `pressSensorPlausible()`, `initResultLabel()`, `initFailVoiceTrack()`. Pure.
- `setup()` — runs the **BOOT → INIT** life phases (§2): C-major arpeggio (C5-E5-G5-C6) synced
  with LED blinks, then a POST-style checklist for the blocking self-tests (OLED → MP3 → temp →
  pressure → flap cycle). A failed check calls `enterInitFail()` (red LED + fault voice + long beeps);
  the ACK button bypasses it. INIT OK plays the track-1 ready jingle then runs a gauge sweep
  animation (0 % → 100 % → real values; error rows blink instead of sweeping) before entering `loop()`.
- `loop()` @200 ms — read sensors, run the decision functions, dispatch outputs: `applyFlap()` (twin
  servos), `applyAlertLed()` (WS2812B primary + PC13 backup), `applyBuzzer()` (piezo via `tone()`,
  muted when acknowledged), `onSituationChange()` (edge-triggered MP3 voice via USART1), and
  `updateDisplay()` throttled to 500 ms with `OLED_I2C.begin()` before each frame (STM32F103 I2C
  BUSY-flag errata). The 200 ms wait is split into 10 × 20 ms polls so ACK taps are never missed.
  `main.cpp` holds the evolving state (`flapOpen`, `lowPressAlarm`, `buzzerSounding`, `ackState`,
  `prevSituation`).
- **Tests**: `test/test_*/` (Unity) run via `pio test -e native` against `lib/` — the `native` env
  excludes `src/` so no hardware/toolchain is needed.

**Conventions to preserve:**
- `lib/` modules stay Arduino-free (pure functions, state passed in) so they build natively — keep
  hardware (Servo/OLED/ADC/SPI/tone) in `src/main.cpp`. Add a test when you add logic.
- Interpolation x-tables (`resTable`, `vPressureTable` in `lib/decision/decision.cpp`) **must stay
  strictly increasing**, each in sync with its y-table (`tempTable`, `barTable`) — `interpolate()`
  assumes it.
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
| Debug Serial (USART2) | `PA2`/`PA3` | | |

Notes: `Servo` lib owns **TIM2**, `tone()` owns **TIM3** (shared timers — don't reuse them for PWM).
USART1 = MP3, USART2 = debug `Serial` (remapped via `build_flags` in `platformio.ini`, since the
variant defaults `Serial` to USART1). CAN uses the USB pins → remove the PA12 USB pull-up. WS2812B
needs a level shifter (or ~4.3V supply).

`main.cpp` includes `pins.h` and uses these macros directly — change a pin in `pins.h` only.
Wired but not yet coded: CAN.

## Layout & workflow

- `src/` firmware · `kicad/stm-auto/` schematic+PCB · `sounds/` MP3 voice files (tracks 1-5, §7) · `lib`/`include`/`test` placeholders · `.pio/` build (gitignored).
- Feature-branch + PR into `main`; Conventional Commits referencing the issue (`#12 feat!: ...`).
- Keep `pins.h` and `DECISION_MATRIX.md` in sync with code — both lead the firmware.
