#pragma once
#include "decision.h" // PRESS_OVER

// OLED liquid-fill gauge scales. Pure / unit-testable; drawing lives in src/main.cpp.

// Temperature bar spans the sensor scale table (925 Ω = 40 °C .. 32 Ω = 150 °C).
constexpr float GAUGE_TEMP_MIN  = 40.0f;
constexpr float GAUGE_TEMP_MAX  = 150.0f;
// Pressure bar spans 0 bar .. the end of the sensor table (3.01 V = 10 bar), so the
// overpressure ceiling shows as a near-full bar instead of the bar saturating early.
constexpr float GAUGE_PRESS_MIN = 0.0f;
constexpr float GAUGE_PRESS_MAX = 10.0f;
static_assert(PRESS_OVER < GAUGE_PRESS_MAX,
              "overpressure ceiling must fit inside the pressure gauge range");

// Fill width in pixels for `value` on a bar of `width` px spanning [lo, hi], clamped.
int gaugeFillWidth(float value, float lo, float hi, int width);
