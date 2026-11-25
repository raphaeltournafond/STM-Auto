#include <unity.h>
#include <string.h>
#include "screen.h"

// Native unit tests for the OLED screen helpers (lib/screen).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

static uint16_t bit(Situation s) { return (uint16_t)(1u << s); }

// ---------- multi-alarm alternation (§6) ----------

void test_alarm_single_shows_stable(void) {
    uint16_t m = bit(SIT_LOW_PRESSURE);
    TEST_ASSERT_EQUAL_INT(SIT_LOW_PRESSURE, alarmToShow(SIT_LOW_PRESSURE, m, 0));
    TEST_ASSERT_EQUAL_INT(SIT_LOW_PRESSURE, alarmToShow(SIT_LOW_PRESSURE, m, 5 * ALARM_CYCLE_MS));
}

void test_alarm_alternates_stable_first_then_priority_order(void) {
    uint16_t m = bit(SIT_LOW_PRESSURE) | bit(SIT_MILD_OVERHEAT) | bit(SIT_OVERPRESSURE);
    TEST_ASSERT_EQUAL_INT(SIT_LOW_PRESSURE,  alarmToShow(SIT_LOW_PRESSURE, m, 0));
    TEST_ASSERT_EQUAL_INT(SIT_OVERPRESSURE,  alarmToShow(SIT_LOW_PRESSURE, m, 1 * ALARM_CYCLE_MS));
    TEST_ASSERT_EQUAL_INT(SIT_MILD_OVERHEAT, alarmToShow(SIT_LOW_PRESSURE, m, 2 * ALARM_CYCLE_MS));
    TEST_ASSERT_EQUAL_INT(SIT_LOW_PRESSURE,  alarmToShow(SIT_LOW_PRESSURE, m, 3 * ALARM_CYCLE_MS));
}

void test_alarm_stable_not_yet_in_raw_mask_still_shown(void) {
    // debounce lag: the stable alarm has just cleared in the raw mask
    TEST_ASSERT_EQUAL_INT(SIT_MILD_OVERHEAT, alarmToShow(SIT_MILD_OVERHEAT, 0, ALARM_CYCLE_MS));
}

void test_alarm_stop_engine_never_diluted(void) {
    uint16_t m = bit(SIT_STOP_ENGINE) | bit(SIT_LOW_PRESSURE) | bit(SIT_MILD_OVERHEAT);
    for (uint32_t k = 0; k < 4; k++)
        TEST_ASSERT_EQUAL_INT(SIT_STOP_ENGINE, alarmToShow(SIT_STOP_ENGINE, m, k * ALARM_CYCLE_MS));
}

void test_alarm_non_alert_stable_unchanged(void) {
    // e.g. raw overheat latched but not yet confirmed: no alternation on a calm screen
    TEST_ASSERT_EQUAL_INT(SIT_REGULATION, alarmToShow(SIT_REGULATION, bit(SIT_MILD_OVERHEAT), ALARM_CYCLE_MS));
}

void test_alarm_both_sensor_errors_alternate(void) {
    uint16_t m = bit(SIT_TEMP_SENSOR_ERR) | bit(SIT_PRESS_SENSOR_ERR);
    TEST_ASSERT_EQUAL_INT(SIT_TEMP_SENSOR_ERR,  alarmToShow(SIT_TEMP_SENSOR_ERR, m, 0));
    TEST_ASSERT_EQUAL_INT(SIT_PRESS_SENSOR_ERR, alarmToShow(SIT_TEMP_SENSOR_ERR, m, ALARM_CYCLE_MS));
}

// ---------- layout ----------

void test_centered_text_x(void) {
    TEST_ASSERT_EQUAL_INT(28, centeredTextX(4, 18, 128)); // "85°C" size 3
    TEST_ASSERT_EQUAL_INT(19, centeredTextX(5, 18, 128)); // "100°C" size 3
}

void test_text_char_counts(void) {
    TEST_ASSERT_EQUAL_INT(4, tempTextChars(85.0f));
    TEST_ASSERT_EQUAL_INT(4, tempTextChars(99.9f));  // printed truncated: "99"
    TEST_ASSERT_EQUAL_INT(5, tempTextChars(100.0f));
    TEST_ASSERT_EQUAL_INT(4, pressTextChars(3.5f));
    TEST_ASSERT_EQUAL_INT(4, pressTextChars(9.94f)); // "9.9b"
    TEST_ASSERT_EQUAL_INT(5, pressTextChars(9.96f)); // rounds to "10.0b"
    TEST_ASSERT_EQUAL_INT(5, pressTextChars(10.0f));
}

// ---------- raw values ----------

void test_format_resistance(void) {
    char b[12];
    formatResistance(3.21f, b, sizeof(b));     TEST_ASSERT_EQUAL_STRING("3.2R", b);  // shorted
    formatResistance(0.0f, b, sizeof(b));      TEST_ASSERT_EQUAL_STRING("0.0R", b);
    formatResistance(-1.0f, b, sizeof(b));     TEST_ASSERT_EQUAL_STRING("0.0R", b);
    formatResistance(9.97f, b, sizeof(b));     TEST_ASSERT_EQUAL_STRING("10R", b);   // no "10.0R"
    formatResistance(925.0f, b, sizeof(b));    TEST_ASSERT_EQUAL_STRING("925R", b);
    formatResistance(18000.0f, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("18.0k", b); // cold soak
    formatResistance(72380.0f, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("72.4k", b); // open circuit
    formatResistance(150000.0f, b, sizeof(b)); TEST_ASSERT_EQUAL_STRING("150k", b);
}

void test_format_resistance_fits_error_row(void) {
    // "ERR " + value must fit 10 size-2 chars (128 px / 12 px)
    char b[12];
    formatResistance(99940.0f, b, sizeof(b));
    TEST_ASSERT_LESS_OR_EQUAL_INT(6, (int)strlen(b));
}

void test_format_voltage(void) {
    char b[12];
    formatVoltage(4.95f, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("4.95V", b);
    formatVoltage(0.04f, b, sizeof(b));  TEST_ASSERT_EQUAL_STRING("0.04V", b);
    formatVoltage(0.0f, b, sizeof(b));   TEST_ASSERT_EQUAL_STRING("0.00V", b);
    formatVoltage(2.999f, b, sizeof(b)); TEST_ASSERT_EQUAL_STRING("3.00V", b);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_alarm_single_shows_stable);
    RUN_TEST(test_alarm_alternates_stable_first_then_priority_order);
    RUN_TEST(test_alarm_stable_not_yet_in_raw_mask_still_shown);
    RUN_TEST(test_alarm_stop_engine_never_diluted);
    RUN_TEST(test_alarm_non_alert_stable_unchanged);
    RUN_TEST(test_alarm_both_sensor_errors_alternate);
    RUN_TEST(test_centered_text_x);
    RUN_TEST(test_text_char_counts);
    RUN_TEST(test_format_resistance);
    RUN_TEST(test_format_resistance_fits_error_row);
    RUN_TEST(test_format_voltage);
    return UNITY_END();
}
