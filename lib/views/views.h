#pragma once
#include <stdint.h>

// OLED views + carousel (DECISION_MATRIX §10). Pure / unit-testable; drawing
// lives in src/main.cpp.

enum View {
    VIEW_MAIN,   // status line + temperature and pressure gauges (alarm screen)
    VIEW_TEMP,   // oil temperature + ~10 min trend graph
    VIEW_PRESS,  // oil pressure + ~10 min trend graph + current low-pressure floor
    VIEW_STATS,  // session min / max / time hot / uptime
    VIEW_DIAG,   // raw Ω / V, situation, flap, ack (bench; not in the carousel)
    VIEW_COUNT,
};

// Carousel: each view stays this long before the next one.
constexpr uint32_t CAROUSEL_PERIOD_MS = 5000;

struct ViewState {
    View selected;         // view chosen by the operator / the carousel
    bool carousel;         // auto-advance every CAROUSEL_PERIOD_MS
    uint32_t lastSwitchMs; // when `selected` last changed (carousel timer)
};

// At boot: main view, carousel off.
ViewState viewInit(uint32_t nowMs);

// Short tap: next view (all views, wraps). Restarts the carousel timer.
ViewState viewNext(ViewState s, uint32_t nowMs);

// Long press: carousel on/off. Turning it on starts the timer from now.
ViewState viewToggleCarousel(ViewState s, uint32_t nowMs);

// Per tick: in carousel mode advance to the next carousel view once the period
// has elapsed. forcedMain (alert awaiting ACK) holds the carousel: the timer
// restarts so the view after the alert gets its full period.
ViewState viewTick(ViewState s, uint32_t nowMs, bool forcedMain);

// View actually drawn: an alert awaiting ACK always forces VIEW_MAIN.
View viewToShow(ViewState s, bool forcedMain);

bool viewInCarousel(View v);       // DIAG is excluded
uint8_t viewNumber(View v);        // 1-based, for the "2/5" page indicator
const char* viewTitle(View v);
