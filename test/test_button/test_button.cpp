#include <unity.h>
#include "button.h"

// Native unit tests for the touch-button gestures (lib/button).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

// Hold the button for `ms` (polled every 20 ms like main.cpp), then release.
// Returns the events seen, in order, as letters: A = ACK, T = TAP, L = LONG.
static const char* press(ButtonState* s, uint32_t* now, uint32_t ms, bool ackPending) {
    static char seen[8];
    int n = 0;
    for (uint32_t t = 0; t <= ms; t += 20) {
        ButtonEvent e = buttonUpdate(s, true, *now, ackPending);
        if (e == BTN_ACK)  seen[n++] = 'A';
        if (e == BTN_TAP)  seen[n++] = 'T';
        if (e == BTN_LONG) seen[n++] = 'L';
        *now += 20;
    }
    ButtonEvent e = buttonUpdate(s, false, *now, ackPending);
    if (e == BTN_TAP)  seen[n++] = 'T';
    if (e == BTN_LONG) seen[n++] = 'L';
    *now += 20;
    seen[n] = 0;
    return seen;
}

void test_idle_no_event(void) {
    ButtonState s = buttonInit();
    for (uint32_t t = 0; t < 5000; t += 20)
        TEST_ASSERT_EQUAL_INT(BTN_NONE, buttonUpdate(&s, false, t, false));
}

void test_short_tap_fires_on_release(void) {
    ButtonState s = buttonInit();
    uint32_t now = 1000;
    TEST_ASSERT_EQUAL_STRING("T", press(&s, &now, 200, false));
}

void test_long_press_fires_once_while_held(void) {
    ButtonState s = buttonInit();
    uint32_t now = 1000;
    TEST_ASSERT_EQUAL_STRING("L", press(&s, &now, LONG_PRESS_MS + 2000, false)); // no TAP on release
}

void test_long_press_threshold(void) {
    ButtonState s = buttonInit();
    TEST_ASSERT_EQUAL_INT(BTN_NONE, buttonUpdate(&s, true, 0, false));
    TEST_ASSERT_EQUAL_INT(BTN_NONE, buttonUpdate(&s, true, LONG_PRESS_MS - 1, false));
    TEST_ASSERT_EQUAL_INT(BTN_LONG, buttonUpdate(&s, true, LONG_PRESS_MS, false));
}

void test_press_during_pending_alert_is_ack_only(void) {
    ButtonState s = buttonInit();
    uint32_t now = 1000;
    TEST_ASSERT_EQUAL_STRING("A", press(&s, &now, 100, true));                 // tap -> ACK, no TAP
    TEST_ASSERT_EQUAL_STRING("A", press(&s, &now, LONG_PRESS_MS + 500, true)); // held -> still no LONG
}

void test_ack_fires_on_press_edge(void) {
    ButtonState s = buttonInit();
    TEST_ASSERT_EQUAL_INT(BTN_ACK, buttonUpdate(&s, true, 0, true)); // at once, not on release
}

void test_alert_appearing_mid_press_does_not_ack(void) {
    // The press began with nothing pending: it stays a navigation gesture.
    ButtonState s = buttonInit();
    TEST_ASSERT_EQUAL_INT(BTN_NONE, buttonUpdate(&s, true, 0, false));
    TEST_ASSERT_EQUAL_INT(BTN_NONE, buttonUpdate(&s, true, 100, true));
    TEST_ASSERT_EQUAL_INT(BTN_TAP,  buttonUpdate(&s, false, 200, true));
}

void test_consecutive_gestures(void) {
    ButtonState s = buttonInit();
    uint32_t now = 0;
    TEST_ASSERT_EQUAL_STRING("T", press(&s, &now, 100, false));
    TEST_ASSERT_EQUAL_STRING("L", press(&s, &now, LONG_PRESS_MS, false));
    TEST_ASSERT_EQUAL_STRING("A", press(&s, &now, 100, true));
    TEST_ASSERT_EQUAL_STRING("T", press(&s, &now, 100, false));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_idle_no_event);
    RUN_TEST(test_short_tap_fires_on_release);
    RUN_TEST(test_long_press_fires_once_while_held);
    RUN_TEST(test_long_press_threshold);
    RUN_TEST(test_press_during_pending_alert_is_ack_only);
    RUN_TEST(test_ack_fires_on_press_edge);
    RUN_TEST(test_alert_appearing_mid_press_does_not_ack);
    RUN_TEST(test_consecutive_gestures);
    return UNITY_END();
}
