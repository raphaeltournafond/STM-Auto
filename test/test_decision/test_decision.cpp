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
    // open (>= TEMP_MAX_SAFE_RESISTANCE) or shorted -> highest temp (safety)
    TEST_ASSERT_EQUAL_FLOAT(150.0f, temperatureFromResistance(TEMP_MAX_SAFE_RESISTANCE));
    TEST_ASSERT_EQUAL_FLOAT(150.0f, temperatureFromResistance(72000.0f)); // open circuit
    TEST_ASSERT_EQUAL_FLOAT(150.0f, temperatureFromResistance(0.0f));     // short to GND
}

void test_temperatureFromResistance_cold_reads_table_floor(void) {
    // colder than the table (925 ohm = 40 C) is a valid reading, clamped to 40 C
    TEST_ASSERT_EQUAL_FLOAT(40.0f, temperatureFromResistance(1500.0f));  // ~27 C
    TEST_ASSERT_EQUAL_FLOAT(40.0f, temperatureFromResistance(18000.0f)); // ~-20 C cold soak
}

void test_tempSensorFault_window(void) {
    TEST_ASSERT_FALSE(tempSensorFault(32.0f));      // 150 C
    TEST_ASSERT_FALSE(tempSensorFault(925.0f));     // 40 C
    TEST_ASSERT_FALSE(tempSensorFault(1500.0f));    // ~27 C: was a false fault before
    TEST_ASSERT_FALSE(tempSensorFault(18000.0f));   // ~-20 C cold soak
    TEST_ASSERT_FALSE(tempSensorFault(TEMP_MIN_SAFE_RESISTANCE));       // lower bound valid
    TEST_ASSERT_TRUE(tempSensorFault(TEMP_MIN_SAFE_RESISTANCE - 1.0f)); // shorted
    TEST_ASSERT_TRUE(tempSensorFault(TEMP_MAX_SAFE_RESISTANCE));        // open
    TEST_ASSERT_TRUE(tempSensorFault(72000.0f));                        // open circuit
}

