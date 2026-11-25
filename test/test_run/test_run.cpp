#include <unity.h>
#include "run.h"

// Native scenario tests for the RUN tick (lib/run): replay sensor sequences
// through the full decision pipeline (latches -> matrix -> debounce -> ack -> flap).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

// ----- Sensor stimuli: invert the scale tables by bisection -----

static float resistanceFor(float tempC) {   // temperature falls as R rises
    float lo = 32.0f, hi = 925.0f;
    for (int i = 0; i < 60; i++) {
        float mid = 0.5f * (lo + hi);
        if (temperatureFromResistance(mid) > tempC) lo = mid; else hi = mid;
    }
    return 0.5f * (lo + hi);
}

static float voltageFor(float bar) {        // pressure rises with V
    float lo = 0.5f, hi = 3.01f;
    for (int i = 0; i < 60; i++) {
        float mid = 0.5f * (lo + hi);
        if (pressureFromVoltage(mid) < bar) lo = mid; else hi = mid;
    }
    return 0.5f * (lo + hi);
}

static const float OPEN_CIRCUIT_R = 72000.0f;

// Run `ticks` identical ticks; returns the last result.
static RunResult hold(RunState* s, float tempC, float bar, int ticks, bool ackFirst = false) {
    RunResult r = {};
    for (int i = 0; i < ticks; i++)
        *s = runTick(*s, resistanceFor(tempC), voltageFor(bar), ackFirst && i == 0, &r);
    return r;
}

// ---------- scenarios ----------

void test_first_tick_primes_without_debounce(void) {
    RunState s = runStateInit();
    RunResult r;
    s = runTick(s, resistanceFor(80.0f), voltageFor(3.0f), false, &r);
    TEST_ASSERT_EQUAL_INT(SIT_NORMAL, r.sit);
    TEST_ASSERT_FLOAT_WITHIN(0.5f, 80.0f, r.temperature);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 3.0f, r.pressure);
    TEST_ASSERT_EQUAL_UINT16(0, r.alertMask);
}

void test_warm_up_drive_flap_hysteresis(void) {
    RunState s = runStateInit();
    RunResult r = hold(&s, 50.0f, 4.0f, 5);
    TEST_ASSERT_EQUAL_INT(SIT_COLD, r.sit);
    TEST_ASSERT_FALSE(s.flapOpen);

    r = hold(&s, 80.0f, 3.0f, 5);
    TEST_ASSERT_EQUAL_INT(SIT_NORMAL, r.sit);
    TEST_ASSERT_FALSE(s.flapOpen);

    r = hold(&s, 91.0f, 2.5f, 5);                 // regulation band, below 92 °C open point
    TEST_ASSERT_EQUAL_INT(SIT_REGULATION, r.sit);
    TEST_ASSERT_FALSE(s.flapOpen);

    hold(&s, 93.0f, 2.5f, 1);                     // flap opens at once (no debounce on hysteresis)
    TEST_ASSERT_TRUE(s.flapOpen);

    r = hold(&s, 87.0f, 2.5f, 5);                 // back in NORMAL band, flap still open
    TEST_ASSERT_EQUAL_INT(SIT_NORMAL, r.sit);
    TEST_ASSERT_TRUE(s.flapOpen);

    hold(&s, 84.0f, 2.5f, 1);                     // closes at <= 85 °C
    TEST_ASSERT_FALSE(s.flapOpen);
}

