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
constexpr uint8_t DF_CMD_QUERY_SD_FILES = 0x48; // reply param = number of files on the SD card
constexpr uint8_t DF_CMD_ERROR      = 0x40; // module error reply (e.g. no card)

// Voice file index for a situation (§7); 0 = no voice.
//   STOP_ENGINE->4  LOW_PRESSURE->3  MILD_OVERHEAT->2  sensor faults->5
// (track 1 is the "system ready" jingle, played by the INIT phase — not here.)
int voiceTrack(Situation sit);

// Track to play when `sit` becomes the active situation: its voiceTrack(), or 0
// while the alert channel is acknowledged (§6: ack mutes sound + voice).
int voiceTrackOnEntry(Situation sit, bool acked);

// Build a DFPlayer Mini command frame (10 bytes incl. checksum) into out[10].
void dfplayerFrame(uint8_t cmd, uint16_t param, uint8_t* out);

// Incremental parser for frames sent back by the DFPlayer (same 10-byte layout,
// e.g. the reply to DF_CMD_QUERY_SD_FILES). Resynchronises on 0x7E, rejects bad
// length / end marker / checksum.
struct DfRxParser {
    uint8_t buf[DF_FRAME_LEN];
    uint8_t len;
};

// Feed one received byte. Returns true when a complete valid frame ended on this
// byte, with its command and parameter in *cmd / *param.
bool dfRxPush(DfRxParser* p, uint8_t byte, uint8_t* cmd, uint16_t* param);
