#include "views.h"

ViewState viewInit(uint32_t nowMs) {
    return {VIEW_MAIN, false, nowMs};
}

static View nextView(View v) {
    return (View)((v + 1) % VIEW_COUNT);
}

ViewState viewNext(ViewState s, uint32_t nowMs) {
    s.selected     = nextView(s.selected);
    s.lastSwitchMs = nowMs;
    return s;
}

ViewState viewToggleCarousel(ViewState s, uint32_t nowMs) {
    s.carousel     = !s.carousel;
    s.lastSwitchMs = nowMs;
    return s;
}

ViewState viewTick(ViewState s, uint32_t nowMs, bool forcedMain) {
    if (!s.carousel) return s;
    if (forcedMain) {                 // alert on screen: hold the rotation
        s.lastSwitchMs = nowMs;
        return s;
    }
    if (nowMs - s.lastSwitchMs < CAROUSEL_PERIOD_MS) return s;
    View v = nextView(s.selected);
    while (!viewInCarousel(v)) v = nextView(v);
    s.selected     = v;
    s.lastSwitchMs = nowMs;
    return s;
}

View viewToShow(ViewState s, bool forcedMain) {
    return forcedMain ? VIEW_MAIN : s.selected;
}

bool viewInCarousel(View v) {
    return v != VIEW_DIAG && v < VIEW_COUNT;
}

uint8_t viewNumber(View v) {
    return (uint8_t)v + 1;
}

const char* viewTitle(View v) {
    switch (v) {
        case VIEW_MAIN:  return "MAIN";
        case VIEW_TEMP:  return "OIL TEMP";
        case VIEW_PRESS: return "OIL PRESSURE";
        case VIEW_STATS: return "SESSION";
        case VIEW_DIAG:  return "DIAG";
        default:         return "?";
    }
}
