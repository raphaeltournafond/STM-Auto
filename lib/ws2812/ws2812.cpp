#include "ws2812.h"

// Moderate brightness so a cabin status LED is clearly visible but not blinding.
static const uint8_t LVL = 64;

Rgb alertColor(AlertLevel level) {
    switch (level) {
        case ALERT_ALARM: return {LVL, 0, 0};   // red
        case ALERT_WARN:  return {LVL, LVL, 0};  // yellow
        default:          return {0, 0, 0};      // off
    }
}

// One WS2812 data byte -> 3 SPI bytes (24 SPI bits, MSB-first).
static void encodeByte(uint8_t value, uint8_t* out) {
    uint32_t bits = 0;
    for (int8_t i = 7; i >= 0; i--) {
        bits = (bits << 3) | (((value >> i) & 1u) ? 0b110u : 0b100u);
    }
    out[0] = (bits >> 16) & 0xFF;
    out[1] = (bits >> 8) & 0xFF;
    out[2] = bits & 0xFF;
}

void encodePixelGRB(const Rgb& c, uint8_t* out) {
    encodeByte(c.g, out);      // WS2812B wire order is G, R, B
    encodeByte(c.r, out + 3);
    encodeByte(c.b, out + 6);
}
