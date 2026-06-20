#pragma once
#include <stdint.h>
#include "decision.h" // Situation

// MP3 voice prompts (DFPlayer Mini over USART1), DECISION_MATRIX §7.
// Pure / unit-testable: the situation -> track mapping and the DFPlayer command
// frame builder. The actual UART write lives in src/main.cpp.

constexpr uint8_t DF_FRAME_LEN      = 10;
constexpr uint8_t DF_CMD_PLAY_INDEX = 0x03; // play the Nth file on the card
constexpr uint8_t DF_CMD_SET_VOLUME = 0x06; // volume 0..30
constexpr uint8_t DF_CMD_STOP       = 0x16; // stop playback

// Voice file index for a situation (§7); 0 = no voice.
//   STOP_ENGINE->4  LOW_PRESSURE->3  MILD_OVERHEAT->2  sensor faults->5
// (track 1 is the "system ready" jingle, played by the INIT phase — not here.)
int voiceTrack(Situation sit);

// Build a DFPlayer Mini command frame (10 bytes incl. checksum) into out[10].
void dfplayerFrame(uint8_t cmd, uint16_t param, uint8_t* out);
