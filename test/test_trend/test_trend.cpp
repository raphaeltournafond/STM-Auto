#include <unity.h>
#include "trend.h"
#include "decision.h"

// Native unit tests for the trend history, graph scaling and session stats (lib/trend).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

void test_history_empty(void) {
    History h; historyInit(&h);
    float v, lo, hi;
    TEST_ASSERT_EQUAL_UINT8(0, h.count);
    TEST_ASSERT_FALSE(historyAt(&h, 0, &v));
    TEST_ASSERT_FALSE(historyMinMax(&h, &lo, &hi));
}

void test_history_order_oldest_first(void) {
    History h; historyInit(&h);
    historyPush(&h, 80.0f, true);
    historyPush(&h, 85.5f, true);
    historyPush(&h, 90.04f, true);
    float v;
    TEST_ASSERT_EQUAL_UINT8(3, h.count);
    TEST_ASSERT_TRUE(historyAt(&h, 0, &v)); TEST_ASSERT_FLOAT_WITHIN(0.01f, 80.0f, v);
    TEST_ASSERT_TRUE(historyAt(&h, 2, &v)); TEST_ASSERT_FLOAT_WITHIN(0.01f, 90.0f, v); // tenths
}

void test_history_wraps_keeping_newest(void) {
    History h; historyInit(&h);
    for (int i = 0; i < HISTORY_LEN + 10; i++) historyPush(&h, (float)i, true);
    float v;
    TEST_ASSERT_EQUAL_UINT8(HISTORY_LEN, h.count);
    TEST_ASSERT_TRUE(historyAt(&h, 0, &v));               TEST_ASSERT_EQUAL_FLOAT(10.0f, v);
    TEST_ASSERT_TRUE(historyAt(&h, HISTORY_LEN - 1, &v)); TEST_ASSERT_EQUAL_FLOAT(HISTORY_LEN + 9, v);
}

void test_history_gaps_skipped_in_minmax(void) {
    History h; historyInit(&h);
    historyPush(&h, 3.0f, true);
    historyPush(&h, 0.0f, false);   // sensor fault
    historyPush(&h, 5.5f, true);
    float v, lo, hi;
    TEST_ASSERT_FALSE(historyAt(&h, 1, &v));
    TEST_ASSERT_TRUE(historyMinMax(&h, &lo, &hi));
    TEST_ASSERT_EQUAL_FLOAT(3.0f, lo);
    TEST_ASSERT_EQUAL_FLOAT(5.5f, hi);
}

void test_graph_range_min_span(void) {
    float lo, hi;
    graphRange(90.0f, 91.0f, 10.0f, &lo, &hi);   // flat -> widened around the middle
    TEST_ASSERT_EQUAL_FLOAT(85.5f, lo);
    TEST_ASSERT_EQUAL_FLOAT(95.5f, hi);
    graphRange(60.0f, 110.0f, 10.0f, &lo, &hi);  // already wide -> unchanged
    TEST_ASSERT_EQUAL_FLOAT(60.0f, lo);
    TEST_ASSERT_EQUAL_FLOAT(110.0f, hi);
}

void test_graph_y(void) {
    TEST_ASSERT_EQUAL_INT(36, graphY(10.0f, 0.0f, 10.0f, 36, 28)); // hi -> top row
    TEST_ASSERT_EQUAL_INT(63, graphY(0.0f, 0.0f, 10.0f, 36, 28));  // lo -> bottom row
    TEST_ASSERT_EQUAL_INT(63, graphY(-5.0f, 0.0f, 10.0f, 36, 28)); // clamped
    TEST_ASSERT_EQUAL_INT(36, graphY(50.0f, 0.0f, 10.0f, 36, 28));
    TEST_ASSERT_EQUAL_INT(63, graphY(5.0f, 5.0f, 5.0f, 36, 28));   // degenerate range
}

void test_stats_track_extremes(void) {
    SessionStats s = statsInit();
    s = statsUpdate(s, true, 40.0f, 4.5f, 200);   // cold start, high pressure
    s = statsUpdate(s, true, 95.0f, 2.0f, 200);
    s = statsUpdate(s, true, 112.0f, 1.6f, 200);
    TEST_ASSERT_EQUAL_FLOAT(112.0f, s.tempMax);
    TEST_ASSERT_EQUAL_FLOAT(4.5f, s.pressMax);
    TEST_ASSERT_TRUE(s.anyWarm);
    TEST_ASSERT_EQUAL_FLOAT(1.6f, s.pressMinWarm);
    TEST_ASSERT_EQUAL_UINT32(200, s.hotMs);
}

void test_stats_cold_pressure_not_counted_as_min(void) {
    SessionStats s = statsInit();
    s = statsUpdate(s, true, 30.0f, 0.0f, 200);   // engine off, cold
    TEST_ASSERT_TRUE(s.any);
    TEST_ASSERT_FALSE(s.anyWarm);
}

void test_stats_ignore_sensor_faults(void) {
    SessionStats s = statsInit();
    s = statsUpdate(s, true, 90.0f, 3.0f, 200);
    s = statsUpdate(s, false, 150.0f, 0.0f, 200); // clamped fault reading
    TEST_ASSERT_EQUAL_FLOAT(90.0f, s.tempMax);
    TEST_ASSERT_EQUAL_FLOAT(3.0f, s.pressMinWarm);
    TEST_ASSERT_EQUAL_UINT32(0, s.hotMs);
}

void test_low_press_floor_follows_band(void) {
    // One floor per §4 band, at least the generic minimum (calibration may raise it).
    TEST_ASSERT_TRUE(lowPressCritical(50.0f)  >= PRESS_CRITICAL_MIN[0]); // < 70 °C
    TEST_ASSERT_TRUE(lowPressCritical(80.0f)  >= PRESS_CRITICAL_MIN[1]);
    TEST_ASSERT_TRUE(lowPressCritical(100.0f) >= PRESS_CRITICAL_MIN[2]);
    TEST_ASSERT_TRUE(lowPressCritical(115.0f) >= PRESS_CRITICAL_MIN[3]);
    TEST_ASSERT_EQUAL_FLOAT(lowPressCritical(80.0f), lowPressCritical(89.9f)); // same band
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_history_empty);
    RUN_TEST(test_history_order_oldest_first);
    RUN_TEST(test_history_wraps_keeping_newest);
    RUN_TEST(test_history_gaps_skipped_in_minmax);
    RUN_TEST(test_graph_range_min_span);
    RUN_TEST(test_graph_y);
    RUN_TEST(test_stats_track_extremes);
    RUN_TEST(test_stats_cold_pressure_not_counted_as_min);
    RUN_TEST(test_stats_ignore_sensor_faults);
    RUN_TEST(test_low_press_floor_follows_band);
    return UNITY_END();
}
