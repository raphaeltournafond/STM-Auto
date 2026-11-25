#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <IWatchdog.h>
#include <string.h>
#include "pins.h"      // pin map (single source of truth)
#include "decision.h"  // hardware-independent decision logic (unit-tested)
#include "run.h"       // RUN-phase decision tick (unit-tested)
#include "ws2812.h"    // WS2812B pixel encoding (unit-tested)
#include "buzzer.h"    // buzzer alert patterns (unit-tested)
#include "ack.h"       // acknowledge state machine (unit-tested)
#include "voice.h"     // MP3 voice mapping + DFPlayer frames (unit-tested)
#include "lifecycle.h" // INIT self-test helpers (unit-tested)
#include "gauge.h"     // OLED gauge scales (unit-tested)
#include "screen.h"    // OLED alarm cycling / layout / raw values (unit-tested)

// ========== CONFIGURATION ==========

// ----- OLED Screen -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 // Default to -1 to share the board reset pin
#define SCREEN_ADDRESS 0x3C // Default to 0x3C for standard I2C adress

TwoWire OLED_I2C(PIN_OLED_SDA, PIN_OLED_SCL);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &OLED_I2C, OLED_RESET);

const uint32_t DISPLAY_PERIOD_MS   = 500; // OLED refresh
const uint32_t TELEMETRY_PERIOD_MS = 500; // CSV line on the debug Serial

// ----- HARDWARE Constants -----
const float VREF = 3.3f;
const uint16_t ADC_MAX = 4095;
const float TEMP_SERIES_RESISTOR = 220.0f; // temperature divider series resistor
const float PRESS_DIVIDER_RATIO = 1.5f;    // 10/20 kOhm pressure divider

// ----- Sampling + loop timing -----
// A RUN tick is TICK_SLICES x SLICE_MS (~200 ms). Each slice takes an ADC burst
// per sensor, so a tick's reading averages TICK_SLICES x ADC_BURST samples spread
// over the whole tick: this filters ignition/alternator ripple and oil-pressure
// pulsation, which a single back-to-back burst (a few µs) cannot.
const uint8_t  TICK_SLICES     = 10;
const uint32_t SLICE_MS        = 20;
const uint8_t  ADC_BURST       = 4;  // samples per sensor per slice
const uint8_t  ADC_SAMPLES_ONE = 16; // one-shot reads (INIT checks, boot animation)

// ----- Hardware watchdog (DECISION_MATRIX §2) -----
// Armed first thing in setup() and reloaded by every wait (waitMs, the INIT_FAIL
// loop, each 20 ms RUN slice). If anything hangs (e.g. I2C lock-up) the MCU
// resets and comes back straight into RUN (fast restart).
const uint32_t WATCHDOG_TIMEOUT_US = 2000000; // 2 s

// ----- FLAP Servos (twin, driven in tandem) -----
const uint8_t FLAP_CLOSED = 0;  // servo angle
const uint8_t FLAP_OPEN   = 90; // servo angle
Servo flapServo1;
Servo flapServo2;

// ----- RUN state (evolved by the pure runTick(), see lib/run) -----
RunState run = runStateInit();
bool buzzerSounding = false; // current buzzer tone on/off (avoids retriggering tone())
int prevSituation = -1;     // last dispatched situation (edge-triggered sound/voice)
bool prevAckButton = false; // previous touch-button level (for rising-edge detect)

// Delay that keeps the watchdog fed: use instead of delay() for any wait > ~1 s
// total, and everywhere in setup().
void waitMs(uint32_t ms) {
    const uint32_t start = millis();
    for (;;) {
        IWatchdog.reload();
        const uint32_t elapsed = millis() - start;
        if (elapsed >= ms) return;
        const uint32_t left = ms - elapsed;
        delay(left < SLICE_MS ? left : SLICE_MS);
    }
}

// ----- WS2812B RGB LED (primary indicator) -----
// Driven over SPI2 MOSI (PIN_WS2812_DATA); SCLK/MISO are SPI2's other pins,
// reserved in pins.h but left unconnected. Each WS2812 bit is 3 SPI bits at
// ~2.4 MHz (see lib/ws2812). A blocking 9-byte transfer is IRQ-safe (no
// bit-banging) and ample for one pixel.
SPIClass WS2812_SPI(PIN_WS2812_DATA, PIN_WS2812_SPI_MISO, PIN_WS2812_SPI_SCLK);

