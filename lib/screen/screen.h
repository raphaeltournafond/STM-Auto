#pragma once
#include <stdint.h>
#include "decision.h" // Situation, situationAlert

// OLED screen helpers: what to show and how to lay it out. Pure / unit-testable;
// the Adafruit drawing calls live in src/main.cpp.

// ----- Multi-alarm alternation (DECISION_MATRIX §6) -----
// Each active alarm stays on screen this long before the next one is shown.
constexpr uint32_t ALARM_CYCLE_MS = 2000;

// Situation the status line shows at time nowMs. With several alarms active
// (alertMask from activeAlertMask()) the screen alternates between them, starting
// from the stable (debounced) one, then the others in priority order. STOP ENGINE
// is never diluted: it already combines the temperature and pressure alarms.
// A non-alert stable situation is returned as-is.
Situation alarmToShow(Situation stable, uint16_t alertMask, uint32_t nowMs);

// ----- Text layout -----
// x that centres `chars` characters of `charWidth` px on a `width` px line.
int centeredTextX(int chars, int charWidth, int width);
// Characters printed for the gauge readouts: "<int>°C" and "<x.x>b".
int tempTextChars(float temperature);
int pressTextChars(float pressure); // print(p, 1) rounds: 9.96 -> "10.0b"

// ----- Raw sensor values (sensor-error and INIT_FAIL screens, §2/§3) -----
// Compact, no float printf needed: "3.2R", "925R", "18.0k", "123k" (R = ohm).
void formatResistance(float ohms, char* out, int size);
// "4.95V".
void formatVoltage(float volts, char* out, int size);
// Duration: "m:ss" under an hour, "h:mm:ss" above.
void formatDuration(uint32_t ms, char* out, int size);
