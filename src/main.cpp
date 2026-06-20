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


// ========== SETUP ==========

void blinkErrorLED() {
    while (true) {
        digitalWrite(PIN_STATUS_LED, HIGH); delay(250);
        digitalWrite(PIN_STATUS_LED, LOW);  delay(250);
    }
}

void setup() {
    Serial.begin(115200);

    delay(500); // important to let the screen initialize

    // ----- Built in initialization -----
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, HIGH); // High is off for the built-in LED
    pinMode(PIN_ACK_BUTTON, INPUT);     // touch module drives the line (no pull)

    // ----- OLED Screen initialization -----
    OLED_I2C.begin(); // I2C at 400kHz by default (smoother display)
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) blinkErrorLED();

    display.clearDisplay();
    display.display();

    // ----- SERVO initialization -----
    flapServo1.attach(PIN_SERVO_FLAP_1);
    flapServo2.attach(PIN_SERVO_FLAP_2);
    flapServo1.write(FLAP_CLOSED);
    flapServo2.write(FLAP_CLOSED);

    // ----- WS2812B initialization -----
    WS2812_SPI.begin();
    ws2812ShowAlert(ALERT_OFF); // start dark

    // ----- MP3 initialization -----
    MP3Serial.begin(9600);            // DFPlayer Mini default baud
    dfplayerSend(DF_CMD_SET_VOLUME, MP3_VOLUME); // best-effort (module may still be booting)

    // ----- OTHER Settings -----
    analogReadResolution(12);
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
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("SWEE.BRZ OIL MONITOR");

    // --- TEMPERATURE ---
    display.setCursor(0, 16);
    if (resistance >= TEMP_MAX_SAFE_RESISTANCE) {
        display.println("TEMPERATURE ERROR!");
        display.setCursor(0, 26);
        display.println("-> check wiring");
    } else {
        display.print("T:"); display.print((int)temperature); display.write(247); display.println("C");
        display.setCursor(0, 26);
        display.print("R:"); display.print((int)resistance); display.println(" Ohm");
    }

    // --- PRESSURE ---
    display.setCursor(0, 36);
    if (vPressure < PRESS_MIN_SAFE_VOLTAGE) {
        display.println("PRESSURE ERROR!");
        display.setCursor(0, 46);
        display.println("-> check wiring");
    } else {
        display.print("PRES:"); display.print(pressure, 1); display.println(" BAR");
        display.setCursor(0, 46);
        display.print("V:"); display.print(vPressure, 2); display.println("v");
    }

    // --- STATUS banner (active situation; sensor errors already shown above) ---
    display.setCursor(0, 56);
    switch (sit) {
        case SIT_STOP_ENGINE:
            display.println("STOP ENGINE!");
            break;
        case SIT_LOW_PRESSURE:
            display.print("LOW PRESS! "); display.print(pressure, 1); display.println("b");
            break;
        case SIT_OVERPRESSURE:
            display.print("OVER PRESS "); display.print(pressure, 1); display.println("b");
            break;
        case SIT_MILD_OVERHEAT:
            display.println("TEMP HIGH!");
            break;
        default: // sensor errors + REGULATION / NORMAL / COLD
            display.print("FLAP: ");
            display.println(flapOpen ? "OPEN" : "CLOSED");
            break;
    }

    display.display();
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
    ackState = updateAck(ackState, sit, ackEdge);
    if (ackEdge && ackState.acked) stopVoice(); // ack also silences an in-progress prompt

    // ----- OUTPUTS -----
    flapOpen = nextFlapState(sit, temperature, flapOpen);
    applyFlap(flapOpen);
    applyAlertLed(situationAlert(sit)); // LED stays on even when acknowledged
    applyBuzzer(sit, ackState.acked);   // ack mutes the buzzer
    if ((int)sit != prevSituation) { onSituationChange(sit); prevSituation = sit; }
    updateDisplay(sit, temperature, resistance, pressure, vPressure);

    delay(200);
}
