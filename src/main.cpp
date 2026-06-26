#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include "pins.h"      // pin map (single source of truth)
#include "decision.h"  // hardware-independent decision logic (unit-tested)
#include "ws2812.h"    // WS2812B pixel encoding (unit-tested)
#include "buzzer.h"    // buzzer alert patterns (unit-tested)
#include "ack.h"       // acknowledge state machine (unit-tested)
#include "voice.h"     // MP3 voice mapping + DFPlayer frames (unit-tested)
#include "lifecycle.h" // INIT self-test helpers (unit-tested)

// ========== CONFIGURATION ==========

// ----- OLED Screen -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 // Default to -1 to share the board reset pin
#define SCREEN_ADDRESS 0x3C // Default to 0x3C for standard I2C adress

TwoWire OLED_I2C(PIN_OLED_SDA, PIN_OLED_SCL);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &OLED_I2C, OLED_RESET);

// ----- HARDWARE Constants -----
const float VREF = 3.3f;
const uint16_t ADC_MAX = 4095;
const float TEMP_SERIES_RESISTOR = 220.0f; // temperature divider series resistor
const float PRESS_DIVIDER_RATIO = 1.5f;    // 10/20 kOhm pressure divider

// ----- FLAP Servos (twin, driven in tandem) -----
const uint8_t FLAP_CLOSED = 0;  // servo angle
const uint8_t FLAP_OPEN   = 90; // servo angle
Servo flapServo1;
Servo flapServo2;

// ----- Decision state (held here, evolved by the pure decision functions) -----
bool flapOpen = false;      // current flap state (hysteresis), starts closed
bool lowPressAlarm = false; // adaptive low-pressure alarm state (§4)
bool buzzerSounding = false; // current buzzer tone on/off (avoids retriggering tone())
int prevSituation = -1;     // last dispatched situation (edge-triggered sound/voice)
AckState ackState = {false, SIT_COLD}; // acknowledge state (§6)
bool prevAckButton = false; // previous touch-button level (for rising-edge detect)

// ----- WS2812B RGB LED (primary indicator) -----
// Driven over SPI2 MOSI (PIN_WS2812_DATA); SCLK/MISO are SPI2's other pins,
// unused/unconnected. Each WS2812 bit is 3 SPI bits at ~2.4 MHz (see lib/ws2812).
// A blocking 9-byte transfer is IRQ-safe (no bit-banging) and ample for one pixel.
SPIClass WS2812_SPI(PIN_WS2812_DATA, PB14, PB13); // MOSI, (MISO), (SCLK)

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
const uint8_t MP3_VOLUME = 22; // 0..30

HardwareSerial MP3Serial(PIN_MP3_RX, PIN_MP3_TX);

void dfplayerSend(uint8_t cmd, uint16_t param) {
    uint8_t frame[DF_FRAME_LEN];
    dfplayerFrame(cmd, param, frame);
    MP3Serial.write(frame, DF_FRAME_LEN);
}

void playVoice(int track) { if (track > 0) dfplayerSend(DF_CMD_PLAY_INDEX, (uint16_t)track); }
void stopVoice()          { dfplayerSend(DF_CMD_STOP, 0); }


// ========== SETUP (BOOT + INIT, DECISION_MATRIX §2) ==========

float readTemperatureResistance(); // defined below; needed by the INIT checks
float readPressureVoltage();

bool oledOk = false; // set by the OLED self-test; gates display use afterwards

void initShow(uint8_t stepsDone) {
    if (!oledOk) return;
    static const char* const steps[] = {"OLED", "MP3", "TEMP", "PRESS", "SERVO"};
    const uint8_t N = 5;
    display.clearDisplay();
    display.setTextWrap(false);
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(40, 4);
    display.print("SWEE.BRZ");
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
    display.display();
}

// INIT_FAIL (§2): red LED, fault voice, long beeps, error on screen. Blocks until
// the operator presses ACK to bypass (deliberate sensor-unplugged override).
void enterInitFail(InitResult r) {
    ws2812ShowAlert(ALERT_ALARM);
    digitalWrite(PIN_STATUS_LED, LOW); // backup LED on (active LOW)
    playVoice(initFailVoiceTrack(r));
    if (oledOk) {
        display.clearDisplay();
        display.setTextWrap(false);
        display.setTextColor(SSD1306_WHITE);
        // Yellow zone: alert header centred
        display.setTextSize(1);
        display.setCursor(19, 4);
        display.print("!! INIT FAIL !!");
        // Blue zone: failed component + bypass instruction
        display.setCursor(2, 22);
        display.print(initResultLabel(r));
        display.setCursor(2, 38);
        display.print("Check connections.");
        display.setCursor(2, 52);
        display.print("ACK to bypass.");
        display.display();
    }
    bool prevAck = false;
    while (true) {
        tone(PIN_BUZZER, BUZZER_FREQ_HZ);
        for (uint8_t i = 0; i < 12; i++) {       // 600 ms on
            bool ackNow = digitalRead(PIN_ACK_BUTTON);
            if (ackNow && !prevAck) {
                noTone(PIN_BUZZER);
                tone(PIN_BUZZER, BUZZER_FREQ_HZ); delay(60); noTone(PIN_BUZZER);
                return;
            }
            prevAck = ackNow;
            delay(50);
        }
        noTone(PIN_BUZZER);
        for (uint8_t i = 0; i < 8; i++) {        // 400 ms off
            bool ackNow = digitalRead(PIN_ACK_BUTTON);
            if (ackNow && !prevAck) {
                tone(PIN_BUZZER, BUZZER_FREQ_HZ); delay(60); noTone(PIN_BUZZER);
                return;
            }
            prevAck = ackNow;
            delay(50);
        }
    }
}

