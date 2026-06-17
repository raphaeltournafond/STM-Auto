#include <unity.h>
#include "ack.h"

// Native unit tests for the acknowledge state machine (lib/ack).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

static AckState fresh() { return AckState{false, SIT_COLD}; }

void test_no_alert_stays_not_acked(void) {
    AckState s = updateAck(fresh(), SIT_NORMAL, false);
    TEST_ASSERT_FALSE(s.acked);
}

void test_press_with_no_alert_does_nothing(void) {
    // Can't acknowledge when there is nothing to acknowledge.
    AckState s = updateAck(fresh(), SIT_NORMAL, true);
    TEST_ASSERT_FALSE(s.acked);
}

void test_press_during_alert_acknowledges(void) {
    AckState s = updateAck(fresh(), SIT_LOW_PRESSURE, true);
    TEST_ASSERT_TRUE(s.acked);
    TEST_ASSERT_EQUAL(SIT_LOW_PRESSURE, s.ackedSituation);
}

void test_acked_holds_while_condition_persists(void) {
    AckState s = updateAck(AckState{true, SIT_LOW_PRESSURE}, SIT_LOW_PRESSURE, false);
    TEST_ASSERT_TRUE(s.acked);
}

void test_auto_return_when_condition_clears(void) {
    AckState s = updateAck(AckState{true, SIT_LOW_PRESSURE}, SIT_NORMAL, false);
    TEST_ASSERT_FALSE(s.acked);
}

void test_higher_priority_rearms_sound(void) {
    // Acked low-pressure (prio 3), then stop-engine (prio 2, higher) appears.
    AckState s = updateAck(AckState{true, SIT_LOW_PRESSURE}, SIT_STOP_ENGINE, false);
    TEST_ASSERT_FALSE(s.acked); // sound re-armed
}

void test_lower_priority_keeps_ack(void) {
    // Acked stop-engine (prio 2), then it improves to overpressure (prio 4, lower).
    AckState s = updateAck(AckState{true, SIT_STOP_ENGINE}, SIT_OVERPRESSURE, false);
    TEST_ASSERT_TRUE(s.acked); // stays muted
}

void test_rearm_then_reack_same_tick(void) {
    // Higher-priority alert appears AND the button is pressed in the same tick.
    AckState s = updateAck(AckState{true, SIT_LOW_PRESSURE}, SIT_STOP_ENGINE, true);
    TEST_ASSERT_TRUE(s.acked);
    TEST_ASSERT_EQUAL(SIT_STOP_ENGINE, s.ackedSituation);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_alert_stays_not_acked);
    RUN_TEST(test_press_with_no_alert_does_nothing);
    RUN_TEST(test_press_during_alert_acknowledges);
    RUN_TEST(test_acked_holds_while_condition_persists);
    RUN_TEST(test_auto_return_when_condition_clears);
    RUN_TEST(test_higher_priority_rearms_sound);
    RUN_TEST(test_lower_priority_keeps_ack);
    RUN_TEST(test_rearm_then_reack_same_tick);
    return UNITY_END();
}
