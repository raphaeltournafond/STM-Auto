#pragma once
#include <stdint.h>

// Single touch button (PB5) gestures, DECISION_MATRIX §6/§10. Pure / unit-testable;
// the pin read lives in src/main.cpp (polled every 20 ms).
//
// While an alert is waiting for acknowledgement, a press is an ACK, fired at
// once on the press edge (no wait for the release) and nothing else. Otherwise a
// short tap (released before LONG_PRESS_MS) means "next view", and holding the
// button LONG_PRESS_MS means "toggle carousel" (fired while still held, so the
// operator gets feedback without having to guess when to let go).

constexpr uint32_t LONG_PRESS_MS = 1500;

enum ButtonEvent {
    BTN_NONE,
    BTN_ACK,  // press edge while an alert awaits acknowledgement
    BTN_TAP,  // short press, released
    BTN_LONG, // held LONG_PRESS_MS (fires once per press)
};

struct ButtonState {
    bool down;         // button currently held
    bool consumed;     // this press was used as an ACK: no TAP/LONG for it
    bool longFired;    // LONG already reported for this press
    uint32_t downAtMs; // press start
};

ButtonState buttonInit();

// Feed the current level (true = touched) at time nowMs. ackPending = an alert
// is active and not yet acknowledged. Returns the gesture completed now, if any.
ButtonEvent buttonUpdate(ButtonState* s, bool level, uint32_t nowMs, bool ackPending);
