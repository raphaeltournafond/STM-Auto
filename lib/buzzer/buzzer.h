#pragma once
#include <stdint.h>
#include "decision.h" // Situation

// Buzzer alert patterns from the DECISION_MATRIX §3 sound column.
// Pure / hardware-independent so they can be unit-tested natively. The actual
// tone() drive and the loop tick live in src/main.cpp.

enum BuzzerPattern {
    BUZZ_SILENT,       // situations 7-9 (regulation / normal / cold)
    BUZZ_CONTINUOUS,   // situations 3-5 (stop engine / low pressure / overpressure)
    BUZZ_INTERMITTENT, // situation 6 (mild overheat)
    BUZZ_LONG_BEEPS,   // situations 1-2 (sensor faults)
};

constexpr uint16_t BUZZER_FREQ_HZ = 2700; // passive piezo drive frequency

// Pattern for the active situation.
BuzzerPattern buzzerPattern(Situation sit);

// Whether the buzzer envelope is "on" at absolute time t (ms). Periodic, so the
// caller can pass millis() directly. CONTINUOUS is always on, SILENT always off.
bool buzzerOnAt(BuzzerPattern pattern, uint32_t t);
