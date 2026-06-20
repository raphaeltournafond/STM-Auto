#include "ack.h"

// Situations are declared in priority order, so a smaller enum value means a
// higher priority (SIT_TEMP_SENSOR_ERR is highest, SIT_COLD lowest).

AckState updateAck(AckState prev, Situation current, bool ackEdge) {
    AckState s = prev;

    // Condition cleared (no active alert) -> auto-return to not-acknowledged.
    if (situationAlert(current) == ALERT_OFF) {
        s.acked = false;
        return s;
    }

    // A new alert of strictly higher priority re-arms sound despite a prior ack.
    if (s.acked && current < s.ackedSituation) {
        s.acked = false;
    }

    // Button press acknowledges the current alert (mutes sound/voice).
    if (ackEdge) {
        s.acked = true;
        s.ackedSituation = current;
    }

    return s;
}
