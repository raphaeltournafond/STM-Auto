#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>


// ========== CONFIGURATION ==========

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

// ----- TEMPERATURE Sensor -----
const uint8_t TEMP_SENSOR_PIN  = PA1;
const float TEMP_SERIES_RESISTOR = 220.0f;
// Calibration
const float tempTable[] =  {40, 60, 80, 100, 120, 140, 150};
const float resTable[]  =  {925,438,224,120, 68,  41,  32};
const uint8_t tableSize = sizeof(tempTable) / sizeof(tempTable[0]);
// Threshold
const float SENSOR_MAX_SAFE_RESISTANCE = 1500.0f; // If the sensor reads above this resistance, it is likely disconnected/failed/oil temp is very low.
const float TEMPERATURE_THRESHOLD = 90.0f; // Adjust according to actual measurement

// ----- FLAP Servos -----
const uint8_t RED_SERVO_PIN = PA6;
const uint8_t RED_SERVO_CLOSED = 0;
const uint8_t RED_SERVO_OPEN   = 90;
Servo redServo;

// ----- HARDWARE Constants -----
const float VREF = 3.3f;
const uint16_t ADC_MAX = 4095;


// ========== SETUP ==========

void blinkErrorLED() {
    while (true) {
        digitalWrite(PIN_STATUSLED, HIGH);
        delay(250);
        digitalWrite(PIN_STATUSLED, LOW);
        delay(250);
    }
}

void setup() {
    Serial.begin(115200);

    delay(500); // important to let the screen initialize
    
    // ----- Built in initialization -----
    pinMode(PIN_STATUSLED, OUTPUT);
    digitalWrite(PIN_STATUSLED, HIGH); // High is off for the built-in LED

    // ----- OLED Screen initialization -----
    OLED_I2C.begin(); // I2C at 400kHz by default (smoother display)
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
        blinkErrorLED();
    }

    display.clearDisplay();
    display.display();

    // ----- SERVO initialization -----
    redServo.attach(RED_SERVO_PIN);
    redServo.write(RED_SERVO_CLOSED);
    
    // ----- OTHER Settings -----
    analogReadResolution(12);
}


// ========== HELPER Functions ==========

float readTemperatureResistance() {
    uint16_t adcValue = analogRead(TEMP_SENSOR_PIN);

    float voltage = adcValue * (VREF / ADC_MAX);

    // Prevent divide-by-zero if disconnected
    if (voltage >= (VREF - 0.01f)) {
        voltage = VREF - 0.01f;
    }

    float resistance = (voltage * TEMP_SERIES_RESISTOR) / (VREF - voltage);

    return resistance;
}

float resistanceToTemperature(float resistance) {
    if (resistance >= SENSOR_MAX_SAFE_RESISTANCE) {
        return tempTable[tableSize - 1]; // likely disconnected or very low temp, return highest temp to open flap for safety
    }

    // Clamp outside range
    if (resistance >= resTable[0]) return tempTable[0];
    if (resistance <= resTable[tableSize - 1]) return tempTable[tableSize - 1];

    for (uint8_t i = 0; i < tableSize - 1; i++) {
        if (resistance <= resTable[i] && resistance >= resTable[i + 1]) {

            float r1 = resTable[i];
            float r2 = resTable[i + 1];
            float t1 = tempTable[i];
            float t2 = tempTable[i + 1];

            // Linear interpolation
            float ratio = (resistance - r1) / (r2 - r1);
            return t1 + ratio * (t2 - t1);
        }
    }

    return tempTable[tableSize - 1]; // fallback to highest temp for safety
}

void updateServo(float temperature) {
    if (temperature >= TEMPERATURE_THRESHOLD) {
        redServo.write(RED_SERVO_OPEN);
    } else {
        redServo.write(RED_SERVO_CLOSED);
    }
}

void updateDisplay(float temperature, float resistance) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // ----- TITLE -----
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("SWEE.BRZ OIL MONITOR");

    // ----- TEMPERATURE -----
    display.setCursor(0, 16);
    if (resistance >= SENSOR_MAX_SAFE_RESISTANCE) {
        display.println("TEMP ERR CHECK WIRING");
    } else {
        display.print("T:");
        display.print((int)temperature);
        display.write(247); 
        display.println("C");
        display.setCursor(0, 28);
        display.print("R:");
        display.print((int)resistance);
        display.println(" Ohm");
    }

    // ----- FLAP Status -----
    display.setCursor(0, 52);
    if (temperature >= TEMPERATURE_THRESHOLD) {
        display.println("FLAP: OPEN (HOT)");
    } else {
        display.println("FLAP: CLOSED");
    }

    display.display();
}

void loop() {
    // ----- TEMPERATURE Calculation -----
    float resistance = readTemperatureResistance();
    float temperature = resistanceToTemperature(resistance);

    // ----- FLAP Update -----
    updateServo(temperature);

    // ----- DISPLAY Update -----
    updateDisplay(temperature, resistance);

    delay(500);
}
