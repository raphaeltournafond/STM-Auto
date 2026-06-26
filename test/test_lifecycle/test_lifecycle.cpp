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

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_temp_sensor_plausible);
    RUN_TEST(test_press_sensor_plausible);
    RUN_TEST(test_init_fail_voice_track);
    RUN_TEST(test_init_result_label_nonempty);
    return UNITY_END();
}
