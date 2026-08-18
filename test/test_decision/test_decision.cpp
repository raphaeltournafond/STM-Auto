#include <unity.h>
#include "decision.h"

// Native unit tests for the pure decision logic (lib/decision).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

// ---------- interpolate ----------

void test_interpolate_below_range_clamps_low(void) {
    const float x[] = {0, 1, 2};
    const float y[] = {10, 20, 30};
    TEST_ASSERT_EQUAL_FLOAT(10, interpolate(-5, x, y, 3));
}

void test_interpolate_above_range_clamps_high(void) {
    const float x[] = {0, 1, 2};
    const float y[] = {10, 20, 30};
    TEST_ASSERT_EQUAL_FLOAT(30, interpolate(99, x, y, 3));
}

void test_interpolate_exact_knot(void) {
    const float x[] = {0, 1, 2};
    const float y[] = {10, 20, 30};
    TEST_ASSERT_EQUAL_FLOAT(20, interpolate(1, x, y, 3));
}

void test_interpolate_midpoint(void) {
    const float x[] = {0, 2};
    const float y[] = {0, 100};
    TEST_ASSERT_EQUAL_FLOAT(50, interpolate(1, x, y, 2));
}

// ---------- sensor conversions ----------

void test_temperatureFromResistance_fault_clamps_to_max(void) {
    // resistance >= TEMP_MAX_SAFE_RESISTANCE -> highest temp (safety)
    TEST_ASSERT_EQUAL_FLOAT(150.0f, temperatureFromResistance(TEMP_MAX_SAFE_RESISTANCE));
    TEST_ASSERT_EQUAL_FLOAT(150.0f, temperatureFromResistance(5000.0f));
}

void test_temperatureFromResistance_known_point(void) {
    // 120 ohm maps to 100 C in the table
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, temperatureFromResistance(120.0f));
}

void test_pressureFromVoltage_known_point(void) {
    // 1.77 V maps to 3.5 bar in the table
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 3.5f, pressureFromVoltage(1.77f));
}

// ---------- temperatureBand ----------

void test_temperatureBand_boundaries(void) {
    TEST_ASSERT_EQUAL_UINT8(0, temperatureBand(69.9f));
    TEST_ASSERT_EQUAL_UINT8(1, temperatureBand(70.0f));
    TEST_ASSERT_EQUAL_UINT8(1, temperatureBand(89.9f));
    TEST_ASSERT_EQUAL_UINT8(2, temperatureBand(90.0f));
    TEST_ASSERT_EQUAL_UINT8(2, temperatureBand(109.9f));
    TEST_ASSERT_EQUAL_UINT8(3, temperatureBand(110.0f));
    TEST_ASSERT_EQUAL_UINT8(3, temperatureBand(122.0f));
    TEST_ASSERT_EQUAL_UINT8(4, temperatureBand(122.1f));
}

// ---------- low-pressure alarm hysteresis (§4, 70-89 band: crit 0.9 / rearm 1.1) ----------

void test_lowPressAlarm_trips_below_critical(void) {
    TEST_ASSERT_TRUE(nextLowPressAlarm(false, 80.0f, 0.8f)); // below 0.9 -> trip
}

void test_lowPressAlarm_stays_off_above_critical(void) {
    TEST_ASSERT_FALSE(nextLowPressAlarm(false, 80.0f, 1.0f)); // above 0.9, not tripped
}

void test_lowPressAlarm_holds_in_hysteresis_band(void) {
    // already tripped, pressure recovered to 1.0 (between crit 0.9 and rearm 1.1) -> stays on
    TEST_ASSERT_TRUE(nextLowPressAlarm(true, 80.0f, 1.0f));
}

void test_lowPressAlarm_clears_at_rearm(void) {
    TEST_ASSERT_FALSE(nextLowPressAlarm(true, 80.0f, 1.1f)); // >= rearm -> clear
}

