#include "lifecycle.h"

bool tempSensorPlausible(float resistance) {
    return resistance >= TEMP_PLAUSIBLE_MIN_RESISTANCE && resistance < TEMP_PLAUSIBLE_MAX_RESISTANCE;
}

bool pressSensorPlausible(float vPressure) {
    return vPressure >= PRESS_MIN_SAFE_VOLTAGE; // disconnected reads ~0 V
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