void ws2812Send(const uint8_t* buf, uint8_t len) {
    WS2812_SPI.beginTransaction(SPISettings(2400000, MSBFIRST, SPI_MODE0));
    for (uint8_t i = 0; i < len; i++) WS2812_SPI.transfer(buf[i]);
    WS2812_SPI.endTransaction();
}

void ws2812ShowAlert(AlertLevel level) {
    uint8_t buf[WS2812_PIXEL_BYTES];
    encodePixelGRB(alertColor(level), buf);
    ws2812Send(buf, sizeof(buf));
}

// ----- MP3 voice (DFPlayer Mini on USART1) -----
// Debug Serial is on USART2 (see platformio.ini build_flags), so USART1 is free.
// STM32duino core >= 3.0 renamed its HardwareSerial class to Uart (HardwareSerial
// is now the abstract ArduinoCore-API base); the (rx, tx) pin constructor is unchanged.
const uint8_t MP3_VOLUME = 22; // 0..30

Uart MP3Serial(PIN_MP3_RX, PIN_MP3_TX);

void dfplayerSend(uint8_t cmd, uint16_t param) {
    uint8_t frame[DF_FRAME_LEN];
    dfplayerFrame(cmd, param, frame);
    MP3Serial.write(frame, DF_FRAME_LEN);
}

void playVoice(int track) { if (track > 0) dfplayerSend(DF_CMD_PLAY_INDEX, (uint16_t)track); }
void stopVoice()          { dfplayerSend(DF_CMD_STOP, 0); }

// INIT check 2 (§2): ask the DFPlayer how many files its SD card holds. Passes on
// a non-zero count; a missing card, a silent module or a 0 count fails. Retries
// cover a module still initialising its card (it answers "busy" meanwhile).
bool mp3CardPresent() {
    while (MP3Serial.available()) MP3Serial.read(); // drop power-up chatter
    DfRxParser rx = {{0}, 0};
    for (uint8_t attempt = 0; attempt < 4; attempt++) {
        dfplayerSend(DF_CMD_QUERY_SD_FILES, 0);
        const uint32_t t0 = millis();
        while (millis() - t0 < 500) {
            IWatchdog.reload();
            while (MP3Serial.available()) {
                uint8_t cmd;
                uint16_t param;
                if (!dfRxPush(&rx, (uint8_t)MP3Serial.read(), &cmd, &param)) continue;
                if (cmd == DF_CMD_QUERY_SD_FILES) return param > 0;
                // DF_CMD_ERROR (busy / no card) or unrelated frame: retry
            }
        }
    }
    return false;
}


// ========== HARDWARE SENSOR READS ==========

uint32_t readPinSum(uint8_t pin, uint8_t samples) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < samples; i++) sum += analogRead(pin);
    return sum;
}

float countsToVolts(uint32_t sum, uint16_t samples) {
    return ((float)sum / samples) * (VREF / ADC_MAX);
}

float dividerResistance(float voltage) {
    if (voltage >= (VREF - 0.01f)) voltage = VREF - 0.01f; // prevent div by zero
    return (voltage * TEMP_SERIES_RESISTOR) / (VREF - voltage);
}

// One-shot reads (INIT + boot animation); RUN averages over the tick instead.
float readTemperatureResistance() {
    return dividerResistance(countsToVolts(readPinSum(PIN_TEMP_SENSE, ADC_SAMPLES_ONE), ADC_SAMPLES_ONE));
}

float readPressureVoltage() {
    return countsToVolts(readPinSum(PIN_PRESS_SENSE, ADC_SAMPLES_ONE), ADC_SAMPLES_ONE) * PRESS_DIVIDER_RATIO;
}


// ========== OLED DRAWING ==========

bool oledOk = false; // set by the OLED self-test; gates display use afterwards

// Header line text (y=0-15) in the current text colour.
static void drawHeader(const char* text, int x) {
    display.setTextSize(1);
    display.setCursor(x, 4);
    display.print(text);
}

