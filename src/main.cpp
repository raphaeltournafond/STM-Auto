#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "pins.h"   // pin map (single source of truth)

// ========== CONFIGURATION ==========

// /!\ Order of interpolation tables should be in INCREASING order

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

// ----- TEMPERATURE Sensor -----
const float TEMP_SERIES_RESISTOR = 220.0f;
// Threshold
const float TEMP_MAX_SAFE_RESISTANCE = 1500.0f; // If the sensor reads above this resistance, it is likely disconnected/failed/oil temp is very low.
// Flap hysteresis (DECISION_MATRIX §5): open at/above OPEN, close at/below CLOSE
const float FLAP_OPEN_TEMP  = 92.0f;
const float FLAP_CLOSE_TEMP = 85.0f;
// Sensor scale
const float resTable[]      =  {32.0,  41.0,  68.0,  120.0, 224.0, 438.0, 925.0}; // increasing mandatory here
const float tempTable[]     =  {150.0, 140.0, 120.0, 100.0, 80.0,  60.0,  40.0};
const uint8_t tempTableSize = sizeof(tempTable) / sizeof(tempTable[0]);

// ----- PRESSURE Sensor -----
const float PRESS_DIVIDER_RATIO = 1.5f; // 10/20 kOhm voltage divider
const float PRESS_MIN_SAFE_VOLTAGE = 0.3f; // under this value we can consider disconnected
// Sensor scale
const float vPressureTable[] = {0.5, 0.87, 1.25, 1.77, 2.22, 2.55, 2.79, 2.91, 3.01}; // increasing mandatory here
const float barTable[]       = {0.0, 1.0,  2.0,  3.5,  5.0,  6.5,  8.0,  9.0,  10.0};
const uint8_t pressTableSize = sizeof(vPressureTable) / sizeof(vPressureTable[0]);

// ----- FLAP Servos (twin, driven in tandem) -----
const uint8_t FLAP_CLOSED = 0;
const uint8_t FLAP_OPEN   = 90;
Servo flapServo1;
Servo flapServo2;
bool flapOpen = false; // current flap state (hysteresis), starts closed

// ----- DECISION MATRIX (DECISION_MATRIX §3) -----
// Situations evaluated by priority (top = highest); first true wins.
enum Situation {
    SIT_TEMP_SENSOR_ERR,  // 1  R > TEMP_MAX_SAFE_RESISTANCE
    SIT_PRESS_SENSOR_ERR, // 2  V < PRESS_MIN_SAFE_VOLTAGE
    SIT_STOP_ENGINE,      // 3  too hot AND too low pressure
    SIT_LOW_PRESSURE,     // 4  pressure below adaptive floor (§4)
    SIT_OVERPRESSURE,     // 5  pressure above ceiling
    SIT_MILD_OVERHEAT,    // 6  hot but pressure OK
    SIT_REGULATION,       // 7  warm, flap regulating
    SIT_NORMAL,           // 8  normal operating band
    SIT_COLD,             // 9  engine cold
};

// Alert level mapped to the indicator channel (LED colour).
enum AlertLevel { ALERT_OFF, ALERT_WARN, ALERT_ALARM }; // off / yellow / red

// Temperature band thresholds (°C)
const float TEMP_STOP_ENGINE = 122.0f; // combined with low pressure -> stop engine
const float TEMP_OVERHEAT    = 110.0f; // mild overheat band start
const float TEMP_REGULATION  = 90.0f;  // thermal-regulation band start
const float TEMP_NORMAL      = 70.0f;  // normal band start
// Pressure thresholds (bar)
const float PRESS_STOP_ENGINE = 1.2f;  // stop-engine pressure (with TEMP_STOP_ENGINE)
const float PRESS_OVER        = 6.5f;  // overpressure ceiling

