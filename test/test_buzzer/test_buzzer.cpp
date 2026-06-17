#include <unity.h>
#include "buzzer.h"

// Native unit tests for the buzzer patterns (lib/buzzer).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

// ---------- pattern selection (§3 sound column) ----------

void test_pattern_continuous_for_alarms(void) {
    TEST_ASSERT_EQUAL(BUZZ_CONTINUOUS, buzzerPattern(SIT_STOP_ENGINE));
    TEST_ASSERT_EQUAL(BUZZ_CONTINUOUS, buzzerPattern(SIT_LOW_PRESSURE));
    TEST_ASSERT_EQUAL(BUZZ_CONTINUOUS, buzzerPattern(SIT_OVERPRESSURE));
}

void test_pattern_long_beeps_for_sensor_faults(void) {
    TEST_ASSERT_EQUAL(BUZZ_LONG_BEEPS, buzzerPattern(SIT_TEMP_SENSOR_ERR));
    TEST_ASSERT_EQUAL(BUZZ_LONG_BEEPS, buzzerPattern(SIT_PRESS_SENSOR_ERR));
}

void test_pattern_intermittent_for_overheat(void) {
    TEST_ASSERT_EQUAL(BUZZ_INTERMITTENT, buzzerPattern(SIT_MILD_OVERHEAT));
}

void test_pattern_silent_for_normal_bands(void) {
    TEST_ASSERT_EQUAL(BUZZ_SILENT, buzzerPattern(SIT_REGULATION));
    TEST_ASSERT_EQUAL(BUZZ_SILENT, buzzerPattern(SIT_NORMAL));
    TEST_ASSERT_EQUAL(BUZZ_SILENT, buzzerPattern(SIT_COLD));
}

// ---------- envelope scheduler ----------

void test_envelope_silent_always_off(void) {
    TEST_ASSERT_FALSE(buzzerOnAt(BUZZ_SILENT, 0));
    TEST_ASSERT_FALSE(buzzerOnAt(BUZZ_SILENT, 1234));
}

void test_envelope_continuous_always_on(void) {
    TEST_ASSERT_TRUE(buzzerOnAt(BUZZ_CONTINUOUS, 0));
    TEST_ASSERT_TRUE(buzzerOnAt(BUZZ_CONTINUOUS, 5000));
}

void test_envelope_intermittent_400_on_400_off(void) {
    TEST_ASSERT_TRUE(buzzerOnAt(BUZZ_INTERMITTENT, 0));
    TEST_ASSERT_TRUE(buzzerOnAt(BUZZ_INTERMITTENT, 200));
    TEST_ASSERT_FALSE(buzzerOnAt(BUZZ_INTERMITTENT, 400));
    TEST_ASSERT_FALSE(buzzerOnAt(BUZZ_INTERMITTENT, 600));
    TEST_ASSERT_TRUE(buzzerOnAt(BUZZ_INTERMITTENT, 800)); // next cycle
}

void test_envelope_long_beeps_800_on_400_off(void) {
    TEST_ASSERT_TRUE(buzzerOnAt(BUZZ_LONG_BEEPS, 0));
    TEST_ASSERT_TRUE(buzzerOnAt(BUZZ_LONG_BEEPS, 700));
    TEST_ASSERT_FALSE(buzzerOnAt(BUZZ_LONG_BEEPS, 800));
    TEST_ASSERT_FALSE(buzzerOnAt(BUZZ_LONG_BEEPS, 1100));
    TEST_ASSERT_TRUE(buzzerOnAt(BUZZ_LONG_BEEPS, 1200)); // next cycle
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pattern_continuous_for_alarms);
    RUN_TEST(test_pattern_long_beeps_for_sensor_faults);
    RUN_TEST(test_pattern_intermittent_for_overheat);
    RUN_TEST(test_pattern_silent_for_normal_bands);
    RUN_TEST(test_envelope_silent_always_off);
    RUN_TEST(test_envelope_continuous_always_on);
    RUN_TEST(test_envelope_intermittent_400_on_400_off);
    RUN_TEST(test_envelope_long_beeps_800_on_400_off);
    return UNITY_END();
}
