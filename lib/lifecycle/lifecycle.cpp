#include "lifecycle.h"

bool tempSensorPlausible(float resistance) {
    return !tempSensorFault(resistance);
}

bool pressSensorPlausible(float vPressure) {
    return !pressSensorFault(vPressure); // disconnected reads ~0 V
}

const char* initResultLabel(InitResult r) {
    switch (r) {
        case INIT_OK:          return "INIT OK";
        case INIT_FAIL_OLED:   return "OLED FAIL";
        case INIT_FAIL_MP3_SD: return "MP3 SD FAIL";
        case INIT_FAIL_TEMP:   return "TEMP SENS FAIL";
        case INIT_FAIL_PRESS:  return "PRESS SENS FAIL";
        default:               return "INIT FAIL";
    }
}

int initFailVoiceTrack(InitResult r) {
    return (r == INIT_OK) ? 0 : 5; // §7: 0005 "defaut" on any INIT_FAIL
}

uint8_t initFailCode(InitResult r) {
    switch (r) {
        case INIT_FAIL_OLED:   return 1;
        case INIT_FAIL_MP3_SD: return 2;
        case INIT_FAIL_TEMP:   return 3;
        case INIT_FAIL_PRESS:  return 4;
        default:               return 0;
    }
}

bool initFailPatternOn(uint8_t code, uint32_t t) {
    if (code == 0) return false;
    const uint32_t pulse  = INIT_FAIL_PULSE_ON + INIT_FAIL_PULSE_OFF;
    const uint32_t period = code * pulse + INIT_FAIL_PAUSE;
    const uint32_t phase  = t % period;
    if (phase >= code * pulse) return false;     // pause between codes
    return (phase % pulse) < INIT_FAIL_PULSE_ON; // inside a pulse
}