// Shared by the boot animation and the RUN screen: temperature row (y=16-39),
// separator, pressure row (y=41-63). Fills are liquid-gauge backgrounds; the
// INVERSE text auto-inverts over them. A row in error shows "ERR <raw value>".
static void drawGaugeRows(float temperature, float pressure, bool tErr, bool pErr,
                          int tFill, int pFill, bool tTextVisible,
                          const char* tRaw, const char* pRaw) {
    char buf[16];
    if (tFill > 0) display.fillRect(0, 16, tFill > SCREEN_WIDTH ? SCREEN_WIDTH : tFill, 24, SSD1306_WHITE);
    display.setTextColor(SSD1306_INVERSE);
    if (tErr) {
        snprintf(buf, sizeof(buf), "ERR %s", tRaw);
        display.setTextSize(2);
        display.setCursor(centeredTextX(strlen(buf), 12, SCREEN_WIDTH), 20);
        display.print(buf);
    } else if (tTextVisible) {
        display.setTextSize(3);
        display.setCursor(centeredTextX(tempTextChars(temperature), 18, SCREEN_WIDTH), 16);
        display.print((int)temperature);
        display.write(247);
        display.print("C");
    }

    display.drawFastHLine(0, 40, SCREEN_WIDTH, SSD1306_WHITE);

    if (pFill > 0) display.fillRect(0, 41, pFill > SCREEN_WIDTH ? SCREEN_WIDTH : pFill, 23, SSD1306_WHITE);
    if (pErr) {
        snprintf(buf, sizeof(buf), "ERR %s", pRaw);
        display.setTextSize(2);
        display.setCursor(centeredTextX(strlen(buf), 12, SCREEN_WIDTH), 45);
        display.print(buf);
    } else {
        display.setTextSize(3);
        display.setCursor(centeredTextX(pressTextChars(pressure), 18, SCREEN_WIDTH), 42);
        display.print(pressure, 1);
        display.print("b");
    }
    display.setTextColor(SSD1306_WHITE);
}

// Push the frame. OLED_I2C.begin() first: STM32F103 I2C BUSY-flag errata.
static void showFrame() {
    OLED_I2C.begin();
    display.display();
}


// ========== SETUP (BOOT + INIT, DECISION_MATRIX §2) ==========

void initShow(uint8_t stepsDone) {
    if (!oledOk) return;
    static const char* const steps[] = {"OLED", "MP3", "TEMP", "PRESS", "SERVO"};
    const uint8_t N = 5;
    display.clearDisplay();
    display.setTextWrap(false);
    display.setTextColor(SSD1306_WHITE);
    drawHeader("SWEE.BRZ", 40);
    if (stepsDone >= N) {
        display.setTextSize(2);
        display.setCursor(28, 30);
        display.print("READY!");
    } else {
        for (uint8_t i = 0; i < N; i++) {
            display.setTextSize(1);
            display.setCursor(4, 16 + i * 9);
            if      (i < stepsDone)  display.print("[OK] ");
            else if (i == stepsDone) display.print("[>>] ");
            else                     display.print("[  ] ");
            display.print(steps[i]);
        }
    }
    showFrame();
}

static void drawInitFail(InitResult r, const char* raw, uint32_t secondsLeft) {
    display.clearDisplay();
    display.setTextWrap(false);
    display.setTextColor(SSD1306_WHITE);
    drawHeader("!! INIT FAIL !!", 19);
    display.setCursor(2, 22);                  // failed component + raw value
    display.print(initResultLabel(r));
    if (raw && raw[0]) { display.print(' '); display.print(raw); }
    display.setCursor(2, 38);
    display.print("Check connections.");
    display.setCursor(2, 52);
    display.print("ACK to skip (");
    display.print(secondsLeft);
    display.print("s)");
    showFrame();
}