// tFill/pFill override gauge fill widths for animation; INVERSE text auto-inverts with the fill.
static void bootGaugeFrame(float temperature, float pressure, bool tErr, bool pErr,
                            int tLen, int pLen, int tFill, int pFill) {
    display.clearDisplay();
    display.setTextWrap(false);
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(40, 4);
    display.print("SWEE.BRZ");
    display.setTextSize(3);
    if (tFill > 0) display.fillRect(0, 16, tFill > 128 ? 128 : tFill, 24, SSD1306_WHITE);
    if (tErr) {
        display.setTextColor(SSD1306_INVERSE);
        display.setCursor(37, 16); display.print("ERR");
    } else {
        display.setTextColor(SSD1306_INVERSE);
        display.setCursor((128 - tLen * 18) / 2, 16);
        display.print((int)temperature); display.write(247); display.print("C");
    }
    display.drawFastHLine(0, 40, 128, SSD1306_WHITE);
    if (pFill > 0) display.fillRect(0, 41, pFill > 128 ? 128 : pFill, 23, SSD1306_WHITE);
    if (pErr) {
        display.setTextColor(SSD1306_INVERSE);
        display.setCursor(37, 42); display.print("ERR");
    } else {
        display.setTextColor(SSD1306_INVERSE);
        display.setCursor((128 - pLen * 18) / 2, 42);
        display.print(pressure, 1); display.print("b");
    }
    OLED_I2C.begin();
    display.display();
}

void gaugeBootAnimation() {
    if (!oledOk) return;
    float resistance  = readTemperatureResistance();
    float temperature = temperatureFromResistance(resistance);
    float vPressure   = readPressureVoltage();
    float pressure    = pressureFromVoltage(vPressure);
    const bool tErr = resistance >= TEMP_MAX_SAFE_RESISTANCE;
    const bool pErr = vPressure  < PRESS_MIN_SAFE_VOLTAGE;
    const int  tLen = ((int)temperature >= 100) ? 5 : 4;
    const int  pLen = (pressure >= 10.0f) ? 5 : 4;
    const int  tTgt = tErr ? 0 : (int)(constrain((temperature - 40.0f) / (150.0f - 40.0f), 0.0f, 1.0f) * 128.0f);
    const int  pTgt = pErr ? 0 : (int)(constrain(pressure / 8.0f, 0.0f, 1.0f) * 128.0f);

    uint32_t blinkMs = millis();
    bool     blinkOn = false;

    for (int w = 0; w <= 128; w += 4) {
        if (millis() - blinkMs >= 150) { blinkOn = !blinkOn; blinkMs = millis(); }
        int ef = blinkOn ? 128 : 0;
        bootGaugeFrame(temperature, pressure, tErr, pErr, tLen, pLen,
                       tErr ? ef : w, pErr ? ef : w);
        delay(8);
    }
    if (millis() - blinkMs >= 150) { blinkOn = !blinkOn; blinkMs = millis(); }
    bootGaugeFrame(temperature, pressure, tErr, pErr, tLen, pLen,
                   tErr ? (blinkOn ? 128 : 0) : 128,
                   pErr ? (blinkOn ? 128 : 0) : 128);
    delay(80);

    for (int s = 0; s <= 10; s++) {
        if (millis() - blinkMs >= 150) { blinkOn = !blinkOn; blinkMs = millis(); }
        int ef = blinkOn ? 128 : 0;
        bootGaugeFrame(temperature, pressure, tErr, pErr, tLen, pLen,
                       tErr ? ef : 128 + (tTgt - 128) * s / 10,
                       pErr ? ef : 128 + (pTgt - 128) * s / 10);
        delay(12);
    }
}

