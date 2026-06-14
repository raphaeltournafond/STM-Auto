#include "decision.h"

// ----- Sensor scale tables (x must stay strictly increasing) -----
static const float resTable[]  = {32.0, 41.0, 68.0, 120.0, 224.0, 438.0, 925.0};
static const float tempTable[] = {150.0, 140.0, 120.0, 100.0, 80.0, 60.0, 40.0};
static const uint8_t tempTableSize = sizeof(tempTable) / sizeof(tempTable[0]);

static const float vPressureTable[] = {0.5, 0.87, 1.25, 1.77, 2.22, 2.55, 2.79, 2.91, 3.01};
static const float barTable[]       = {0.0, 1.0,  2.0,  3.5,  5.0,  6.5,  8.0,  9.0,  10.0};
static const uint8_t pressTableSize = sizeof(vPressureTable) / sizeof(vPressureTable[0]);

// ----- Adaptive low-pressure floor (§4), indexed by temperature band 0..4 -----
static const float pressCritical[] = {1.5f, 0.9f, 0.8f, 0.6f, 1.2f}; // alarm below
static const float pressRearm[]    = {1.8f, 1.1f, 1.0f, 0.8f, 1.2f}; // clear at/above

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

float temperatureFromResistance(float resistance) {
    if (resistance >= TEMP_MAX_SAFE_RESISTANCE) return tempTable[0]; // max temp for safety
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

bool nextLowPressAlarm(bool prev, float temperature, float pressure) {
    uint8_t b = temperatureBand(temperature);
    if (!prev && pressure < pressCritical[b])  return true;
    if (prev && pressure >= pressRearm[b])      return false;
    return prev;
}

Situation evaluateSituation(float resistance, float vPressure, float temperature,
                            float pressure, bool lowPressAlarm) {
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