void test_pressSensorFault(void) {
    TEST_ASSERT_FALSE(pressSensorFault(0.5f));                  // 0 bar, sensor present
    TEST_ASSERT_FALSE(pressSensorFault(PRESS_MIN_SAFE_VOLTAGE)); // boundary valid
    TEST_ASSERT_TRUE(pressSensorFault(0.1f));                   // disconnected
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

// ---------- overheat / overpressure latches (§5 hysteresis) ----------

void test_overheat_trips_at_110(void) {
    TEST_ASSERT_FALSE(nextOverheat(false, 109.9f));
    TEST_ASSERT_TRUE(nextOverheat(false, 110.0f));
}

void test_overheat_holds_until_below_108(void) {
    TEST_ASSERT_TRUE(nextOverheat(true, 109.0f));               // noise just under 110: holds
    TEST_ASSERT_TRUE(nextOverheat(true, TEMP_OVERHEAT_CLEAR));  // 108 still holds
    TEST_ASSERT_FALSE(nextOverheat(true, 107.9f));              // clears below 108
}

void test_overpressure_trips_above_ceiling(void) {
    TEST_ASSERT_FALSE(nextOverpressure(false, PRESS_OVER));
    TEST_ASSERT_TRUE(nextOverpressure(false, PRESS_OVER + 0.1f));
}

void test_overpressure_holds_until_clear(void) {
    TEST_ASSERT_TRUE(nextOverpressure(true, PRESS_OVER - 0.1f));
    TEST_ASSERT_FALSE(nextOverpressure(true, PRESS_OVER_CLEAR));
}

void test_alarm_latches_drop_on_sensor_fault(void) {
    AlarmLatches all = {true, true, true};
    AlarmLatches next = nextAlarmLatches(all, false, 115.0f, 7.0f);
    TEST_ASSERT_FALSE(next.lowPress);
    TEST_ASSERT_FALSE(next.overheat);
    TEST_ASSERT_FALSE(next.overpressure);
}

void test_alarm_latches_evolve_when_sensors_ok(void) {
    AlarmLatches none = {false, false, false};
    AlarmLatches next = nextAlarmLatches(none, true, 115.0f, 10.0f); // hot, high pressure
    TEST_ASSERT_FALSE(next.lowPress);
    TEST_ASSERT_TRUE(next.overheat);
    TEST_ASSERT_TRUE(next.overpressure);
    next = nextAlarmLatches(none, true, 80.0f, 0.5f);               // 70-89 band, < 0.9 bar
    TEST_ASSERT_TRUE(next.lowPress);
}

// ---------- evaluateSituation: each branch (valid sensors: R=100, V=2.0) ----------

static const AlarmLatches L_NONE      = {false, false, false};
static const AlarmLatches L_LOWPRESS  = {true,  false, false};
static const AlarmLatches L_OVERHEAT  = {false, true,  false};
static const AlarmLatches L_OVERPRESS = {false, false, true};
static const AlarmLatches L_ALL       = {true,  true,  true};

void test_situation_temp_sensor_err(void) {
    TEST_ASSERT_EQUAL(SIT_TEMP_SENSOR_ERR,
                      evaluateSituation(TEMP_MAX_SAFE_RESISTANCE, 2.0f, 150.0f, 1.0f, L_NONE)); // open
    TEST_ASSERT_EQUAL(SIT_TEMP_SENSOR_ERR,
                      evaluateSituation(0.0f, 2.0f, 150.0f, 3.0f, L_NONE));                     // shorted
}

void test_situation_cold_soak_is_not_sensor_error(void) {
    // Regression: a cold start (1.5-18 kOhm) used to report TEMP SENSOR ERROR.
    TEST_ASSERT_EQUAL(SIT_COLD, evaluateSituation(1500.0f, 2.0f,
                                                  temperatureFromResistance(1500.0f), 3.0f, L_NONE));
    TEST_ASSERT_EQUAL(SIT_COLD, evaluateSituation(18000.0f, 2.0f,
                                                  temperatureFromResistance(18000.0f), 3.0f, L_NONE));
}

void test_situation_press_sensor_err(void) {
    TEST_ASSERT_EQUAL(SIT_PRESS_SENSOR_ERR, evaluateSituation(100.0f, 0.2f, 80.0f, 3.0f, L_NONE));
}

void test_situation_stop_engine(void) {
    TEST_ASSERT_EQUAL(SIT_STOP_ENGINE, evaluateSituation(100.0f, 2.0f, 130.0f, 1.0f, L_NONE));
}

void test_situation_low_pressure(void) {
    TEST_ASSERT_EQUAL(SIT_LOW_PRESSURE, evaluateSituation(100.0f, 2.0f, 80.0f, 2.0f, L_LOWPRESS));
}

void test_situation_overpressure(void) {
    TEST_ASSERT_EQUAL(SIT_OVERPRESSURE, evaluateSituation(100.0f, 2.0f, 80.0f, 10.0f, L_OVERPRESS));
}

void test_situation_mild_overheat(void) {
    TEST_ASSERT_EQUAL(SIT_MILD_OVERHEAT, evaluateSituation(100.0f, 2.0f, 115.0f, 3.0f, L_OVERHEAT));
    // latched: still mild overheat at 109 C (hysteresis), not regulation
    TEST_ASSERT_EQUAL(SIT_MILD_OVERHEAT, evaluateSituation(100.0f, 2.0f, 109.0f, 3.0f, L_OVERHEAT));
}

void test_situation_regulation(void) {
    TEST_ASSERT_EQUAL(SIT_REGULATION, evaluateSituation(100.0f, 2.0f, 100.0f, 3.0f, L_NONE));
}

void test_situation_normal(void) {
    TEST_ASSERT_EQUAL(SIT_NORMAL, evaluateSituation(100.0f, 2.0f, 80.0f, 3.0f, L_NONE));
}

void test_situation_cold(void) {
    TEST_ASSERT_EQUAL(SIT_COLD, evaluateSituation(100.0f, 2.0f, 50.0f, 3.0f, L_NONE));
}

// ---------- evaluateSituation: priority masking ----------

void test_priority_temp_err_beats_stop_engine(void) {
    // both temp fault and stop-engine conditions true -> temp fault wins
    TEST_ASSERT_EQUAL(SIT_TEMP_SENSOR_ERR,
                      evaluateSituation(TEMP_MAX_SAFE_RESISTANCE, 2.0f, 130.0f, 1.0f, L_ALL));
}

void test_priority_stop_engine_beats_low_pressure(void) {
    // stop-engine and lowPress latch both true -> stop engine wins
    TEST_ASSERT_EQUAL(SIT_STOP_ENGINE, evaluateSituation(100.0f, 2.0f, 130.0f, 1.0f, L_ALL));
}

void test_priority_low_pressure_beats_overpressure(void) {
    // lowPress and overpressure latches both set -> low pressure wins (checked first)
    TEST_ASSERT_EQUAL(SIT_LOW_PRESSURE, evaluateSituation(100.0f, 2.0f, 80.0f, 10.0f, L_ALL));
}

void test_priority_overpressure_beats_overheat(void) {
    AlarmLatches l = {false, true, true};
    TEST_ASSERT_EQUAL(SIT_OVERPRESSURE, evaluateSituation(100.0f, 2.0f, 115.0f, 10.0f, l));
}

// ---------- situation debounce filter ----------

static SituationFilter freshFilter() { return {false, SIT_COLD, SIT_COLD, 0}; }

void test_filter_first_tick_taken_as_is(void) {
    SituationFilter f = nextSituationFilter(freshFilter(), SIT_NORMAL, 3);
    TEST_ASSERT_EQUAL(SIT_NORMAL, f.stable);
}

void test_filter_needs_consecutive_ticks(void) {
    SituationFilter f = nextSituationFilter(freshFilter(), SIT_NORMAL, 3);
    f = nextSituationFilter(f, SIT_LOW_PRESSURE, 3);
    TEST_ASSERT_EQUAL(SIT_NORMAL, f.stable);
    f = nextSituationFilter(f, SIT_LOW_PRESSURE, 3);
    TEST_ASSERT_EQUAL(SIT_NORMAL, f.stable);
    f = nextSituationFilter(f, SIT_LOW_PRESSURE, 3);
    TEST_ASSERT_EQUAL(SIT_LOW_PRESSURE, f.stable); // confirmed on the 3rd tick
}

void test_filter_ignores_flicker(void) {
    SituationFilter f = nextSituationFilter(freshFilter(), SIT_REGULATION, 3);
    for (int i = 0; i < 20; i++) {  // alternates every tick, never 3 in a row
        f = nextSituationFilter(f, (i % 2) ? SIT_REGULATION : SIT_MILD_OVERHEAT, 3);
        TEST_ASSERT_EQUAL(SIT_REGULATION, f.stable);
    }
}

void test_filter_restarts_on_different_candidate(void) {
    SituationFilter f = nextSituationFilter(freshFilter(), SIT_NORMAL, 3);
    f = nextSituationFilter(f, SIT_LOW_PRESSURE, 3);
    f = nextSituationFilter(f, SIT_LOW_PRESSURE, 3);
    f = nextSituationFilter(f, SIT_STOP_ENGINE, 3);  // new candidate: count restarts
    TEST_ASSERT_EQUAL(SIT_NORMAL, f.stable);
    f = nextSituationFilter(f, SIT_STOP_ENGINE, 3);
    f = nextSituationFilter(f, SIT_STOP_ENGINE, 3);
    TEST_ASSERT_EQUAL(SIT_STOP_ENGINE, f.stable);
}

void test_filter_confirm_one_is_immediate(void) {
    SituationFilter f = nextSituationFilter(freshFilter(), SIT_NORMAL, 1);
    f = nextSituationFilter(f, SIT_STOP_ENGINE, 1);
    TEST_ASSERT_EQUAL(SIT_STOP_ENGINE, f.stable);
}

// ---------- scenario: noisy temperature around the 110 C threshold ----------

void test_scenario_noise_at_110_does_not_flicker(void) {
    // Pipeline as run by loop(): latches -> evaluateSituation -> filter.
    const float temps[] = {105, 108, 110.2f, 109.6f, 110.4f, 109.1f, 110.0f, 109.8f,
                           108.5f, 110.3f, 109.2f, 109.9f, 110.1f, 108.9f};
    AlarmLatches l = L_NONE;
    SituationFilter f = freshFilter();
    int changes = 0;
    Situation prev = SIT_COLD;
    for (unsigned i = 0; i < sizeof(temps) / sizeof(temps[0]); i++) {
        l = nextAlarmLatches(l, true, temps[i], 3.0f);
        f = nextSituationFilter(f, evaluateSituation(100.0f, 2.0f, temps[i], 3.0f, l),
                                SITUATION_CONFIRM_TICKS);
        if (i > 0 && f.stable != prev) changes++;
        prev = f.stable;
    }
    TEST_ASSERT_EQUAL(SIT_MILD_OVERHEAT, f.stable);
    TEST_ASSERT_EQUAL_INT(1, changes); // REGULATION -> MILD_OVERHEAT once, never back
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
    RUN_TEST(test_temperatureFromResistance_cold_reads_table_floor);
    RUN_TEST(test_tempSensorFault_window);
    RUN_TEST(test_pressSensorFault);
    RUN_TEST(test_temperatureFromResistance_known_point);
    RUN_TEST(test_pressureFromVoltage_known_point);

    RUN_TEST(test_temperatureBand_boundaries);

    RUN_TEST(test_lowPressAlarm_trips_below_critical);
    RUN_TEST(test_lowPressAlarm_stays_off_above_critical);
    RUN_TEST(test_lowPressAlarm_holds_in_hysteresis_band);
    RUN_TEST(test_lowPressAlarm_clears_at_rearm);

    RUN_TEST(test_overheat_trips_at_110);
    RUN_TEST(test_overheat_holds_until_below_108);
    RUN_TEST(test_overpressure_trips_above_ceiling);
    RUN_TEST(test_overpressure_holds_until_clear);
    RUN_TEST(test_alarm_latches_drop_on_sensor_fault);
    RUN_TEST(test_alarm_latches_evolve_when_sensors_ok);

    RUN_TEST(test_situation_temp_sensor_err);
    RUN_TEST(test_situation_cold_soak_is_not_sensor_error);
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
    RUN_TEST(test_priority_overpressure_beats_overheat);

    RUN_TEST(test_filter_first_tick_taken_as_is);
    RUN_TEST(test_filter_needs_consecutive_ticks);
    RUN_TEST(test_filter_ignores_flicker);
    RUN_TEST(test_filter_restarts_on_different_candidate);
    RUN_TEST(test_filter_confirm_one_is_immediate);
    RUN_TEST(test_scenario_noise_at_110_does_not_flicker);

    RUN_TEST(test_flap_safety_forces_open);
    RUN_TEST(test_flap_overpressure_holds_state);
    RUN_TEST(test_flap_hysteresis_opens_at_92);
    RUN_TEST(test_flap_hysteresis_closes_at_85);
    RUN_TEST(test_flap_hysteresis_holds_between);

    RUN_TEST(test_alert_levels);

    return UNITY_END();
}