// ---------- evaluateSituation: each branch (valid sensors: R=100, V=2.0) ----------

void test_situation_temp_sensor_err(void) {
    TEST_ASSERT_EQUAL(SIT_TEMP_SENSOR_ERR, evaluateSituation(1500.0f, 2.0f, 130.0f, 1.0f, true));
}

void test_situation_press_sensor_err(void) {
    TEST_ASSERT_EQUAL(SIT_PRESS_SENSOR_ERR, evaluateSituation(100.0f, 0.2f, 80.0f, 3.0f, false));
}

void test_situation_stop_engine(void) {
    TEST_ASSERT_EQUAL(SIT_STOP_ENGINE, evaluateSituation(100.0f, 2.0f, 130.0f, 1.0f, false));
}

void test_situation_low_pressure(void) {
    TEST_ASSERT_EQUAL(SIT_LOW_PRESSURE, evaluateSituation(100.0f, 2.0f, 80.0f, 2.0f, true));
}

void test_situation_overpressure(void) {
    TEST_ASSERT_EQUAL(SIT_OVERPRESSURE, evaluateSituation(100.0f, 2.0f, 80.0f, 10.0f, false));
}

void test_situation_mild_overheat(void) {
    TEST_ASSERT_EQUAL(SIT_MILD_OVERHEAT, evaluateSituation(100.0f, 2.0f, 115.0f, 3.0f, false));
}

void test_situation_regulation(void) {
    TEST_ASSERT_EQUAL(SIT_REGULATION, evaluateSituation(100.0f, 2.0f, 100.0f, 3.0f, false));
}

void test_situation_normal(void) {
    TEST_ASSERT_EQUAL(SIT_NORMAL, evaluateSituation(100.0f, 2.0f, 80.0f, 3.0f, false));
}

void test_situation_cold(void) {
    TEST_ASSERT_EQUAL(SIT_COLD, evaluateSituation(100.0f, 2.0f, 50.0f, 3.0f, false));
}

// ---------- evaluateSituation: priority masking ----------

void test_priority_temp_err_beats_stop_engine(void) {
    // both temp fault and stop-engine conditions true -> temp fault wins
    TEST_ASSERT_EQUAL(SIT_TEMP_SENSOR_ERR, evaluateSituation(1500.0f, 2.0f, 130.0f, 1.0f, true));
}

void test_priority_stop_engine_beats_low_pressure(void) {
    // stop-engine and lowPressAlarm both true -> stop engine wins
    TEST_ASSERT_EQUAL(SIT_STOP_ENGINE, evaluateSituation(100.0f, 2.0f, 130.0f, 1.0f, true));
}

void test_priority_low_pressure_beats_overpressure(void) {
    // lowPressAlarm set and pressure also over ceiling -> low pressure wins (checked first)
    TEST_ASSERT_EQUAL(SIT_LOW_PRESSURE, evaluateSituation(100.0f, 2.0f, 80.0f, 7.0f, true));
}

// ---------- nextFlapState ----------

void test_flap_safety_forces_open(void) {
    TEST_ASSERT_TRUE(nextFlapState(SIT_STOP_ENGINE, 50.0f, false));
    TEST_ASSERT_TRUE(nextFlapState(SIT_LOW_PRESSURE, 50.0f, false));
    TEST_ASSERT_TRUE(nextFlapState(SIT_MILD_OVERHEAT, 50.0f, false));
    TEST_ASSERT_TRUE(nextFlapState(SIT_TEMP_SENSOR_ERR, 50.0f, false));
    TEST_ASSERT_TRUE(nextFlapState(SIT_PRESS_SENSOR_ERR, 50.0f, false));
}

void test_flap_overpressure_holds_state(void) {
    TEST_ASSERT_FALSE(nextFlapState(SIT_OVERPRESSURE, 130.0f, false)); // stays closed
    TEST_ASSERT_TRUE(nextFlapState(SIT_OVERPRESSURE, 50.0f, true));    // stays open
}

