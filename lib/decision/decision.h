#pragma once
#include <stdint.h>

// Hardware-independent decision logic for the SWEE.BRZ oil monitor.
// Pure functions only (no Arduino) so they can be unit-tested natively.
// See DECISION_MATRIX.md (§3 situations, §4 pressure floor, §5 hysteresis).

// Situations by priority (top = highest); first true wins (§3).
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

// ----- Thresholds -----
constexpr float TEMP_MAX_SAFE_RESISTANCE    = 1500.0f;  // above -> temp sensor fault
constexpr float PRESS_MIN_SAFE_VOLTAGE      = 0.3f;     // below -> pressure sensor fault
constexpr float FLAP_OPEN_TEMP              = 92.0f;    // flap hysteresis open (§5)
constexpr float FLAP_CLOSE_TEMP             = 85.0f;    // flap hysteresis close (§5)
constexpr float TEMP_STOP_ENGINE            = 122.0f;   // with low pressure -> stop engine
constexpr float TEMP_OVERHEAT               = 110.0f;   // mild overheat band start
constexpr float TEMP_REGULATION             = 90.0f;    // thermal-regulation band start
constexpr float TEMP_NORMAL                 = 70.0f;    // normal band start
constexpr float PRESS_STOP_ENGINE           = 1.2f;     // stop-engine pressure
constexpr float PRESS_OVER                  = 9.5f;     // overpressure ceiling

// ----- Pure helpers -----

// Piecewise-linear lookup; xTable must be strictly increasing, same size as yTable.
float interpolate(float x, const float* xTable, const float* yTable, uint8_t size);

// Sensor conversions (use the built-in scale tables).
float temperatureFromResistance(float resistance); // clamps to max-temp on fault
float pressureFromVoltage(float vPressure);

// Temperature band index: 0:<70  1:70-89  2:90-110  3:110-122  4:>122
uint8_t temperatureBand(float temperature);

// Adaptive low-pressure alarm with per-band hysteresis (§4); returns next state.
bool nextLowPressAlarm(bool prev, float temperature, float pressure);

// Priority-ordered situation evaluation (§3). lowPressAlarm is the §4 state.
Situation evaluateSituation(float resistance, float vPressure, float temperature,
                            float pressure, bool lowPressAlarm);

// Next flap state: safety situations force open, overpressure holds, normal bands
// use temperature hysteresis (§5); returns true = open.
bool nextFlapState(Situation sit, float temperature, bool prevFlapOpen);

AlertLevel situationAlert(Situation sit);
const char* situationName(Situation sit);
