#include <unity.h>
#include "ws2812.h"

// Native unit tests for the WS2812B encoding (lib/ws2812).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

// 3-SPI-bit codes: data 0 -> 0b100, data 1 -> 0b110.
// A 0x00 byte -> 100100100100100100100100 -> 0x92 0x49 0x24
// A 0xFF byte -> 110110110110110110110110 -> 0xDB 0x6D 0xB6
static const uint8_t ZERO_BYTE[3] = {0x92, 0x49, 0x24};
static const uint8_t FF_BYTE[3]   = {0xDB, 0x6D, 0xB6};

void test_encode_all_zero(void) {
    uint8_t out[WS2812_PIXEL_BYTES];
    encodePixelGRB(Rgb{0, 0, 0}, out);
    for (uint8_t i = 0; i < 9; i++) TEST_ASSERT_EQUAL_HEX8(ZERO_BYTE[i % 3], out[i]);
}

void test_encode_all_full(void) {
    uint8_t out[WS2812_PIXEL_BYTES];
    encodePixelGRB(Rgb{255, 255, 255}, out);
    for (uint8_t i = 0; i < 9; i++) TEST_ASSERT_EQUAL_HEX8(FF_BYTE[i % 3], out[i]);
}

void test_encode_grb_order(void) {
    // Pure red: R=0xFF, G=0x00, B=0x00. Wire order G,R,B -> zero, full, zero.
    uint8_t out[WS2812_PIXEL_BYTES];
    encodePixelGRB(Rgb{255, 0, 0}, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ZERO_BYTE, &out[0], 3); // G
    TEST_ASSERT_EQUAL_HEX8_ARRAY(FF_BYTE,   &out[3], 3); // R
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ZERO_BYTE, &out[6], 3); // B
}

void test_encoded_stream_ends_low(void) {
    // Last SPI bit must be 0 so the line idles low for the reset/latch.
    uint8_t out[WS2812_PIXEL_BYTES];
    encodePixelGRB(Rgb{123, 200, 45}, out);
    TEST_ASSERT_EQUAL_UINT8(0, out[WS2812_PIXEL_BYTES - 1] & 0x01);
}

void test_alert_colors(void) {
    Rgb off = alertColor(ALERT_OFF);
    TEST_ASSERT_EQUAL_UINT8(0, off.r); TEST_ASSERT_EQUAL_UINT8(0, off.g); TEST_ASSERT_EQUAL_UINT8(0, off.b);

    Rgb warn = alertColor(ALERT_WARN);   // yellow: R and G on, B off
    TEST_ASSERT_TRUE(warn.r > 0); TEST_ASSERT_TRUE(warn.g > 0); TEST_ASSERT_EQUAL_UINT8(0, warn.b);

    Rgb alarm = alertColor(ALERT_ALARM); // red: R on, G and B off
    TEST_ASSERT_TRUE(alarm.r > 0); TEST_ASSERT_EQUAL_UINT8(0, alarm.g); TEST_ASSERT_EQUAL_UINT8(0, alarm.b);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_encode_all_zero);
    RUN_TEST(test_encode_all_full);
    RUN_TEST(test_encode_grb_order);
    RUN_TEST(test_encoded_stream_ends_low);
    RUN_TEST(test_alert_colors);
    return UNITY_END();
}