void test_flap_hysteresis_opens_at_92(void) {
    TEST_ASSERT_FALSE(nextFlapState(SIT_REGULATION, 91.0f, false)); // not yet
    TEST_ASSERT_TRUE(nextFlapState(SIT_REGULATION, 92.0f, false));  // opens at 92
}

void test_flap_hysteresis_closes_at_85(void) {
    TEST_ASSERT_TRUE(nextFlapState(SIT_NORMAL, 86.0f, true));   // holds open
    TEST_ASSERT_FALSE(nextFlapState(SIT_NORMAL, 85.0f, true));  // closes at 85
}

void test_flap_hysteresis_holds_between(void) {
    TEST_ASSERT_FALSE(nextFlapState(SIT_NORMAL, 88.0f, false)); // stays closed
    TEST_ASSERT_TRUE(nextFlapState(SIT_REGULATION, 90.0f, true)); // stays open
}

// ---------- situationAlert ----------

void test_alert_levels(void) {
    TEST_ASSERT_EQUAL(ALERT_ALARM, situationAlert(SIT_STOP_ENGINE));
    TEST_ASSERT_EQUAL(ALERT_ALARM, situationAlert(SIT_LOW_PRESSURE));
    TEST_ASSERT_EQUAL(ALERT_ALARM, situationAlert(SIT_OVERPRESSURE));
    TEST_ASSERT_EQUAL(ALERT_ALARM, situationAlert(SIT_TEMP_SENSOR_ERR));
    TEST_ASSERT_EQUAL(ALERT_WARN,  situationAlert(SIT_MILD_OVERHEAT));
    TEST_ASSERT_EQUAL(ALERT_OFF,   situationAlert(SIT_REGULATION));
    TEST_ASSERT_EQUAL(ALERT_OFF,   situationAlert(SIT_NORMAL));
    TEST_ASSERT_EQUAL(ALERT_OFF,   situationAlert(SIT_COLD));
}

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_interpolate_below_range_clamps_low);
    RUN_TEST(test_interpolate_above_range_clamps_high);
    RUN_TEST(test_interpolate_exact_knot);
    RUN_TEST(test_interpolate_midpoint);

    RUN_TEST(test_temperatureFromResistance_fault_clamps_to_max);
    RUN_TEST(test_temperatureFromResistance_known_point);
    RUN_TEST(test_pressureFromVoltage_known_point);

    RUN_TEST(test_temperatureBand_boundaries);

    RUN_TEST(test_lowPressAlarm_trips_below_critical);
    RUN_TEST(test_lowPressAlarm_stays_off_above_critical);
    RUN_TEST(test_lowPressAlarm_holds_in_hysteresis_band);
    RUN_TEST(test_lowPressAlarm_clears_at_rearm);

    RUN_TEST(test_situation_temp_sensor_err);
    RUN_TEST(test_situation_press_sensor_err);
    RUN_TEST(test_situation_stop_engine);
    RUN_TEST(test_situation_low_pressure);
    RUN_TEST(test_situation_overpressure);
    RUN_TEST(test_situation_mild_overheat);
    RUN_TEST(test_situation_regulation);
    RUN_TEST(test_situation_normal);
    RUN_TEST(test_situation_cold);

    RUN_TEST(test_priority_temp_err_beats_stop_engine);
    RUN_TEST(test_priority_stop_engine_beats_low_pressure);
    RUN_TEST(test_priority_low_pressure_beats_overpressure);

    RUN_TEST(test_flap_safety_forces_open);
    RUN_TEST(test_flap_overpressure_holds_state);
    RUN_TEST(test_flap_hysteresis_opens_at_92);
    RUN_TEST(test_flap_hysteresis_closes_at_85);
    RUN_TEST(test_flap_hysteresis_holds_between);

    RUN_TEST(test_alert_levels);

    return UNITY_END();
}
