#include "voice.h"

int voiceTrack(Situation sit) {
    switch (sit) {
        case SIT_STOP_ENGINE:      return 4; // "surchauffe, coupez le moteur"
        case SIT_LOW_PRESSURE:     return 3; // "pression d'huile basse"
        case SIT_MILD_OVERHEAT:    return 2; // "temperature elevee"
        case SIT_TEMP_SENSOR_ERR:
        case SIT_PRESS_SENSOR_ERR: return 5; // "defaut capteur"
        default:                   return 0; // overpressure / normal bands: no voice
    }
}

void dfplayerFrame(uint8_t cmd, uint16_t param, uint8_t* out) {
    out[0] = 0x7E;                      // start
    out[1] = 0xFF;                      // version
    out[2] = 0x06;                      // number of bytes (cmd..paramL)
    out[3] = cmd;
    out[4] = 0x00;                      // no command feedback
    out[5] = (uint8_t)(param >> 8);     // param high
    out[6] = (uint8_t)(param & 0xFF);   // param low
    uint16_t sum = out[1] + out[2] + out[3] + out[4] + out[5] + out[6];
    uint16_t chk = (uint16_t)(0u - sum); // two's-complement checksum
    out[7] = (uint8_t)(chk >> 8);
    out[8] = (uint8_t)(chk & 0xFF);
    out[9] = 0xEF;                      // end
}
