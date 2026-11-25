#include "decision.h"

// ----- Sensor scale tables (x must stay strictly increasing) -----
static const float resTable[]  = {32.0, 41.0, 68.0, 120.0, 224.0, 438.0, 925.0};
static const float tempTable[] = {150.0, 140.0, 120.0, 100.0, 80.0, 60.0, 40.0};
static const uint8_t tempTableSize = sizeof(tempTable) / sizeof(tempTable[0]);

static const float vPressureTable[] = {0.5, 0.87, 1.25, 1.77, 2.22, 2.55, 2.79, 2.91, 3.01};
static const float barTable[]       = {0.0, 1.0,  2.0,  3.5,  5.0,  6.5,  8.0,  9.0,  10.0};
static const uint8_t pressTableSize = sizeof(vPressureTable) / sizeof(vPressureTable[0]);

// ----- Adaptive low-pressure floor (§4), indexed by temperature band 0..4 -----
// Vehicle-calibrated floors: regenerate with scripts/analyze_log.py from logged
// drives (README "Calibration"). Band 4 (> 122 °C) is the stop-engine pairing,
// left as-is. Never below PRESS_*_MIN (decision.h), enforced below.
static constexpr float pressCritical[] = {1.50f, 0.90f, 0.80f, 0.60f, 1.20f}; // alarm below
static constexpr float pressRearm[]    = {1.80f, 1.10f, 1.00f, 0.80f, 1.20f}; // clear at/above

static_assert(sizeof(pressCritical) == sizeof(PRESS_CRITICAL_MIN) &&
              sizeof(pressRearm) == sizeof(PRESS_REARM_MIN), "one floor per temperature band");

// Each band: calibrated floor >= generic minimum, and rearm >= critical.
constexpr bool floorsValid(int b) {
    return b >= 5 || (pressCritical[b] >= PRESS_CRITICAL_MIN[b] &&
                      pressRearm[b] >= PRESS_REARM_MIN[b] &&
                      pressRearm[b] >= pressCritical[b] && floorsValid(b + 1));
}
static_assert(floorsValid(0), "calibrated low-pressure floors must never go below "
                              "PRESS_CRITICAL_MIN / PRESS_REARM_MIN, and rearm >= critical");

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

bool tempSensorFault(float resistance) {
    return resistance < TEMP_MIN_SAFE_RESISTANCE || resistance >= TEMP_MAX_SAFE_RESISTANCE;
}

bool pressSensorFault(float vPressure) {
    return vPressure < PRESS_MIN_SAFE_VOLTAGE || vPressure > PRESS_MAX_SAFE_VOLTAGE;
}

float temperatureFromResistance(float resistance) {
    if (tempSensorFault(resistance)) return tempTable[0]; // max temp for safety
    return interpolate(resistance, resTable, tempTable, tempTableSize);
}

float pressureFromVoltage(float vPressure) {
    return interpolate(vPressure, vPressureTable, barTable, pressTableSize);
}

uint8_t temperatureBand(float temperature) {
    if (temperature < TEMP_NORMAL)       return 0; // < 70
    if (temperature < TEMP_REGULATION)   return 1; // 70-89
    if (temperature < TEMP_OVERHEAT)     return 2; // 90-110
    if (temperature <= TEMP_STOP_ENGINE) return 3; // 110-122
    return 4;                                      // > 122
}

float lowPressCritical(float temperature) {
    return pressCritical[temperatureBand(temperature)];
}

float lowPressRearm(float temperature) {
    return pressRearm[temperatureBand(temperature)];
}

bool nextLowPressAlarm(bool prev, float temperature, float pressure) {
    uint8_t b = temperatureBand(temperature);
    if (!prev && pressure < pressCritical[b])  return true;
    if (prev && pressure >= pressRearm[b])      return false;
    return prev;
}

bool nextOverheat(bool prev, float temperature) {
    if (!prev && temperature >= TEMP_OVERHEAT)      return true;
    if (prev && temperature < TEMP_OVERHEAT_CLEAR)  return false;
    return prev;
}