// INIT_FAIL (§2): fault voice, then the failed step's code as N long red blinks +
// beeps, and the error + raw value on screen. Ends on an ACK press (bypass) or
// after INIT_FAIL_TIMEOUT_MS, so a reset while driving never leaves the engine
// unmonitored: INIT carries on and RUN reports the fault itself.
void enterInitFail(InitResult r, const char* raw) {
    Serial.print("INIT FAIL: ");
    Serial.print(initResultLabel(r));
    if (raw) { Serial.print(' '); Serial.print(raw); }
    Serial.println();
    playVoice(initFailVoiceTrack(r));

    const uint8_t code = initFailCode(r);
    const uint32_t t0 = millis();
    bool prevAck = digitalRead(PIN_ACK_BUTTON); // a button already held is not a press
    bool on = false, first = true;
    uint32_t shownSeconds = 0;
    bool acked = false;
    for (;;) {
        IWatchdog.reload();
        const uint32_t elapsed = millis() - t0;
        if (elapsed >= INIT_FAIL_TIMEOUT_MS) break;

        const bool want = initFailPatternOn(code, elapsed);
        if (want != on || first) {
            first = false;
            on = want;
            if (on) tone(PIN_BUZZER, BUZZER_FREQ_HZ); else noTone(PIN_BUZZER);
            ws2812ShowAlert(on ? ALERT_ALARM : ALERT_OFF);
            digitalWrite(PIN_STATUS_LED, on ? LOW : HIGH); // active LOW
        }
        const uint32_t secondsLeft = (INIT_FAIL_TIMEOUT_MS - elapsed + 999) / 1000;
        if (oledOk && secondsLeft != shownSeconds) {
            shownSeconds = secondsLeft;
            drawInitFail(r, raw, secondsLeft);
        }
        const bool ackNow = digitalRead(PIN_ACK_BUTTON);
        if (ackNow && !prevAck) { acked = true; break; }
        prevAck = ackNow;
        delay(SLICE_MS);
    }
    noTone(PIN_BUZZER);
    ws2812ShowAlert(ALERT_OFF);
    digitalWrite(PIN_STATUS_LED, HIGH);
    if (acked) { tone(PIN_BUZZER, BUZZER_FREQ_HZ); delay(60); noTone(PIN_BUZZER); }
    Serial.println(acked ? "INIT FAIL bypassed (ACK)" : "INIT FAIL timed out -> continuing");
}

// tFill/pFill override the gauge fill widths for the animation.
static void bootGaugeFrame(float temperature, float pressure, bool tErr, bool pErr,
                           int tFill, int pFill, const char* tRaw, const char* pRaw) {
    display.clearDisplay();
    display.setTextWrap(false);
    display.setTextColor(SSD1306_WHITE);
    drawHeader("SWEE.BRZ", 40);
    drawGaugeRows(temperature, pressure, tErr, pErr, tFill, pFill, true, tRaw, pRaw);
    showFrame();
    IWatchdog.reload();
}

// Gauge sweep 0 % -> 100 % -> real values; error rows blink instead of sweeping.
void gaugeBootAnimation() {
    if (!oledOk) return;
    const float resistance  = readTemperatureResistance();
    const float temperature = temperatureFromResistance(resistance);
    const float vPressure   = readPressureVoltage();
    const float pressure    = pressureFromVoltage(vPressure);
    const bool tErr = tempSensorFault(resistance);
    const bool pErr = pressSensorFault(vPressure);
    char tRaw[8], pRaw[8];
    formatResistance(resistance, tRaw, sizeof(tRaw));
    formatVoltage(vPressure, pRaw, sizeof(pRaw));
    const int tTgt = tErr ? 0 : gaugeFillWidth(temperature, GAUGE_TEMP_MIN, GAUGE_TEMP_MAX, SCREEN_WIDTH);
    const int pTgt = pErr ? 0 : gaugeFillWidth(pressure, GAUGE_PRESS_MIN, GAUGE_PRESS_MAX, SCREEN_WIDTH);

    uint32_t blinkMs = millis();
    bool     blinkOn = false;
    auto errFill = [&]() {
        if (millis() - blinkMs >= 150) { blinkOn = !blinkOn; blinkMs = millis(); }
        return blinkOn ? SCREEN_WIDTH : 0;
    };

    for (int w = 0; w <= SCREEN_WIDTH; w += 4) {
        const int ef = errFill();
        bootGaugeFrame(temperature, pressure, tErr, pErr, tErr ? ef : w, pErr ? ef : w, tRaw, pRaw);
        waitMs(8);
    }
    const int ef = errFill();
    bootGaugeFrame(temperature, pressure, tErr, pErr,
                   tErr ? ef : SCREEN_WIDTH, pErr ? ef : SCREEN_WIDTH, tRaw, pRaw);
    waitMs(80);

    for (int s = 0; s <= 10; s++) {
        const int ef2 = errFill();
        bootGaugeFrame(temperature, pressure, tErr, pErr,
                       tErr ? ef2 : SCREEN_WIDTH + (tTgt - SCREEN_WIDTH) * s / 10,
                       pErr ? ef2 : SCREEN_WIDTH + (pTgt - SCREEN_WIDTH) * s / 10, tRaw, pRaw);
        waitMs(12);
    }
}