void test_overheat_debounced_then_flap_forced_open(void) {
    RunState s = runStateInit();
    hold(&s, 100.0f, 2.5f, 3);
    s.flapOpen = false;                           // e.g. just after a fast restart
    RunResult r = hold(&s, 112.0f, 2.5f, SITUATION_CONFIRM_TICKS - 1);
    TEST_ASSERT_EQUAL_INT(SIT_REGULATION, r.sit); // not confirmed yet
    TEST_ASSERT_EQUAL_INT(SIT_MILD_OVERHEAT, r.raw);
    TEST_ASSERT_TRUE(r.alertMask & (1u << SIT_MILD_OVERHEAT));
    r = hold(&s, 112.0f, 2.5f, 1);
    TEST_ASSERT_EQUAL_INT(SIT_MILD_OVERHEAT, r.sit);
    TEST_ASSERT_TRUE(s.flapOpen);

    r = hold(&s, 109.0f, 2.5f, 5);                // hysteresis: still hot above 108 °C
    TEST_ASSERT_EQUAL_INT(SIT_MILD_OVERHEAT, r.sit);
    r = hold(&s, 105.0f, 2.5f, 5);
    TEST_ASSERT_EQUAL_INT(SIT_REGULATION, r.sit);
}

void test_sensor_unplugged_mid_drive(void) {
    RunState s = runStateInit();
    hold(&s, 80.0f, 3.0f, 3);
    RunResult r;
    for (int i = 0; i < SITUATION_CONFIRM_TICKS; i++)
        s = runTick(s, OPEN_CIRCUIT_R, voltageFor(3.0f), false, &r);
    TEST_ASSERT_EQUAL_INT(SIT_TEMP_SENSOR_ERR, r.sit);
    TEST_ASSERT_TRUE(s.flapOpen);                 // fail-safe: favour cooling
    TEST_ASSERT_EQUAL_UINT16(1u << SIT_TEMP_SENSOR_ERR, r.alertMask);
    TEST_ASSERT_FALSE(s.latches.overheat);        // clamped 150 °C must not latch alarms
}

void test_ack_mutes_then_higher_priority_rearms(void) {
    RunState s = runStateInit();
    hold(&s, 112.0f, 2.5f, 5);                    // mild overheat active
    hold(&s, 112.0f, 2.5f, 1, true);              // ACK
    TEST_ASSERT_TRUE(s.ack.acked);

    RunResult r = hold(&s, 112.0f, 0.4f, 5);      // pressure collapses -> LOW PRESSURE
    TEST_ASSERT_EQUAL_INT(SIT_LOW_PRESSURE, r.sit);
    TEST_ASSERT_FALSE(s.ack.acked);               // higher priority re-arms sound
    TEST_ASSERT_TRUE(r.alertMask & (1u << SIT_MILD_OVERHEAT)); // both alarms on screen
    TEST_ASSERT_TRUE(r.alertMask & (1u << SIT_LOW_PRESSURE));

    r = hold(&s, 80.0f, 3.0f, 10);                // all clear -> auto-return
    TEST_ASSERT_EQUAL_INT(SIT_NORMAL, r.sit);
    TEST_ASSERT_FALSE(s.ack.acked);
}

void test_stop_engine(void) {
    RunState s = runStateInit();
    hold(&s, 100.0f, 2.5f, 3);
    RunResult r = hold(&s, 125.0f, 1.0f, SITUATION_CONFIRM_TICKS);
    TEST_ASSERT_EQUAL_INT(SIT_STOP_ENGINE, r.sit);
    TEST_ASSERT_TRUE(s.flapOpen);
    TEST_ASSERT_TRUE(r.alertMask & (1u << SIT_STOP_ENGINE));
}

void test_null_result_pointer_ok(void) {
    RunState s = runStateInit();
    s = runTick(s, resistanceFor(80.0f), voltageFor(3.0f), false, nullptr);
    TEST_ASSERT_EQUAL_INT(SIT_NORMAL, s.filter.stable);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_tick_primes_without_debounce);
    RUN_TEST(test_warm_up_drive_flap_hysteresis);
    RUN_TEST(test_overheat_debounced_then_flap_forced_open);
    RUN_TEST(test_sensor_unplugged_mid_drive);
    RUN_TEST(test_ack_mutes_then_higher_priority_rearms);
    RUN_TEST(test_stop_engine);
    RUN_TEST(test_null_result_pointer_ok);
    return UNITY_END();
}
