#pragma once
#include <stdint.h>

// Hardware-independent decision logic for the SWEE.BRZ oil monitor.
// Pure functions only (no Arduino) so they can be unit-tested natively.
// See DECISION_MATRIX.md (§3 situations, §4 pressure floor, §5 hysteresis).

// Situations by priority (top = highest); first true wins (§3).
enum Situation {
    SIT_TEMP_SENSOR_ERR,  // 1  R outside [TEMP_MIN_SAFE_RESISTANCE, TEMP_MAX_SAFE_RESISTANCE)
    SIT_PRESS_SENSOR_ERR, // 2  V outside [PRESS_MIN_SAFE_VOLTAGE, PRESS_MAX_SAFE_VOLTAGE]
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
// Temp sensor fault window on the raw resistance. Must stay wide enough for a
// cold-soak start: the NTC reads ~18 kΩ at -20 °C, open circuit reads ~72 kΩ,
// and the scale table bottoms out at 925 Ω = 40 °C (colder reads as 40 °C).
constexpr float TEMP_MAX_SAFE_RESISTANCE    = 25000.0f; // at/above -> open / disconnected
constexpr float TEMP_MIN_SAFE_RESISTANCE    = 10.0f;    // below -> shorted to GND (150 °C = 32 Ω)
constexpr float PRESS_MIN_SAFE_VOLTAGE      = 0.3f;     // below -> pressure sensor fault (disconnected)
// Above -> signal shorted to the 5 V rail (or sensor GND lost): the ADC pin then
// saturates at 3.3 V = 4.95 V sensor-side. The scale table ends at 3.01 V = 10 bar.
constexpr float PRESS_MAX_SAFE_VOLTAGE      = 4.8f;
constexpr float FLAP_OPEN_TEMP              = 92.0f;    // flap hysteresis open (§5)
constexpr float FLAP_CLOSE_TEMP             = 85.0f;    // flap hysteresis close (§5)
constexpr float TEMP_STOP_ENGINE            = 122.0f;   // with low pressure -> stop engine
constexpr float TEMP_OVERHEAT               = 110.0f;   // mild overheat band start
constexpr float TEMP_REGULATION             = 90.0f;    // thermal-regulation band start
constexpr float TEMP_NORMAL                 = 70.0f;    // normal band start
constexpr float PRESS_STOP_ENGINE           = 1.2f;     // stop-engine pressure
constexpr float PRESS_OVER                  = 9.5f;     // overpressure ceiling

// ----- Alert hysteresis (§5): enter at the threshold, clear only past these -----
constexpr float TEMP_OVERHEAT_CLEAR         = 108.0f;   // mild overheat clears below
constexpr float PRESS_OVER_CLEAR            = 9.2f;     // overpressure clears at/below

// A new situation must be seen on this many consecutive loop ticks (~200 ms
// each) before it replaces the active one, so sensor noise cannot flicker
// alerts (voice replays, LED toggles, lost acknowledgements).
constexpr uint8_t SITUATION_CONFIRM_TICKS = 3;

// Latched alert states with hysteresis, evolved once per loop tick.
struct AlarmLatches {
    bool lowPress;     // adaptive low-pressure floor (§4)
    bool overheat;     // mild overheat band (110 °C, clears < 108 °C)
    bool overpressure; // overpressure (> 9.5 bar, clears <= 9.2 bar)
};

// Debounce state for the active situation (see SITUATION_CONFIRM_TICKS).
struct SituationFilter {
    bool primed;          // false until the first tick (first reading is taken as-is)
    Situation stable;     // confirmed situation, drives all outputs
    Situation candidate;  // raw situation currently being confirmed
    uint8_t count;        // consecutive ticks the candidate has been seen
};

// ----- Pure helpers -----

// Raw sensor fault checks (tested on the raw Ω / V before conversion, §3).
bool tempSensorFault(float resistance);  // shorted or open
bool pressSensorFault(float vPressure);  // disconnected or shorted to 5 V

// Piecewise-linear lookup; xTable must be strictly increasing, same size as yTable.
float interpolate(float x, const float* xTable, const float* yTable, uint8_t size);

// Sensor conversions (use the built-in scale tables).
float temperatureFromResistance(float resistance); // clamps to max-temp on fault
float pressureFromVoltage(float vPressure);

// Temperature band index: 0:<70  1:70-89  2:90-110  3:110-122  4:>122
uint8_t temperatureBand(float temperature);

// Adaptive low-pressure alarm with per-band hysteresis (§4); returns next state.
bool nextLowPressAlarm(bool prev, float temperature, float pressure);

// Mild-overheat / overpressure latches with hysteresis (§5); return next state.
bool nextOverheat(bool prev, float temperature);
bool nextOverpressure(bool prev, float pressure);

// Evolve all latches for one tick. When a sensor is faulty its readings are
// unreliable, so every latch drops (the sensor-error situation takes over).
AlarmLatches nextAlarmLatches(AlarmLatches prev, bool sensorsOk,
                              float temperature, float pressure);

// Priority-ordered situation evaluation (§3), using the latched alert states.
Situation evaluateSituation(float resistance, float vPressure, float temperature,
                            float pressure, AlarmLatches latches);

// Every alert situation (situationAlert != ALERT_OFF) currently true, as a bit
// mask of (1u << Situation). evaluateSituation() keeps only the highest one; the
// display uses the full mask to alternate between simultaneous alarms (§6).
uint16_t activeAlertMask(float resistance, float vPressure, float temperature,
                         float pressure, AlarmLatches latches);

// Debounce the raw situation: returns the next filter state; use .stable.
SituationFilter nextSituationFilter(SituationFilter prev, Situation raw, uint8_t confirmTicks);

// Next flap state: safety situations force open, overpressure holds, normal bands
// use temperature hysteresis (§5); returns true = open.
bool nextFlapState(Situation sit, float temperature, bool prevFlapOpen);

// Situation shown on the OLED status line. Between 70 and 110 °C the flap follows
// its own 92/85 °C hysteresis, so the REGULATION/NORMAL label follows the actual
// flap state (REGULATING = flap open) instead of the 90 °C band edge. Display only:
// decisions, LED, sound and pressure floors keep using `sit`.
Situation displaySituation(Situation sit, bool flapOpen);

AlertLevel situationAlert(Situation sit);
const char* situationName(Situation sit);
