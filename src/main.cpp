#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ========== CONFIGURATION ==========

// /!\ Order of interpolation tables should be in INCREASING order 

// ----- BUILT IN LED -----
const uint8_t PIN_STATUSLED = LED_BUILTIN;

// ----- OLED Screen -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1 // Default to -1 to share the board reset pin
#define SCREEN_ADDRESS 0x3C // Default to 0x3C for standard I2C adress
#define OLED_SDA_PIN PB11
#define OLED_SCL_PIN PB10

TwoWire OLED_I2C(OLED_SDA_PIN, OLED_SCL_PIN); 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &OLED_I2C, OLED_RESET);

const float TEMP_ALERTE    = 130.0;
const int   FLASH_INTERVAL = 400;   // ms entre chaque inversion

bool  alerteActive   = false;
bool  invertedMode   = false;
unsigned long lastFlash = 0;

// ----- HARDWARE Constants -----
const float VREF = 3.3f;
const uint16_t ADC_MAX = 4095;

// ----- TEMPERATURE Sensor -----
const uint8_t TEMP_SENSOR_PIN  = PA1;
const float TEMP_SERIES_RESISTOR = 220.0f;
// Threshold
const float TEMP_MAX_SAFE_RESISTANCE = 1500.0f; // If the sensor reads above this resistance, it is likely disconnected/failed/oil temp is very low.
const float TEMPERATURE_THRESHOLD = 90.0f; // Adjust according to actual measurement
// Sensor scale
const float resTable[]      =  {32.0,  41.0,  68.0,  120.0, 224.0, 438.0, 925.0}; // increasing mandatory here
const float tempTable[]     =  {150.0, 140.0, 120.0, 100.0, 80.0,  60.0,  40.0};
const uint8_t tempTableSize = sizeof(tempTable) / sizeof(tempTable[0]);

// ----- PRESSURE Sensor -----
const uint8_t PRESS_SENSOR_PIN = PA0;
const float PRESS_DIVIDER_RATIO = 1.5f; // 10/20 kOhm voltage divider
const float PRESS_MIN_SAFE_VOLTAGE = 0.3f; // under this value we can consider disconnected
// Sensor scale
const float vPressureTable[] = {0.5, 0.87, 1.25, 1.77, 2.22, 2.55, 2.79, 2.91, 3.01}; // increasing mandatory here
const float barTable[]       = {0.0, 1.0,  2.0,  3.5,  5.0,  6.5,  8.0,  9.0,  10.0};
const uint8_t pressTableSize = sizeof(vPressureTable) / sizeof(vPressureTable[0]);

// ----- FLAP Servos -----
const uint8_t RED_SERVO_PIN = PA6;
const uint8_t RED_SERVO_CLOSED = 0;
const uint8_t RED_SERVO_OPEN   = 90;
Servo redServo;


// ========== SETUP ==========

void blinkErrorLED() {
    while (true) {
        digitalWrite(PIN_STATUSLED, HIGH); delay(250);
        digitalWrite(PIN_STATUSLED, LOW);  delay(250);
    }
}

void setup() {
    Serial.begin(115200);

    //Doublon dans loop (peut etre supprimer celui la jsp) 
    // ----- TEMPERATURE Computation -----
    float resistance = readTemperatureResistance();
    float temperature = resistanceToTemperature(resistance);

    // ----- PRESSURE Computation -----
    float vPressure = readPressureVoltage();
    float pressure = interpolate(vPressure, vPressureTable, barTable, pressTableSize);

    delay(500); // important to let the screen initialize
    
    // ----- Built in initialization -----
    pinMode(PIN_STATUSLED, OUTPUT);
    digitalWrite(PIN_STATUSLED, HIGH); // High is off for the built-in LED

    // ----- OLED Screen initialization -----
    OLED_I2C.begin(); // I2C at 400kHz by default (smoother display)
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) blinkErrorLED();
    alerteActive = (temperature > TEMP_ALERTE);

    display.clearDisplay();

    // Zone scroll (pages 0-1 = 16 premiers pixels)
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 4);
    display.print(F("SWEE.BRZ OIL MONITOR"));
    display.display();
    display.startscrollright(0x00, 0x01);

    drawStaticZone(temperature, pressure);

    // ----- SERVO initialization -----
    redServo.attach(RED_SERVO_PIN);
    redServo.write(RED_SERVO_CLOSED);

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
    float voltage = readPinVoltage(TEMP_SENSOR_PIN);
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
    return readPinVoltage(PRESS_SENSOR_PIN) * PRESS_DIVIDER_RATIO;
}

// ========== SENSOR FEEDBACK ==========

// ----- SERVOS -----

void updateServo(float temperature) {
    redServo.write((temperature >= TEMPERATURE_THRESHOLD) ? RED_SERVO_OPEN : RED_SERVO_CLOSED);
}

// ----- DISPLAY -----

void updateDisplay(float temperature, float resistance, float pressure, float vPressure) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

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

    // --- FLAP Status ---
    display.setCursor(0, 56);
    display.print("FLAP: ");
    display.println((temperature >= TEMPERATURE_THRESHOLD) ? "OPEN" : "CLOSED");

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

    // ----- FLAP Update -----
    updateServo(temperature);

    // ----- DISPLAY Update -----
    drawStaticZone(temperature, pressure);

    delay(200);

    alerteActive = (temperature > TEMP_ALERTE);

    if (alerteActive) {
        unsigned long now = millis();
        if (now - lastFlash >= FLASH_INTERVAL) {
        lastFlash    = now;
        invertedMode = !invertedMode;
        // Inversion matérielle : ne touche pas au framebuffer ni au scroll
        display.invertDisplay(invertedMode);
        }
    } else {
        if (invertedMode) {
        invertedMode = false;
        display.invertDisplay(false); // retour à l'affichage normal
        }
    }
}

// ─────────────────────────────────────────────
void drawStaticZone(float temperature, float pressure) {
  // Efface uniquement la zone basse
  display.fillRect(0, 16, SCREEN_WIDTH, SCREEN_HEIGHT - 16, SSD1306_BLACK);

  // Séparateur
  display.drawLine(0, 17, SCREEN_WIDTH - 1, 17, SSD1306_WHITE);

  // --- Température ---
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 22);
  display.print(F("Temp huile"));

  if (alerteActive) {
    display.setCursor(74, 22);
    display.print(F("! ALERTE !"));
  }

  display.setTextSize(2);
  display.setCursor(0, 33);
  display.print(temperature, 1);
  display.print(F(" C"));

  // --- Pression ---
  display.setTextSize(1);
  display.setCursor(0, 52);
  display.print(F("Pression : "));
  display.print(pressure, 1);
  display.print(F(" bar"));

  display.display();
}