bool nextOverpressure(bool prev, float pressure) {
    if (!prev && pressure > PRESS_OVER)        return true;
    if (prev && pressure <= PRESS_OVER_CLEAR)  return false;
    return prev;
}

AlarmLatches nextAlarmLatches(AlarmLatches prev, bool sensorsOk,
                              float temperature, float pressure) {
    if (!sensorsOk) return {false, false, false};
    return {
        nextLowPressAlarm(prev.lowPress, temperature, pressure),
        nextOverheat(prev.overheat, temperature),
        nextOverpressure(prev.overpressure, pressure),
    };
}

Situation evaluateSituation(float resistance, float vPressure, float temperature,
                            float pressure, AlarmLatches latches) {
    if (tempSensorFault(resistance))                                    return SIT_TEMP_SENSOR_ERR;
    if (pressSensorFault(vPressure))                                    return SIT_PRESS_SENSOR_ERR;
    if (temperature > TEMP_STOP_ENGINE && pressure < PRESS_STOP_ENGINE) return SIT_STOP_ENGINE;
    if (latches.lowPress)                                               return SIT_LOW_PRESSURE;
    if (latches.overpressure)                                           return SIT_OVERPRESSURE;
    if (latches.overheat)                                               return SIT_MILD_OVERHEAT;
    if (temperature >= TEMP_REGULATION)                                 return SIT_REGULATION;
    if (temperature >= TEMP_NORMAL)                                     return SIT_NORMAL;
    return SIT_COLD;
}

uint16_t activeAlertMask(float resistance, float vPressure, float temperature,
                         float pressure, AlarmLatches latches) {
    const bool tErr = tempSensorFault(resistance);
    const bool pErr = pressSensorFault(vPressure);
    uint16_t m = 0;
    if (tErr) m |= 1u << SIT_TEMP_SENSOR_ERR;
    if (pErr) m |= 1u << SIT_PRESS_SENSOR_ERR;
    if (tErr || pErr) return m; // readings unreliable: nothing else is judgeable
    if (temperature > TEMP_STOP_ENGINE && pressure < PRESS_STOP_ENGINE) m |= 1u << SIT_STOP_ENGINE;
    if (latches.lowPress)     m |= 1u << SIT_LOW_PRESSURE;
    if (latches.overpressure) m |= 1u << SIT_OVERPRESSURE;
    if (latches.overheat)     m |= 1u << SIT_MILD_OVERHEAT;
    return m;
}

SituationFilter nextSituationFilter(SituationFilter prev, Situation raw, uint8_t confirmTicks) {
    SituationFilter f = prev;
    if (!f.primed) return {true, raw, raw, 0};  // first tick: take the reading as-is
    if (raw == f.stable) {                      // back to (or still in) the active one
        f.candidate = raw;
        f.count = 0;
        return f;
    }
    if (raw != f.candidate) {                   // a different change starts over
        f.candidate = raw;
        f.count = 1;
    } else if (f.count < 255) {
        f.count++;
    }
    if (f.count >= confirmTicks) {              // confirmed -> becomes active
        f.stable = raw;
        f.count = 0;
    }
    return f;
}

bool nextFlapState(Situation sit, float temperature, bool prevFlapOpen) {
    switch (sit) {
        case SIT_TEMP_SENSOR_ERR:
        case SIT_PRESS_SENSOR_ERR:
        case SIT_STOP_ENGINE:
        case SIT_LOW_PRESSURE:
        case SIT_MILD_OVERHEAT:
            return true;            // force open (favour cooling)
        case SIT_OVERPRESSURE:
            return prevFlapOpen;    // unchanged
        default:                    // REGULATION / NORMAL / COLD -> hysteresis
            if (!prevFlapOpen && temperature >= FLAP_OPEN_TEMP) return true;
            if (prevFlapOpen && temperature <= FLAP_CLOSE_TEMP) return false;
            return prevFlapOpen;
    }
}

Situation displaySituation(Situation sit, bool flapOpen) {
    if (sit == SIT_REGULATION || sit == SIT_NORMAL) {
        return flapOpen ? SIT_REGULATION : SIT_NORMAL;
    }
    return sit;
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
