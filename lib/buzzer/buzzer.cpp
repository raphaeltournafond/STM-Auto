#include "buzzer.h"

// Envelope timings (ms). Kept as multiples of the ~200 ms main loop so the
// coarse sampling rate reproduces them cleanly.
static const uint32_t INTERMITTENT_ON     = 400;
static const uint32_t INTERMITTENT_PERIOD = 800;  // 400 on / 400 off
static const uint32_t LONG_ON             = 800;
static const uint32_t LONG_PERIOD         = 1200; // 800 on / 400 off

BuzzerPattern buzzerPattern(Situation sit) {
    switch (sit) {
        case SIT_STOP_ENGINE:
        case SIT_LOW_PRESSURE:
        case SIT_OVERPRESSURE:     return BUZZ_CONTINUOUS;
        case SIT_TEMP_SENSOR_ERR:
        case SIT_PRESS_SENSOR_ERR: return BUZZ_LONG_BEEPS;
        case SIT_MILD_OVERHEAT:    return BUZZ_INTERMITTENT;
        default:                   return BUZZ_SILENT; // regulation / normal / cold
    }
}

bool buzzerOnAt(BuzzerPattern pattern, uint32_t t) {
    switch (pattern) {
        case BUZZ_CONTINUOUS:   return true;
        case BUZZ_INTERMITTENT: return (t % INTERMITTENT_PERIOD) < INTERMITTENT_ON;
        case BUZZ_LONG_BEEPS:   return (t % LONG_PERIOD) < LONG_ON;
        default:                return false; // BUZZ_SILENT
    }
}