// Adaptive low-pressure floor (§4), indexed by temperature band:
// 0:<70  1:70-89  2:90-110  3:110-122  4:>122
const float pressCritical[] = {1.5f, 0.9f, 0.8f, 0.6f, 1.2f}; // alarm below
const float pressRearm[]    = {1.8f, 1.1f, 1.0f, 0.8f, 1.2f}; // clear at/above
bool lowPressAlarm = false; // adaptive low-pressure alarm state (§4 hysteresis)
int prevSituation = -1;     // last dispatched situation (for edge-triggered sound/voice)


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
    
    // ----- OTHER Settings -----
    analogReadResolution(12);
}


// ========== MATH and HARDWARE HELPERS ==========

float readPinVoltage(uint8_t pin) {
    return analogRead(pin) * (VREF / ADC_MAX);
}

float interpolate(float x, const float* xTable, const float* yTable, uint8_t size) {
    if (x <= xTable[0]) return yTable[0];
    if (x >= xTable[size - 1]) return yTable[size - 1];

    for (uint8_t i = 0; i < size - 1; i++) {
        if (x >= xTable[i] && x <= xTable[i + 1]) {
            return yTable[i] + (x - xTable[i]) * (yTable[i + 1] - yTable[i]) / (xTable[i + 1] - xTable[i]);
        }
    }
    return yTable[size - 1]; // fallback
}

// ========== SENSOR Specific ==========

// ----- TEMPERATURE -----

float readTemperatureResistance() {
    float voltage = readPinVoltage(PIN_TEMP_SENSE);
    if (voltage >= (VREF - 0.01f)) voltage = VREF - 0.01f; // prevent div by zero
    return (voltage * TEMP_SERIES_RESISTOR) / (VREF - voltage);
}

float resistanceToTemperature(float resistance) {
    if (resistance >= TEMP_MAX_SAFE_RESISTANCE) {
        return tempTable[0]; // fallback to highest temp for safety
    }
    return interpolate(resistance, resTable, tempTable, tempTableSize);
}

// ----- PRESSURE -----

float readPressureVoltage() {
    return readPinVoltage(PIN_PRESS_SENSE) * PRESS_DIVIDER_RATIO;
}

// ========== DECISION MATRIX ==========

uint8_t temperatureBand(float t) {
    if (t < TEMP_NORMAL)       return 0; // < 70
    if (t < TEMP_REGULATION)   return 1; // 70-89
    if (t < TEMP_OVERHEAT)     return 2; // 90-110
    if (t <= TEMP_STOP_ENGINE) return 3; // 110-122
    return 4;                            // > 122
}

// Adaptive low-pressure alarm with per-band hysteresis (§4).
void updateLowPressAlarm(float temperature, float pressure) {
    uint8_t b = temperatureBand(temperature);
    if (!lowPressAlarm && pressure < pressCritical[b])      lowPressAlarm = true;
    else if (lowPressAlarm && pressure >= pressRearm[b])    lowPressAlarm = false;
}

// Priority-ordered evaluation: first true wins (§3).
Situation evaluateSituation(float resistance, float vPressure, float temperature, float pressure) {
    if (resistance >= TEMP_MAX_SAFE_RESISTANCE)                          return SIT_TEMP_SENSOR_ERR;
    if (vPressure < PRESS_MIN_SAFE_VOLTAGE)                              return SIT_PRESS_SENSOR_ERR;
    if (temperature > TEMP_STOP_ENGINE && pressure < PRESS_STOP_ENGINE) return SIT_STOP_ENGINE;
    if (lowPressAlarm)                                                  return SIT_LOW_PRESSURE;
    if (pressure > PRESS_OVER)                                          return SIT_OVERPRESSURE;
    if (temperature >= TEMP_OVERHEAT)                                   return SIT_MILD_OVERHEAT;
    if (temperature >= TEMP_REGULATION)                                 return SIT_REGULATION;
    if (temperature >= TEMP_NORMAL)                                     return SIT_NORMAL;
    return SIT_COLD;
}