void setup() {
    // ===== BOOT =====
    Serial.begin(115200);                 // debug on USART2 (build_flags)
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH);   // HIGH = off (active LOW)
    pinMode(PIN_ACK_BUTTON, INPUT);       // touch module drives the line (no pull)
    analogReadResolution(12);
    WS2812_SPI.begin();
    ws2812ShowAlert(ALERT_OFF);
    MP3Serial.begin(9600);                // DFPlayer Mini default baud

    delay(500); // let the screen + DFPlayer power up

    // C major arpeggio (C5-E5-G5-C6) synced with LED blink
    static const uint16_t bootNotes[] = {523, 659, 784, 1047};
    for (uint8_t i = 0; i < 4; i++) {
        tone(PIN_BUZZER, bootNotes[i]);
        digitalWrite(PIN_STATUS_LED, LOW);  delay(100);
        noTone(PIN_BUZZER);
        digitalWrite(PIN_STATUS_LED, HIGH); delay(60);
    }
    for (uint8_t i = 0; i < 2; i++) {
        digitalWrite(PIN_STATUS_LED, LOW);  delay(80);
        digitalWrite(PIN_STATUS_LED, HIGH); delay(80);
    }
    // Servos parked before INIT so flaps stay controlled through any blocking check.
    flapServo1.attach(PIN_SERVO_FLAP_1);
    flapServo2.attach(PIN_SERVO_FLAP_2);
    flapServo1.write(FLAP_CLOSED);
    flapServo2.write(FLAP_CLOSED);
    dfplayerSend(DF_CMD_SET_VOLUME, MP3_VOLUME); // before INIT so fail voice is at the right level

    // ===== INIT =====
    OLED_I2C.begin();
    oledOk = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
    if (!oledOk) enterInitFail(INIT_FAIL_OLED);
    initShow(1); delay(400);

    // TODO: real DFPlayer SD-presence query (parse status frame); assumed OK for now.
    initShow(2); delay(400);

    if (!tempSensorPlausible(readTemperatureResistance())) enterInitFail(INIT_FAIL_TEMP);
    initShow(3); delay(400);

    if (!pressSensorPlausible(readPressureVoltage())) enterInitFail(INIT_FAIL_PRESS);
    initShow(4); delay(400);

    flapServo1.write(FLAP_CLOSED); flapServo2.write(FLAP_CLOSED); delay(400);
    flapServo1.write(FLAP_OPEN);   flapServo2.write(FLAP_OPEN);   delay(400);
    flapServo1.write(FLAP_CLOSED); flapServo2.write(FLAP_CLOSED);

    // ===== INIT OK -> RUN =====
    playVoice(1);
    initShow(5); delay(400);
    gaugeBootAnimation();
}


// ========== HARDWARE SENSOR READS ==========

float readPinVoltage(uint8_t pin) {
    return analogRead(pin) * (VREF / ADC_MAX);
}

float readTemperatureResistance() {
    float voltage = readPinVoltage(PIN_TEMP_SENSE);
    if (voltage >= (VREF - 0.01f)) voltage = VREF - 0.01f; // prevent div by zero
    return (voltage * TEMP_SERIES_RESISTOR) / (VREF - voltage);
}