void attachFlaps() {
    flapServo1.attach(PIN_SERVO_FLAP_1);
    flapServo2.attach(PIN_SERVO_FLAP_2);
    flapServo1.write(FLAP_CLOSED);
    flapServo2.write(FLAP_CLOSED);
}

void bootAndInit(); // full BOOT + INIT sequence, defined below

void setup() {
    // ===== BOOT =====
    // Watchdog first: a hang anywhere in BOOT/INIT (e.g. I2C lock-up in
    // display.begin()) now resets into the fast-restart path instead of freezing.
    const bool watchdogReset = IWatchdog.isReset(true);
    IWatchdog.begin(WATCHDOG_TIMEOUT_US);

    Serial.begin(115200);                 // debug on USART2 (build_flags)
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);   // HIGH = off (active LOW)
    pinMode(PIN_ACK_BUTTON, INPUT);       // touch module drives the line (no pull)
    analogReadResolution(12);
    WS2812_SPI.begin();
    ws2812ShowAlert(ALERT_OFF);
    MP3Serial.begin(9600);                // DFPlayer Mini default baud

    if (watchdogReset) {
        // Something hung: get back to monitoring at once. Skip the jingle,
        // animations, flap cycle and INIT_FAIL (RUN reports any sensor fault
        // itself). The OLED + DFPlayer kept power, no wait needed.
        Serial.println("Watchdog reset -> fast restart into RUN");
        attachFlaps();
        OLED_I2C.begin();
        oledOk = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
    } else {
        bootAndInit();
    }
    // Telemetry header (one "T," line per TELEMETRY_PERIOD_MS, see printTelemetry).
    Serial.println("# T,t_ms,ohm,degC,volt,bar,situation,flap,ack");
}

void bootAndInit() {
    waitMs(500); // let the screen + DFPlayer power up

    // C major arpeggio (C5-E5-G5-C6) synced with LED blink
    static const uint16_t bootNotes[] = {523, 659, 784, 1047};
    for (uint8_t i = 0; i < 4; i++) {
        tone(PIN_BUZZER, bootNotes[i]);
        digitalWrite(PIN_STATUS_LED, LOW);  waitMs(100);
        noTone(PIN_BUZZER);
        digitalWrite(PIN_STATUS_LED, HIGH); waitMs(60);
    }
    for (uint8_t i = 0; i < 2; i++) {
        digitalWrite(PIN_STATUS_LED, LOW);  waitMs(80);
        digitalWrite(PIN_STATUS_LED, HIGH); waitMs(80);
    }
    // Servos parked before INIT so flaps stay controlled through any blocking check.
    attachFlaps();
    dfplayerSend(DF_CMD_SET_VOLUME, MP3_VOLUME); // before INIT so fail voice is at the right level

    // ===== INIT =====
    char raw[8];
    OLED_I2C.begin();
    oledOk = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
    if (!oledOk) enterInitFail(INIT_FAIL_OLED, nullptr);
    initShow(1); waitMs(400);

    if (!mp3CardPresent()) enterInitFail(INIT_FAIL_MP3_SD, nullptr);
    initShow(2); waitMs(400);

    const float resistance = readTemperatureResistance();
    if (!tempSensorPlausible(resistance)) {
        formatResistance(resistance, raw, sizeof(raw));
        enterInitFail(INIT_FAIL_TEMP, raw);
    }
    initShow(3); waitMs(400);

    const float vPressure = readPressureVoltage();
    if (!pressSensorPlausible(vPressure)) {
        formatVoltage(vPressure, raw, sizeof(raw));
        enterInitFail(INIT_FAIL_PRESS, raw);
    }
    initShow(4); waitMs(400);

    flapServo1.write(FLAP_CLOSED); flapServo2.write(FLAP_CLOSED); waitMs(400);
    flapServo1.write(FLAP_OPEN);   flapServo2.write(FLAP_OPEN);   waitMs(400);
    flapServo1.write(FLAP_CLOSED); flapServo2.write(FLAP_CLOSED);

    // ===== INIT OK -> RUN =====
    playVoice(1);
    initShow(5); waitMs(400);
    gaugeBootAnimation();
}