AlertLevel situationAlert(Situation sit) {
    switch (sit) {
        case SIT_TEMP_SENSOR_ERR:
        case SIT_PRESS_SENSOR_ERR:
        case SIT_STOP_ENGINE:
        case SIT_LOW_PRESSURE:
        case SIT_OVERPRESSURE:   return ALERT_ALARM;
        case SIT_MILD_OVERHEAT:  return ALERT_WARN;
        default:                 return ALERT_OFF;
    }
}

const char* situationName(Situation sit) {
    switch (sit) {
        case SIT_TEMP_SENSOR_ERR:  return "TEMP SENS ERR";
        case SIT_PRESS_SENSOR_ERR: return "PRESS SENS ERR";
        case SIT_STOP_ENGINE:      return "STOP ENGINE";
        case SIT_LOW_PRESSURE:     return "LOW PRESSURE";
        case SIT_OVERPRESSURE:     return "OVERPRESSURE";
        case SIT_MILD_OVERHEAT:    return "MILD OVERHEAT";
        case SIT_REGULATION:       return "REGULATION";
        case SIT_NORMAL:           return "NORMAL";
        default:                   return "COLD";
    }
}

// ========== SITUATION OUTPUTS ==========

// ----- FLAP (servos) -----
// Safety situations force the flap open; overpressure leaves it unchanged
// (opening does not relieve pressure); normal bands use temperature hysteresis (§5).
void applyFlap(Situation sit, float temperature) {
    switch (sit) {
        case SIT_TEMP_SENSOR_ERR:
        case SIT_PRESS_SENSOR_ERR:
        case SIT_STOP_ENGINE:
        case SIT_LOW_PRESSURE:
        case SIT_MILD_OVERHEAT:
            flapOpen = true; // force open (favour cooling)
            break;
        case SIT_OVERPRESSURE:
            break;           // unchanged
        default:             // REGULATION / NORMAL / COLD
            if (!flapOpen && temperature >= FLAP_OPEN_TEMP)      flapOpen = true;
            else if (flapOpen && temperature <= FLAP_CLOSE_TEMP) flapOpen = false;
            break;
    }
    uint8_t angle = flapOpen ? FLAP_OPEN : FLAP_CLOSED;
    flapServo1.write(angle);
    flapServo2.write(angle);
}

// ----- LED -----
// Onboard PC13 (active LOW) used as the backup indicator: lit on any alert.
// TODO: drive WS2812B (PIN_WS2812_DATA) as the primary off/yellow/red indicator.
void applyAlertLed(AlertLevel level) {
    digitalWrite(PIN_STATUS_LED, level == ALERT_OFF ? HIGH : LOW);
}

// ----- SOUND / VOICE (edge-triggered) -----
// Buzzer (PIN_BUZZER) and MP3 voice (USART1) are wired but not yet driven.
// TODO: implement buzzer patterns (§3) and voice files (§7); for now log only.
void onSituationChange(Situation sit) {
    Serial.print("Situation -> ");
    Serial.println(situationName(sit));
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
    // ----- TEMPERATURE Computation -----
    float resistance = readTemperatureResistance();
    float temperature = resistanceToTemperature(resistance);

    // ----- PRESSURE Computation -----
    float vPressure = readPressureVoltage();
    float pressure = interpolate(vPressure, vPressureTable, barTable, pressTableSize);

    // ----- DECISION (§3) -----
    bool sensorsOk = (resistance < TEMP_MAX_SAFE_RESISTANCE) && (vPressure >= PRESS_MIN_SAFE_VOLTAGE);
    if (sensorsOk) updateLowPressAlarm(temperature, pressure);
    else           lowPressAlarm = false; // readings unreliable -> drop the adaptive alarm
    Situation sit = evaluateSituation(resistance, vPressure, temperature, pressure);

    // ----- OUTPUTS -----
    applyFlap(sit, temperature);
    applyAlertLed(situationAlert(sit));
    if ((int)sit != prevSituation) { onSituationChange(sit); prevSituation = sit; }
    updateDisplay(sit, temperature, resistance, pressure, vPressure);

    delay(200);
}
