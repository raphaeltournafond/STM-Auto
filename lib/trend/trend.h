#pragma once
#include <stdint.h>

// Trend history for the graph views and session statistics (DECISION_MATRIX §10).
// Pure / unit-testable; sampling cadence and drawing live in src/main.cpp.

// ----- History ring buffer (one per sensor) -----
// HISTORY_LEN samples, one every HISTORY_SAMPLE_MS -> 128 x 5 s ≈ 10.7 min, one
// sample per OLED column. Values are stored in tenths (int16) to keep RAM low.
constexpr uint8_t  HISTORY_LEN       = 128;
constexpr uint32_t HISTORY_SAMPLE_MS = 5000;

struct History {
    int16_t tenths[HISTORY_LEN];
    uint8_t next;   // write index
    uint8_t count;  // samples stored (<= HISTORY_LEN)
};

void historyInit(History* h);
// valid = false records a gap (sensor fault), drawn as a break in the graph.
void historyPush(History* h, float value, bool valid);
// Sample i, 0 = oldest .. count-1 = newest. Returns false for a gap.
bool historyAt(const History* h, uint8_t i, float* value);
// Min / max over the valid samples; false if there are none.
bool historyMinMax(const History* h, float* lo, float* hi);

// ----- Graph scaling -----
// Widen [lo, hi] symmetrically to at least minSpan, so a flat signal is not
// blown up into full-height noise.
void graphRange(float lo, float hi, float minSpan, float* outLo, float* outHi);
// Pixel row for value on a graph `height` px tall starting at `top` (hi at the
// top), clamped inside the graph.
int graphY(float value, float lo, float hi, int top, int height);

// ----- Session statistics (since power-on) -----
struct SessionStats {
    bool  any;         // at least one valid reading
    float tempMax;     // °C
    float pressMax;    // bar
    bool  anyWarm;     // at least one valid reading with oil >= TEMP_NORMAL
    float pressMinWarm;// lowest pressure with warm oil (cold start / engine-off excluded)
    uint32_t hotMs;    // time spent at or above TEMP_OVERHEAT
};

SessionStats statsInit();
// sensorsOk = both sensors plausible (faulty readings are ignored); dtMs = time
// since the previous update.
SessionStats statsUpdate(SessionStats s, bool sensorsOk, float temperature,
                         float pressure, uint32_t dtMs);
