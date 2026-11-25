#include "run.h"

RunState runStateInit() {
    RunState s;
    s.flapOpen = false;
    s.latches  = {false, false, false};
    s.filter   = {false, SIT_COLD, SIT_COLD, 0};
    s.ack      = {false, SIT_COLD};
    return s;
}

RunState runTick(RunState prev, float resistance, float vPressure, bool ackEdge,
                 RunResult* out) {
    RunState s = prev;
    const float temperature = temperatureFromResistance(resistance);
    const float pressure    = pressureFromVoltage(vPressure);

    const bool sensorsOk = !tempSensorFault(resistance) && !pressSensorFault(vPressure);
    s.latches = nextAlarmLatches(s.latches, sensorsOk, temperature, pressure);
    const Situation raw = evaluateSituation(resistance, vPressure, temperature, pressure, s.latches);
    s.filter = nextSituationFilter(s.filter, raw, SITUATION_CONFIRM_TICKS);
    const Situation sit = s.filter.stable;

    s.ack      = updateAck(s.ack, sit, ackEdge); // every tick: auto-return / re-arm
    s.flapOpen = nextFlapState(sit, temperature, s.flapOpen);

    if (out) {
        out->temperature = temperature;
        out->pressure    = pressure;
        out->raw         = raw;
        out->sit         = sit;
        out->alertMask   = activeAlertMask(resistance, vPressure, temperature, pressure, s.latches);
    }
    return s;
}
