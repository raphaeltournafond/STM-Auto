#include "screen.h"
#include <stdio.h>

Situation alarmToShow(Situation stable, uint16_t alertMask, uint32_t nowMs) {
    if (situationAlert(stable) == ALERT_OFF || stable == SIT_STOP_ENGINE) return stable;

    Situation list[16];
    uint8_t n = 0;
    list[n++] = stable;
    for (uint8_t s = SIT_TEMP_SENSOR_ERR; s <= SIT_COLD; s++) {
        if (s == stable || !(alertMask & (1u << s))) continue;
        list[n++] = (Situation)s;
    }
    return list[(nowMs / ALARM_CYCLE_MS) % n];
}

int centeredTextX(int chars, int charWidth, int width) {
    return (width - chars * charWidth) / 2;
}

int tempTextChars(float temperature) {
    return ((int)temperature >= 100) ? 5 : 4; // "100°C" / "85°C"
}

int pressTextChars(float pressure) {
    return (pressure >= 9.95f) ? 5 : 4;       // "10.0b" / "3.5b"
}

void formatResistance(float ohms, char* out, int size) {
    if (ohms < 0.0f) ohms = 0.0f;
    if (ohms < 9.95f) {
        int tenths = (int)(ohms * 10.0f + 0.5f);
        snprintf(out, size, "%d.%dR", tenths / 10, tenths % 10);
    } else if (ohms < 999.5f) {
        snprintf(out, size, "%dR", (int)(ohms + 0.5f));
    } else if (ohms < 99950.0f) {
        int tenthsK = (int)(ohms / 100.0f + 0.5f);
        snprintf(out, size, "%d.%dk", tenthsK / 10, tenthsK % 10);
    } else {
        snprintf(out, size, "%dk", (int)(ohms / 1000.0f + 0.5f));
    }
}

void formatVoltage(float volts, char* out, int size) {
    if (volts < 0.0f) volts = 0.0f;
    int hundredths = (int)(volts * 100.0f + 0.5f);
    snprintf(out, size, "%d.%02dV", hundredths / 100, hundredths % 100);
}
