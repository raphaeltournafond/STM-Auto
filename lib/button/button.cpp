#include "button.h"

ButtonState buttonInit() {
    return {false, false, false, 0};
}

ButtonEvent buttonUpdate(ButtonState* s, bool level, uint32_t nowMs, bool ackPending) {
    if (level && !s->down) {                       // press edge
        s->down      = true;
        s->downAtMs  = nowMs;
        s->longFired = false;
        s->consumed  = ackPending;
        return ackPending ? BTN_ACK : BTN_NONE;
    }
    if (level) {                                   // still held
        if (!s->consumed && !s->longFired && nowMs - s->downAtMs >= LONG_PRESS_MS) {
            s->longFired = true;
            return BTN_LONG;
        }
        return BTN_NONE;
    }
    if (s->down) {                                 // release edge
        s->down = false;
        if (!s->consumed && !s->longFired) return BTN_TAP;
    }
    return BTN_NONE;
}