// ========== SITUATION OUTPUTS ==========

// ----- FLAP (servos) -----
void applyFlap(bool open) {
    uint8_t angle = open ? FLAP_OPEN : FLAP_CLOSED;
    flapServo1.write(angle);
    flapServo2.write(angle);
}

// ----- LED -----
// Primary indicator: WS2812B (off/yellow/red). Onboard PC13 (active LOW) mirrors
// it as a backup: lit on any alert, off otherwise.
void applyAlertLed(AlertLevel level) {
    ws2812ShowAlert(level);
    digitalWrite(PIN_STATUS_LED, level == ALERT_OFF ? HIGH : LOW);
}

// ----- BUZZER (per-loop envelope) -----
// Passive piezo on PIN_BUZZER via tone() (TIM3). The pure pattern/envelope logic
// lives in lib/buzzer; here we only gate the tone on transitions.
void applyBuzzer(Situation sit, bool muted) {
    BuzzerPattern pattern = muted ? BUZZ_SILENT : buzzerPattern(sit);
    bool on = buzzerOnAt(pattern, millis());
    if (on && !buzzerSounding)      { tone(PIN_BUZZER, BUZZER_FREQ_HZ); buzzerSounding = true; }
    else if (!on && buzzerSounding) { noTone(PIN_BUZZER); buzzerSounding = false; }
}

// ----- VOICE (edge-triggered) -----
// Play the situation's voice prompt (§7) once when it becomes active, unless the
// alert channel is acknowledged (§6). Called after runTick() so a higher-priority
// alert has already re-armed it.
void onSituationChange(Situation sit) {
    Serial.print("Situation -> ");
    Serial.println(situationName(sit));
    playVoice(voiceTrackOnEntry(sit, run.ack.acked));
}

// ----- DISPLAY -----
// Status line: the active alarm, alternating between simultaneous alarms (§6);
// REGULATING/NORMAL follow the flap. Gauges: values, or "ERR <raw Ω/V>" on a
// sensor fault (§3). The temperature blinks while the overheat alarm is active.
void updateDisplay(const RunResult& r, float resistance, float vPressure, uint32_t nowMs) {
    static bool blinkPhase = false;
    blinkPhase = !blinkPhase; // one toggle per frame (~DISPLAY_PERIOD_MS)

    display.clearDisplay();
    display.setTextWrap(false);

    const Situation shown = displaySituation(alarmToShow(r.sit, r.alertMask, nowMs), run.flapOpen);
    if (situationAlert(shown) != ALERT_OFF) { // alerts invert the strip for visibility
        display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
    } else {
        display.setTextColor(SSD1306_WHITE);
    }
    const char* flap = run.flapOpen ? "OPEN" : "CLSD";
    display.setTextSize(1);
    display.setCursor(2, 4);
    switch (shown) {
        case SIT_STOP_ENGINE:      display.print("!! STOP ENGINE !!"); break;
        case SIT_TEMP_SENSOR_ERR:  display.print("TEMP SENSOR ERROR"); break;
        case SIT_PRESS_SENSOR_ERR: display.print("PRESS SENSOR ERROR"); break;
        case SIT_LOW_PRESSURE:     display.print("LOW OIL PRESSURE"); break;
        case SIT_OVERPRESSURE:     display.print("OVERPRESSURE"); break;
        case SIT_MILD_OVERHEAT:    display.print("OIL HOT!  FLAP:"); display.print(flap); break;
        case SIT_REGULATION:       display.print("REGULATING FLAP:"); display.print(flap); break;
        case SIT_NORMAL:           display.print("NORMAL    FLAP:"); display.print(flap); break;
        case SIT_COLD:             display.print("COLD      FLAP:"); display.print(flap); break;
    }
    display.setTextColor(SSD1306_WHITE);

    const bool tErr = tempSensorFault(resistance);
    const bool pErr = pressSensorFault(vPressure);
    const bool hot  = r.alertMask & (1u << SIT_MILD_OVERHEAT);
    char tRaw[8], pRaw[8];
    formatResistance(resistance, tRaw, sizeof(tRaw));
    formatVoltage(vPressure, pRaw, sizeof(pRaw));
    drawGaugeRows(r.temperature, r.pressure, tErr, pErr,
                  tErr ? 0 : gaugeFillWidth(r.temperature, GAUGE_TEMP_MIN, GAUGE_TEMP_MAX, SCREEN_WIDTH),
                  pErr ? 0 : gaugeFillWidth(r.pressure, GAUGE_PRESS_MIN, GAUGE_PRESS_MAX, SCREEN_WIDTH),
                  !hot || blinkPhase, tRaw, pRaw);
    showFrame();
}

