#pragma once
#include "decision.h" // Situation, situationAlert

// Acknowledge state machine (DECISION_MATRIX §6). Pure / unit-testable; the
// button read + edge detection live in src/main.cpp.
//
// Acknowledging mutes the sound/voice channel but keeps the LED + screen while
// the condition persists. A new higher-priority alert re-arms sound despite a
// prior ack. When no alert is active the state auto-returns to not-acknowledged.

struct AckState {
    bool acked;                 // is the current alert acknowledged (sound muted)?
    Situation ackedSituation;   // the situation that was acknowledged
};

// ackEdge = rising edge of the touch button this tick. Returns the next state.
AckState updateAck(AckState prev, Situation current, bool ackEdge);
