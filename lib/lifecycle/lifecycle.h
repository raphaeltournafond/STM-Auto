#pragma once
#include <stdint.h>
#include "decision.h" // reuses PRESS_MIN_SAFE_VOLTAGE

// INIT plausibility bounds for the temperature sensor: the same window as the
// runtime fault check (decision.h), so a sensor that passes INIT can never be
// flagged faulty by RUN at the same temperature (e.g. on a cold-soak start).
constexpr float TEMP_PLAUSIBLE_MIN_RESISTANCE = TEMP_MIN_SAFE_RESISTANCE; // below -> shorted
constexpr float TEMP_PLAUSIBLE_MAX_RESISTANCE = TEMP_MAX_SAFE_RESISTANCE; // at/above -> open

// Startup life phases (DECISION_MATRIX §2): BOOT -> INIT -> RUN, with INIT_FAIL
// from INIT (blocks until ACK or INIT_FAIL_TIMEOUT_MS, then INIT carries on). Phases run once at boot in setup(); RUN is the loop().
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

// INIT_FAIL gives up blocking after this long and lets INIT carry on into RUN,
// which reports a sensor fault itself. A reset while driving (supply dip, loose
// connector) must never leave the engine unmonitored waiting for an ACK press.
constexpr uint32_t INIT_FAIL_TIMEOUT_MS = 10000;

// INIT_FAIL blink/beep code: the failed step's position in the INIT order
// (1 OLED, 2 MP3, 3 TEMP, 4 PRESS); 0 for INIT_OK.
uint8_t initFailCode(InitResult r);

// INIT_FAIL LED + buzzer envelope at time t (ms since INIT_FAIL was entered):
// `code` long pulses (INIT_FAIL_PULSE_ON on / INIT_FAIL_PULSE_OFF off), then an
// INIT_FAIL_PAUSE gap, repeating. code 0 is always off.
constexpr uint32_t INIT_FAIL_PULSE_ON  = 600;
constexpr uint32_t INIT_FAIL_PULSE_OFF = 400;
constexpr uint32_t INIT_FAIL_PAUSE     = 1600;
bool initFailPatternOn(uint8_t code, uint32_t t);