float readPressureVoltage() {
    return readPinVoltage(PIN_PRESS_SENSE) * PRESS_DIVIDER_RATIO;
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
// Play the situation's voice prompt (§7) once when it becomes active.
void onSituationChange(Situation sit) {
    Serial.print("Situation -> ");
    Serial.println(situationName(sit));
    playVoice(voiceTrack(sit));
}

// ----- DISPLAY -----

void updateDisplay(Situation sit, float temperature, float resistance, float pressure, float vPressure) {
    display.clearDisplay();
    display.setTextWrap(false);

    const bool tempErr  = resistance >= TEMP_MAX_SAFE_RESISTANCE;
    const bool pressErr = vPressure < PRESS_MIN_SAFE_VOLTAGE;

    // Yellow zone (y=0-15): situation label; alerts invert the strip for visibility.
    const bool isAlert = situationAlert(sit) != ALERT_OFF;
    if (isAlert) {
        display.fillRect(0, 0, 128, 16, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
    } else {
        display.setTextColor(SSD1306_WHITE);
    }
    display.setTextSize(1);
    display.setCursor(2, 4);
    switch (sit) {
        case SIT_STOP_ENGINE:      display.print("!! STOP ENGINE !!"); break;
        case SIT_TEMP_SENSOR_ERR:  display.print("TEMP SENSOR ERROR"); break;
        case SIT_PRESS_SENSOR_ERR: display.print("PRESS SENSOR ERROR"); break;
        case SIT_LOW_PRESSURE:     display.print("LOW OIL PRESSURE"); break;
        case SIT_OVERPRESSURE:     display.print("OVERPRESSURE"); break;
        case SIT_MILD_OVERHEAT:
            display.print("OIL HOT!  FLAP:");
            display.print(flapOpen ? "OPEN" : "CLSD");
            break;
        case SIT_REGULATION:
            display.print("REGULATING FLAP:");
            display.print(flapOpen ? "OPEN" : "CLSD");
            break;
        case SIT_NORMAL:
            display.print("NORMAL    FLAP:");
            display.print(flapOpen ? "OPEN" : "CLSD");
            break;
        case SIT_COLD:
            display.print("COLD      FLAP:");
            display.print(flapOpen ? "OPEN" : "CLSD");
            break;
    }
    display.setTextColor(SSD1306_WHITE);

    // Blue zone top (y=16-39): temperature, size-3 with liquid-fill gauge background.
    display.setTextSize(3);
    if (tempErr) {
        display.setCursor(37, 16);
        display.print("ERR");
    } else {
        int fillW = (int)(constrain((temperature - 40.0f) / (150.0f - 40.0f), 0.0f, 1.0f) * 128.0f);
        if (fillW > 0) display.fillRect(0, 16, fillW, 24, SSD1306_WHITE);
        int len = ((int)temperature >= 100) ? 5 : 4;
        display.setTextColor(SSD1306_INVERSE);
        display.setCursor((128 - len * 18) / 2, 16);
        display.print((int)temperature);
        display.write(247);
        display.print("C");
    }

    display.drawFastHLine(0, 40, 128, SSD1306_WHITE);

    // Blue zone bottom (y=41-63): pressure gauge.
    display.setTextSize(3);
    if (pressErr) {
        display.setCursor(37, 42);
        display.print("ERR");
    } else {
        int fillW = (int)(constrain(pressure / 8.0f, 0.0f, 1.0f) * 128.0f);
        if (fillW > 0) display.fillRect(0, 41, fillW, 23, SSD1306_WHITE);
        int len = (pressure >= 10.0f) ? 5 : 4;
        display.setTextColor(SSD1306_INVERSE);
        display.setCursor((128 - len * 18) / 2, 42);
        display.print(pressure, 1);
        display.print("b");
    }

    display.display();
}


// On an ACK rising edge: mute an in-progress voice prompt and give a feedback blip.
void onAckPress() {
    if (ackState.acked) stopVoice();
    tone(PIN_BUZZER, BUZZER_FREQ_HZ); delay(60); noTone(PIN_BUZZER);
    buzzerSounding = false;
}


// ========== MAIN LOOP ==========

void loop() {
    // ----- SENSOR Reads + conversions -----
    float resistance = readTemperatureResistance();
    float temperature = temperatureFromResistance(resistance);
    float vPressure = readPressureVoltage();
    float pressure = pressureFromVoltage(vPressure);

    // ----- DECISION (§3/§4) -----
    bool sensorsOk = (resistance < TEMP_MAX_SAFE_RESISTANCE) && (vPressure >= PRESS_MIN_SAFE_VOLTAGE);
    lowPressAlarm = sensorsOk ? nextLowPressAlarm(lowPressAlarm, temperature, pressure)
                              : false; // readings unreliable -> drop the adaptive alarm
    Situation sit = evaluateSituation(resistance, vPressure, temperature, pressure, lowPressAlarm);

    // ----- ACKNOWLEDGE (§6) -----
    bool ackNow = digitalRead(PIN_ACK_BUTTON);
    bool ackEdge = ackNow && !prevAckButton; // rising edge = touch
    prevAckButton = ackNow;
    ackState = updateAck(ackState, sit, ackEdge); // every loop: handles auto-return / re-arm
    if (ackEdge) onAckPress();

    // ----- OUTPUTS -----
    flapOpen = nextFlapState(sit, temperature, flapOpen);
    applyFlap(flapOpen);
    applyAlertLed(situationAlert(sit)); // LED stays on even when acknowledged
    applyBuzzer(sit, ackState.acked);   // ack mutes the buzzer
    if ((int)sit != prevSituation) { onSituationChange(sit); prevSituation = sit; }
    static uint32_t lastDisplayMs = 0;
    uint32_t nowMs = millis();
    if (oledOk && (nowMs - lastDisplayMs >= 500)) {
        lastDisplayMs = nowMs;
        OLED_I2C.begin(); // STM32F103 I2C BUSY-flag errata: re-init before each frame
        updateDisplay(sit, temperature, resistance, pressure, vPressure);
    }

    // Split the 200 ms wait into 20 ms slices so short ACK taps are never missed.
    for (uint8_t i = 0; i < 10; i++) {
        delay(20);
        bool ackNow = digitalRead(PIN_ACK_BUTTON);
        if (ackNow && !prevAckButton) {
            ackState = updateAck(ackState, sit, true);
            onAckPress();
        }
        prevAckButton = ackNow;
    }
}
