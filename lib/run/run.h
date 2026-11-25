#pragma once
#include <stdint.h>
#include "decision.h"
#include "ack.h"

// One RUN-phase decision tick (DECISION_MATRIX §3-§6), chained from the pure
// helpers: conversions -> alarm latches -> priority matrix -> debounce -> ack ->
// flap. Pure / unit-testable so whole drive scenarios can be replayed natively;
// src/main.cpp samples the sensors, calls runTick() and drives the outputs.

// Everything the RUN phase carries from one tick to the next.
struct RunState {
    bool flapOpen;          // flap hysteresis state (true = open)
    AlarmLatches latches;   // low-press / overheat / overpressure (§4/§5)
    SituationFilter filter; // debounced situation (.stable drives the outputs)
    AckState ack;           // acknowledge state (§6)
};

// State at the start of RUN: flap closed, nothing latched or acknowledged.
RunState runStateInit();

// What a tick decided, for the outputs (display, LED, sound, telemetry).
struct RunResult {
    float temperature;  // °C (clamped to the max on a temp sensor fault)
    float pressure;     // bar
    Situation raw;      // undebounced situation this tick
    Situation sit;      // debounced situation (== next state's filter.stable)
    uint16_t alertMask; // every active alert (activeAlertMask)
};

// Advance one tick from the averaged raw readings. ackEdge = touch-button rising
// edge seen since the previous tick. `out` may be null.
RunState runTick(RunState prev, float resistance, float vPressure, bool ackEdge,
                 RunResult* out);
