#include <unity.h>
#include "gauge.h"

// Native unit tests for the OLED gauge scales (lib/gauge).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

void test_fill_clamps_and_scales(void) {
    TEST_ASSERT_EQUAL_INT(0,   gaugeFillWidth(-5.0f, 0.0f, 10.0f, 128)); // below range
    TEST_ASSERT_EQUAL_INT(0,   gaugeFillWidth(0.0f,  0.0f, 10.0f, 128));
    TEST_ASSERT_EQUAL_INT(64,  gaugeFillWidth(5.0f,  0.0f, 10.0f, 128)); // midpoint
    TEST_ASSERT_EQUAL_INT(128, gaugeFillWidth(10.0f, 0.0f, 10.0f, 128));
    TEST_ASSERT_EQUAL_INT(128, gaugeFillWidth(12.0f, 0.0f, 10.0f, 128)); // above range
}

void test_fill_degenerate_range_is_empty(void) {
    TEST_ASSERT_EQUAL_INT(0, gaugeFillWidth(5.0f, 10.0f, 10.0f, 128));
    TEST_ASSERT_EQUAL_INT(0, gaugeFillWidth(5.0f, 0.0f, 10.0f, 0));
}

void test_pressure_bar_not_full_below_overpressure(void) {
    // Regression: the bar used to saturate at 8 bar while the alarm is at PRESS_OVER.
    int atCeiling = gaugeFillWidth(PRESS_OVER, GAUGE_PRESS_MIN, GAUGE_PRESS_MAX, 128);
    TEST_ASSERT_LESS_THAN_INT(128, atCeiling);
    TEST_ASSERT_LESS_THAN_INT(atCeiling,
                              gaugeFillWidth(8.0f, GAUGE_PRESS_MIN, GAUGE_PRESS_MAX, 128));
}

void test_temperature_bar_spans_table(void) {
    TEST_ASSERT_EQUAL_INT(0,   gaugeFillWidth(40.0f,  GAUGE_TEMP_MIN, GAUGE_TEMP_MAX, 128));
    TEST_ASSERT_EQUAL_INT(128, gaugeFillWidth(150.0f, GAUGE_TEMP_MIN, GAUGE_TEMP_MAX, 128));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_fill_clamps_and_scales);
    RUN_TEST(test_fill_degenerate_range_is_empty);
    RUN_TEST(test_pressure_bar_not_full_below_overpressure);
    RUN_TEST(test_temperature_bar_spans_table);
    return UNITY_END();
}
