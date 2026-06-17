#pragma once
#include <stdint.h>
#include "decision.h" // AlertLevel

// WS2812B pixel encoding for transmission over an SPI MOSI line.
// Each WS2812 data bit is sent as 3 SPI bits at ~2.4 MHz: '0' -> 0b100,
// '1' -> 0b110 (high ~0.4 us / ~0.8 us, within the WS2812B timing window).
// One pixel (GRB, 24 bits) -> 9 SPI bytes, MSB-first. The encoded stream always
// ends in a 0 bit, so the line idles low for the >50 us reset/latch gap.
//
// Pure / hardware-independent so it can be unit-tested natively.

struct Rgb { uint8_t r, g, b; };

constexpr uint8_t WS2812_PIXEL_BYTES = 9; // SPI bytes per WS2812B pixel

// Indicator colour for an alert level (DECISION_MATRIX LED column: off/yellow/red).
Rgb alertColor(AlertLevel level);

// Encode one pixel (GRB order) into WS2812_PIXEL_BYTES SPI bytes (out[9]).
void encodePixelGRB(const Rgb& c, uint8_t* out);
