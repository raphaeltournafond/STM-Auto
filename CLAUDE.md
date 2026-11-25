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
pio test -e native                    # run host unit tests (no hardware; needs host gcc)
scripts\test-native-msvc.cmd          # same tests with MSVC (Windows, no gcc)
pio run -t clean                      # clean
```

Libs (`platformio.ini`, exact versions): Adafruit SSD1306 + GFX + BusIO (firmware); Unity (native
tests, auto-installed).
Platform: `platformio/ststm32 @ ~20.0.0` = **STM32duino core 3.x** (ArduinoCore-API based). In core 3
`arduino::HardwareSerial` is an abstract base — instantiate extra UARTs as `Uart` (e.g.
`Uart MP3Serial(rx, tx)`); `SPISettings`/`HardwareSPI`/`HardwareI2C` come from ArduinoCore-API.

## Architecture

Pure logic lives in `lib/` (Arduino-free, unit-tested); `src/main.cpp` owns hardware I/O and state
and calls into it:

- **`lib/decision/decision.{h,cpp}`** — `evaluateSituation()` (priority matrix §3), `nextFlapState()`
  (§5 hysteresis), `nextAlarmLatches()` (§4 low-press + §5 overheat/overpressure latches),
  `nextSituationFilter()` (3-tick debounce), `activeAlertMask()` (every active alarm, for the
  multi-alarm display), `tempSensorFault()`/`pressSensorFault()`,
  `displaySituation()` (status label: REGULATING/NORMAL follow the flap), `temperatureBand()`,
  `lowPressCritical()` (current §4 floor, drawn on the pressure graph),
  `situationAlert()`, `interpolate()` and the sensor scale tables. All pure functions taking state
  as parameters.
- **`lib/run/run.{h,cpp}`** — `runTick()`: one RUN decision tick chaining the helpers (conversions →
  latches → matrix → debounce → ack → flap) over a `RunState`; returns a `RunResult` (temps, raw +
  debounced situation, alert mask). `test_run` replays whole drive scenarios through it.
- **`lib/gauge/gauge.{h,cpp}`** — OLED gauge scales (`GAUGE_TEMP_*` 40–150 °C, `GAUGE_PRESS_*`
  0–10 bar) and `gaugeFillWidth()`. A `static_assert` keeps `PRESS_OVER` inside the pressure bar.
- **`lib/screen/screen.{h,cpp}`** — `alarmToShow()` (§6 multi-alarm alternation, 2 s each, STOP
  ENGINE never alternates), text layout (`centeredTextX()`, `tempTextChars()`, `pressTextChars()`)
  and raw-value formatting without float printf (`formatResistance()` "72.4k", `formatVoltage()`,
  `formatDuration()`).
- **`lib/ws2812/ws2812.{h,cpp}`** — `alertColor()` and `encodePixelGRB()` (one WS2812B pixel → 9 SPI
  bytes, 3 SPI bits per WS bit). Pure; the SPI2 transmit lives in `main.cpp` (`ws2812Send()`).
- **`lib/buzzer/buzzer.{h,cpp}`** — `buzzerPattern()` (situation → §3 sound pattern) and `buzzerOnAt()`
  (periodic on/off envelope). Pure; `tone()` drive lives in `main.cpp` (`applyBuzzer()`).
- **`lib/ack/ack.{h,cpp}`** — `updateAck()` acknowledge state machine (§6): mutes sound while keeping
  LED+screen, re-arms on a higher-priority alert, auto-returns when clear. Pure; the PB5 read + edge
  detect live in `main.cpp`.
- **`lib/button/button.{h,cpp}`** — `buttonUpdate()`: touch-button gestures (§6/§10). A press while
  an alert awaits ACK is `BTN_ACK` (on the press edge, nothing else); otherwise `BTN_TAP` (released
  < `LONG_PRESS_MS` 1.5 s) or `BTN_LONG` (fires once while held).
- **`lib/views/views.{h,cpp}`** — views + carousel (§10): `View` (MAIN/TEMP/PRESS/STATS/DIAG),
  `viewNext()` (tap), `viewToggleCarousel()` (long press), `viewTick()` (advance every
  `CAROUSEL_PERIOD_MS` 5 s, skips DIAG, held during a pending alert), `viewToShow()` (pending alert
  forces MAIN).
- **`lib/trend/trend.{h,cpp}`** — `History` ring buffer (128 × 5 s, int16 tenths, gaps for sensor
  faults), `graphRange()`/`graphY()` scaling, `SessionStats`/`statsUpdate()` (T max, P max, P min
  with warm oil, time ≥ 110 °C).
- **`lib/voice/voice.{h,cpp}`** — `voiceTrack()` (situation → §7 MP3 file), `dfplayerFrame()`
  (DFPlayer Mini command frame + checksum) and `dfRxPush()` (incremental reply-frame parser, used by
  the INIT SD-card query). Pure; the USART1 I/O lives in `main.cpp`.
- **`lib/lifecycle/lifecycle.{h,cpp}`** — INIT helpers (§2): `tempSensorPlausible()`/
  `pressSensorPlausible()`, `initResultLabel()`, `initFailVoiceTrack()`, `initFailCode()` +
  `initFailPatternOn()` (N-pulse LED/beep code) and `INIT_FAIL_TIMEOUT_MS`. Pure.
- `setup()` — arms the watchdog first, then runs the **BOOT → INIT** life phases (§2) (or, after a
  watchdog reset, a fast restart straight into RUN): C-major arpeggio (C5-E5-G5-C6) synced with LED
  blinks, then a POST-style checklist (OLED → MP3 SD file-count query `mp3CardPresent()` → temp →
  pressure → flap cycle). A failed check calls `enterInitFail()` (N-pulse code + fault voice + raw
  value on screen), which ends on ACK or after 10 s so INIT always reaches RUN. INIT OK plays the
  track-1 ready jingle then runs a gauge sweep animation before entering `loop()`. All waits use
  `waitMs()` (feeds the watchdog).
- `loop()` ~200 ms — 10 × 20 ms slices, each feeding the watchdog, taking a 4-sample ADC burst per
  sensor (averaged over the tick) and calling `pollButton()` (ACK / next view / carousel toggle);
  then `runTick()`, stats + history (every 5 s), `viewTick()`, and the outputs: `applyFlap()`
  (twin servos), `applyAlertLed()` (WS2812B primary + PC13 backup), `applyBuzzer()` (piezo via
  `tone()`, muted when acknowledged), `onSituationChange()` (edge-triggered MP3 voice via USART1),
  `updateDisplay()` every 500 ms or at once after a gesture / carousel step (dispatches the view:
  main gauges via `drawGaugeRows()`, shared with the boot animation; temp/pressure trend graphs;
  session stats; diag. `showFrame()` calls `OLED_I2C.begin()` first for the STM32F103 I2C BUSY-flag
  errata) and `printTelemetry()` (CSV `T,…` line every 500 ms on debug Serial). `main.cpp` holds
  `RunState run`, `ButtonState button`, `ViewState views`, the two `History` buffers, `stats`,
  `buzzerSounding`, `prevSituation`.
- **Tests**: `test/test_*/` (Unity) run via `pio test -e native` against `lib/` — the `native` env
  excludes `src/` so no hardware/toolchain is needed.

**Conventions to preserve:**
- `lib/` modules stay Arduino-free (pure functions, state passed in) so they build natively — keep
  hardware (Servo/OLED/ADC/SPI/tone) in `src/main.cpp`. Add a test when you add logic.
- Interpolation x-tables (`resTable`, `vPressureTable` in `lib/decision/decision.cpp`) **must stay
  strictly increasing**, each in sync with its y-table (`tempTable`, `barTable`) — `interpolate()`
  assumes it.
- Keep the sensor fail-safes via `tempSensorFault()` / `pressSensorFault()`: resistance outside
  [`TEMP_MIN_SAFE_RESISTANCE`, `TEMP_MAX_SAFE_RESISTANCE`) → `TEMPERATURE ERROR`; pressure volt
  outside [`PRESS_MIN_SAFE_VOLTAGE`, `PRESS_MAX_SAFE_VOLTAGE`] (disconnected / shorted to 5 V) →
  `PRESSURE ERROR`. INIT plausibility reuses the same windows — keep
  it wide enough for a cold-soak start (~18 kΩ at −20 °C).
- The watchdog runs from the start of `setup()`: every wait must feed it — use `waitMs()` rather
  than a long `delay()` in BOOT/INIT, and reload in any new blocking loop (`loop()` reloads every
  20 ms slice). No phase may block indefinitely (INIT_FAIL times out).

## Pins (`src/pins.h` is the source of truth)

| Function | Pin(s) | Function | Pin(s) |
|---|---|---|---|
| Pressure sensor | `PA0` | Buzzer (tone→TIM3) | `PB6` |
| Temp sensor | `PA1` | OLED SCL/SDA (I2C2) | `PB10`/`PB11` |
| Flap servos ×2 (Servo→TIM2) | `PA6`/`PA7` | WS2812B RGB LED (SPI2 MOSI; PB13/PB14 reserved) | `PB15` |
| MP3 (USART1) | `PA9`/`PA10` | Backup LED (onboard) | `PC13` |
| CAN RX/TX | `PA11`/`PA12` | ACK / view touch button | `PB5` |
| Debug Serial (USART2) | `PA2`/`PA3` | | |

Notes: `Servo` lib owns **TIM2**, `tone()` owns **TIM3** (shared timers — don't reuse them for PWM).
USART1 = MP3 (`Uart MP3Serial`), USART2 = debug `Serial` (the core aliases it to `Serial2`; remapped
via `build_flags` in `platformio.ini`, since the variant defaults `Serial` to USART1). CAN uses the USB pins → remove the PA12 USB pull-up. WS2812B
needs a level shifter (or ~4.3V supply).

`main.cpp` includes `pins.h` and uses these macros directly — change a pin in `pins.h` only.
Wired but not yet coded: CAN.

## Layout & workflow

- `src/` firmware · `lib/` pure modules · `test/` Unity suites · `scripts/` MSVC test runner · `kicad/stm-auto/` schematic+PCB · `sounds/` MP3 voice files (tracks 1-5, §7) · `.pio/` build (gitignored).
- Feature-branch + PR into `main`; Conventional Commits referencing the issue (`#12 feat!: ...`).
- Keep `pins.h` and `DECISION_MATRIX.md` in sync with code — both lead the firmware.
