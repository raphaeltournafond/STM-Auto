#pragma once
#include <stdint.h>
#include "decision.h" // reuses PRESS_MIN_SAFE_VOLTAGE

// INIT plausibility bounds for the temperature sensor: the same window as the
// runtime fault check (decision.h), so a sensor that passes INIT can never be
// flagged faulty by RUN at the same temperature (e.g. on a cold-soak start).
constexpr float TEMP_PLAUSIBLE_MIN_RESISTANCE = TEMP_MIN_SAFE_RESISTANCE; // below -> shorted
constexpr float TEMP_PLAUSIBLE_MAX_RESISTANCE = TEMP_MAX_SAFE_RESISTANCE; // at/above -> open

// Startup life phases (DECISION_MATRIX §2): BOOT -> INIT -> RUN, with a blocking
// INIT_FAIL from INIT. Phases run once at boot in setup(); RUN is the loop().
// Pure helpers here (plausibility + fail mapping); the orchestration + hardware
// self-tests live in src/main.cpp.

// Result of the INIT self-test sequence (order follows §2).
enum InitResult {
    INIT_OK,
    INIT_FAIL_OLED,
    INIT_FAIL_MP3_SD,
    INIT_FAIL_TEMP,
    INIT_FAIL_PRESS,
};

// Raw-signal plausibility for the INIT sensor checks (sensor present, not open).
bool tempSensorPlausible(float resistance);
bool pressSensorPlausible(float vPressure);

// Short screen label for a result, and the §7 voice file for an INIT_FAIL
// (5 = "defaut", 0 = none).
const char* initResultLabel(InitResult r);
int initFailVoiceTrack(InitResult r);
