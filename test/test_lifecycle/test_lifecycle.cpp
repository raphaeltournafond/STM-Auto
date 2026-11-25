#include <unity.h>
#include <string.h>
#include "lifecycle.h"

// Native unit tests for the INIT helpers (lib/lifecycle).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

void test_temp_sensor_plausible(void) {
    TEST_ASSERT_TRUE(tempSensorPlausible(120.0f));                            // warm oil (~80 °C)
    TEST_ASSERT_TRUE(tempSensorPlausible(925.0f));                            // 40 °C (table min)
    TEST_ASSERT_TRUE(tempSensorPlausible(18000.0f));                          // ~-20 °C cold soak
    TEST_ASSERT_TRUE(tempSensorPlausible(TEMP_PLAUSIBLE_MIN_RESISTANCE));     // lower bound inclusive
    TEST_ASSERT_TRUE(tempSensorPlausible(TEMP_PLAUSIBLE_MAX_RESISTANCE - 1)); // just inside upper
    TEST_ASSERT_FALSE(tempSensorPlausible(TEMP_PLAUSIBLE_MAX_RESISTANCE));    // at upper -> open
    TEST_ASSERT_FALSE(tempSensorPlausible(70000.0f));                         // open circuit
    TEST_ASSERT_FALSE(tempSensorPlausible(TEMP_PLAUSIBLE_MIN_RESISTANCE - 1.0f)); // below min -> short
    TEST_ASSERT_FALSE(tempSensorPlausible(0.0f));                             // shorted to GND
}

void test_press_sensor_plausible(void) {
    TEST_ASSERT_TRUE(pressSensorPlausible(2.0f));
    TEST_ASSERT_TRUE(pressSensorPlausible(PRESS_MIN_SAFE_VOLTAGE));      // boundary is inclusive
    TEST_ASSERT_FALSE(pressSensorPlausible(0.1f));                       // disconnected ~0 V
    TEST_ASSERT_FALSE(pressSensorPlausible(4.95f));                      // shorted to 5 V
}

void test_init_fail_voice_track(void) {
    TEST_ASSERT_EQUAL_INT(0, initFailVoiceTrack(INIT_OK));
    TEST_ASSERT_EQUAL_INT(5, initFailVoiceTrack(INIT_FAIL_OLED));
    TEST_ASSERT_EQUAL_INT(5, initFailVoiceTrack(INIT_FAIL_TEMP));
    TEST_ASSERT_EQUAL_INT(5, initFailVoiceTrack(INIT_FAIL_PRESS));
    TEST_ASSERT_EQUAL_INT(5, initFailVoiceTrack(INIT_FAIL_MP3_SD));
}

void test_init_result_label_nonempty(void) {
    // every result has a distinct, non-empty label
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(initResultLabel(INIT_OK)));
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(initResultLabel(INIT_FAIL_OLED)));
    TEST_ASSERT_GREATER_THAN(0, (int)strlen(initResultLabel(INIT_FAIL_PRESS)));
    TEST_ASSERT_EQUAL_STRING("TEMP SENS FAIL", initResultLabel(INIT_FAIL_TEMP));
}

void test_init_fail_code_follows_init_order(void) {
    TEST_ASSERT_EQUAL_UINT8(0, initFailCode(INIT_OK));
    TEST_ASSERT_EQUAL_UINT8(1, initFailCode(INIT_FAIL_OLED));
    TEST_ASSERT_EQUAL_UINT8(2, initFailCode(INIT_FAIL_MP3_SD));
    TEST_ASSERT_EQUAL_UINT8(3, initFailCode(INIT_FAIL_TEMP));
    TEST_ASSERT_EQUAL_UINT8(4, initFailCode(INIT_FAIL_PRESS));
}

// Count rising edges of the pattern over one period, sampled every 10 ms.
static int pulsesInPeriod(uint8_t code) {
    const uint32_t period = code * (INIT_FAIL_PULSE_ON + INIT_FAIL_PULSE_OFF) + INIT_FAIL_PAUSE;
    int pulses = 0;
    bool prev = false;
    for (uint32_t t = 0; t < period; t += 10) {
        bool on = initFailPatternOn(code, t);
        if (on && !prev) pulses++;
        prev = on;
    }
    return pulses;
}

void test_init_fail_pattern_pulses_n_times(void) {
    TEST_ASSERT_EQUAL_INT(1, pulsesInPeriod(1));
    TEST_ASSERT_EQUAL_INT(3, pulsesInPeriod(3));
    TEST_ASSERT_EQUAL_INT(4, pulsesInPeriod(4));
}

void test_init_fail_pattern_timing(void) {
    TEST_ASSERT_TRUE(initFailPatternOn(2, 0));                                 // first pulse
    TEST_ASSERT_FALSE(initFailPatternOn(2, INIT_FAIL_PULSE_ON));               // gap
    TEST_ASSERT_TRUE(initFailPatternOn(2, INIT_FAIL_PULSE_ON + INIT_FAIL_PULSE_OFF)); // second
    TEST_ASSERT_FALSE(initFailPatternOn(2, 2 * (INIT_FAIL_PULSE_ON + INIT_FAIL_PULSE_OFF))); // pause
    const uint32_t period = 2 * (INIT_FAIL_PULSE_ON + INIT_FAIL_PULSE_OFF) + INIT_FAIL_PAUSE;
    TEST_ASSERT_TRUE(initFailPatternOn(2, period));                            // repeats
    TEST_ASSERT_FALSE(initFailPatternOn(0, 0));                                // INIT_OK: never
}

void test_init_fail_timeout_is_bounded(void) {
    // A reset while driving must not leave the engine unmonitored for long, but
    // the operator needs time to read the screen; keep it within a sane window.
    TEST_ASSERT_TRUE(INIT_FAIL_TIMEOUT_MS >= 5000 && INIT_FAIL_TIMEOUT_MS <= 30000);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_temp_sensor_plausible);
    RUN_TEST(test_press_sensor_plausible);
    RUN_TEST(test_init_fail_voice_track);
    RUN_TEST(test_init_result_label_nonempty);
    RUN_TEST(test_init_fail_code_follows_init_order);
    RUN_TEST(test_init_fail_pattern_pulses_n_times);
    RUN_TEST(test_init_fail_pattern_timing);
    RUN_TEST(test_init_fail_timeout_is_bounded);
    return UNITY_END();
}