// ----- TELEMETRY -----
// One CSV line per TELEMETRY_PERIOD_MS on the debug Serial, for bench calibration
// and in-car logging: grep "^T," (header printed at the end of setup()).
void printTelemetry(const RunResult& r, float resistance, float vPressure) {
    Serial.print("T,");
    Serial.print(millis());          Serial.print(',');
    Serial.print(resistance, 0);     Serial.print(',');
    Serial.print(r.temperature, 1);  Serial.print(',');
    Serial.print(vPressure, 3);      Serial.print(',');
    Serial.print(r.pressure, 2);     Serial.print(',');
    Serial.print(situationName(r.sit)); Serial.print(',');
    Serial.print(run.flapOpen ? 1 : 0); Serial.print(',');
    Serial.println(run.ack.acked ? 1 : 0);
}


// On an ACK rising edge: mute an in-progress voice prompt and give a feedback blip.
void onAckPress() {
    if (run.ack.acked) stopVoice();
    tone(PIN_BUZZER, BUZZER_FREQ_HZ); delay(60); noTone(PIN_BUZZER);
    buzzerSounding = false;
}


// ========== MAIN LOOP ==========

void loop() {
    // ----- SAMPLE + WAIT (~200 ms) -----
    // TICK_SLICES x 20 ms: feed the watchdog, take an ADC burst per sensor, and
    // poll the ACK button so short taps are never missed (acked immediately).
    uint32_t tSum = 0, pSum = 0;
    for (uint8_t i = 0; i < TICK_SLICES; i++) {
        delay(SLICE_MS);
        IWatchdog.reload();
        tSum += readPinSum(PIN_TEMP_SENSE, ADC_BURST);
        pSum += readPinSum(PIN_PRESS_SENSE, ADC_BURST);
        const bool ackNow = digitalRead(PIN_ACK_BUTTON);
        if (ackNow && !prevAckButton) {
            run.ack = updateAck(run.ack, run.filter.stable, true);
            onAckPress();
        }
        prevAckButton = ackNow;
    }
    const uint16_t samples  = (uint16_t)TICK_SLICES * ADC_BURST;
    const float resistance  = dividerResistance(countsToVolts(tSum, samples));
    const float vPressure   = countsToVolts(pSum, samples) * PRESS_DIVIDER_RATIO;

    // ----- DECISION (§3-§6, lib/run) -----
    RunResult r;
    run = runTick(run, resistance, vPressure, false, &r); // ACK edges handled above

    // ----- OUTPUTS -----
    applyFlap(run.flapOpen);
    applyAlertLed(situationAlert(r.sit)); // LED stays on even when acknowledged
    applyBuzzer(r.sit, run.ack.acked);    // ack mutes the buzzer
    if ((int)r.sit != prevSituation) { onSituationChange(r.sit); prevSituation = r.sit; }

    const uint32_t nowMs = millis();
    static uint32_t lastDisplayMs = 0, lastTelemetryMs = 0;
    if (oledOk && (nowMs - lastDisplayMs >= DISPLAY_PERIOD_MS)) {
        lastDisplayMs = nowMs;
        updateDisplay(r, resistance, vPressure, nowMs);
    }
    if (nowMs - lastTelemetryMs >= TELEMETRY_PERIOD_MS) {
        lastTelemetryMs = nowMs;
        printTelemetry(r, resistance, vPressure);
    }
}
