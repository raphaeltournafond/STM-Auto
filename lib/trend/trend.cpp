#include "trend.h"
#include "decision.h" // TEMP_NORMAL, TEMP_OVERHEAT

static const int16_t GAP = INT16_MIN;

void historyInit(History* h) {
    h->next  = 0;
    h->count = 0;
}

void historyPush(History* h, float value, bool valid) {
    int16_t t = GAP;
    if (valid) {
        float x = value * 10.0f;
        if (x > 32767.0f)  x = 32767.0f;
        if (x < -32767.0f) x = -32767.0f;
        t = (int16_t)(x < 0 ? x - 0.5f : x + 0.5f);
    }
    h->tenths[h->next] = t;
    h->next = (uint8_t)((h->next + 1) % HISTORY_LEN);
    if (h->count < HISTORY_LEN) h->count++;
}

bool historyAt(const History* h, uint8_t i, float* value) {
    if (i >= h->count) return false;
    uint8_t oldest = (uint8_t)((h->next + HISTORY_LEN - h->count) % HISTORY_LEN);
    int16_t t = h->tenths[(oldest + i) % HISTORY_LEN];
    if (t == GAP) return false;
    *value = t / 10.0f;
    return true;
}

bool historyMinMax(const History* h, float* lo, float* hi) {
    bool any = false;
    for (uint8_t i = 0; i < h->count; i++) {
        float v;
        if (!historyAt(h, i, &v)) continue;
        if (!any || v < *lo) *lo = v;
        if (!any || v > *hi) *hi = v;
        any = true;
    }
    return any;
}

void graphRange(float lo, float hi, float minSpan, float* outLo, float* outHi) {
    if (hi < lo) { float t = lo; lo = hi; hi = t; }
    float span = hi - lo;
    if (span < minSpan) {
        float pad = (minSpan - span) / 2.0f;
        lo -= pad;
        hi += pad;
    }
    *outLo = lo;
    *outHi = hi;
}

int graphY(float value, float lo, float hi, int top, int height) {
    if (height <= 1 || hi <= lo) return top + height - 1;
    float frac = (value - lo) / (hi - lo);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return top + (height - 1) - (int)(frac * (height - 1) + 0.5f);
}

SessionStats statsInit() {
    return {false, 0.0f, 0.0f, false, 0.0f, 0};
}

SessionStats statsUpdate(SessionStats s, bool sensorsOk, float temperature,
                         float pressure, uint32_t dtMs) {
    if (!sensorsOk) return s;
    if (!s.any || temperature > s.tempMax) s.tempMax  = temperature;
    if (!s.any || pressure > s.pressMax)   s.pressMax = pressure;
    s.any = true;
    if (temperature >= TEMP_NORMAL) {
        if (!s.anyWarm || pressure < s.pressMinWarm) s.pressMinWarm = pressure;
        s.anyWarm = true;
    }
    if (temperature >= TEMP_OVERHEAT) s.hotMs += dtMs;
    return s;
}
