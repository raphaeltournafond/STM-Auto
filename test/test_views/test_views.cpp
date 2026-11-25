#include <unity.h>
#include <string.h>
#include "views.h"

// Native unit tests for the OLED views + carousel (lib/views).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

void test_boot_main_carousel_off(void) {
    ViewState s = viewInit(0);
    TEST_ASSERT_EQUAL_INT(VIEW_MAIN, s.selected);
    TEST_ASSERT_FALSE(s.carousel);
}

void test_tap_cycles_all_views_and_wraps(void) {
    ViewState s = viewInit(0);
    const View expected[] = {VIEW_TEMP, VIEW_PRESS, VIEW_STATS, VIEW_DIAG, VIEW_MAIN};
    for (View e : expected) {
        s = viewNext(s, 0);
        TEST_ASSERT_EQUAL_INT(e, s.selected);
    }
}

void test_manual_mode_never_auto_advances(void) {
    ViewState s = viewInit(0);
    s = viewTick(s, 10 * CAROUSEL_PERIOD_MS, false);
    TEST_ASSERT_EQUAL_INT(VIEW_MAIN, s.selected);
}

void test_carousel_advances_every_period_skipping_diag(void) {
    ViewState s = viewToggleCarousel(viewInit(0), 0);
    TEST_ASSERT_TRUE(s.carousel);
    uint32_t t = 0;
    const View expected[] = {VIEW_TEMP, VIEW_PRESS, VIEW_STATS, VIEW_MAIN, VIEW_TEMP};
    for (View e : expected) {
        s = viewTick(s, t + CAROUSEL_PERIOD_MS - 1, false);   // not yet
        TEST_ASSERT_NOT_EQUAL(e, s.selected);
        t += CAROUSEL_PERIOD_MS;
        s = viewTick(s, t, false);
        TEST_ASSERT_EQUAL_INT(e, s.selected);
    }
}

void test_tap_in_carousel_restarts_timer(void) {
    ViewState s = viewToggleCarousel(viewInit(0), 0);
    s = viewNext(s, 4000);                                     // TEMP, timer restarts at 4 s
    s = viewTick(s, 4000 + CAROUSEL_PERIOD_MS - 1, false);
    TEST_ASSERT_EQUAL_INT(VIEW_TEMP, s.selected);
    s = viewTick(s, 4000 + CAROUSEL_PERIOD_MS, false);
    TEST_ASSERT_EQUAL_INT(VIEW_PRESS, s.selected);
}

void test_carousel_leaves_diag_after_a_period(void) {
    ViewState s = viewInit(0);
    for (int i = 0; i < 4; i++) s = viewNext(s, 0);            // DIAG by hand
    s = viewToggleCarousel(s, 0);
    s = viewTick(s, CAROUSEL_PERIOD_MS, false);
    TEST_ASSERT_EQUAL_INT(VIEW_MAIN, s.selected);
}

void test_pending_alert_forces_main_and_holds_carousel(void) {
    ViewState s = viewToggleCarousel(viewInit(0), 0);
    s = viewNext(s, 0);                                        // TEMP
    TEST_ASSERT_EQUAL_INT(VIEW_MAIN, viewToShow(s, true));     // alert overrides
    TEST_ASSERT_EQUAL_INT(VIEW_TEMP, viewToShow(s, false));
    s = viewTick(s, 3 * CAROUSEL_PERIOD_MS, true);             // held during the alert
    TEST_ASSERT_EQUAL_INT(VIEW_TEMP, s.selected);
    s = viewTick(s, 4 * CAROUSEL_PERIOD_MS - 1, false);        // full period after it
    TEST_ASSERT_EQUAL_INT(VIEW_TEMP, s.selected);
    s = viewTick(s, 4 * CAROUSEL_PERIOD_MS, false);
    TEST_ASSERT_EQUAL_INT(VIEW_PRESS, s.selected);
}

void test_toggle_off_stops(void) {
    ViewState s = viewToggleCarousel(viewInit(0), 0);
    s = viewToggleCarousel(s, 100);
    TEST_ASSERT_FALSE(s.carousel);
    s = viewTick(s, 10 * CAROUSEL_PERIOD_MS, false);
    TEST_ASSERT_EQUAL_INT(VIEW_MAIN, s.selected);
}

void test_numbers_and_titles(void) {
    TEST_ASSERT_EQUAL_UINT8(1, viewNumber(VIEW_MAIN));
    TEST_ASSERT_EQUAL_UINT8(VIEW_COUNT, viewNumber(VIEW_DIAG));
    for (int v = 0; v < VIEW_COUNT; v++) {
        // title + " AUTO 5/5" must fit the 21-char header line
        TEST_ASSERT_LESS_OR_EQUAL_INT(12, (int)strlen(viewTitle((View)v)));
    }
    TEST_ASSERT_FALSE(viewInCarousel(VIEW_DIAG));
    TEST_ASSERT_TRUE(viewInCarousel(VIEW_MAIN));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_boot_main_carousel_off);
    RUN_TEST(test_tap_cycles_all_views_and_wraps);
    RUN_TEST(test_manual_mode_never_auto_advances);
    RUN_TEST(test_carousel_advances_every_period_skipping_diag);
    RUN_TEST(test_tap_in_carousel_restarts_timer);
    RUN_TEST(test_carousel_leaves_diag_after_a_period);
    RUN_TEST(test_pending_alert_forces_main_and_holds_carousel);
    RUN_TEST(test_toggle_off_stops);
    RUN_TEST(test_numbers_and_titles);
    return UNITY_END();
}
