#include <unity.h>
#include "voice.h"

// Native unit tests for the MP3 voice mapping + DFPlayer frame (lib/voice).
// Run: pio test -e native

void setUp(void) {}
void tearDown(void) {}

// ---------- situation -> track (§7) ----------

void test_voice_track_mapping(void) {
    TEST_ASSERT_EQUAL_INT(4, voiceTrack(SIT_STOP_ENGINE));
    TEST_ASSERT_EQUAL_INT(3, voiceTrack(SIT_LOW_PRESSURE));
    TEST_ASSERT_EQUAL_INT(2, voiceTrack(SIT_MILD_OVERHEAT));
    TEST_ASSERT_EQUAL_INT(5, voiceTrack(SIT_TEMP_SENSOR_ERR));
    TEST_ASSERT_EQUAL_INT(5, voiceTrack(SIT_PRESS_SENSOR_ERR));
}

void test_voice_no_track_for_silent_situations(void) {
    TEST_ASSERT_EQUAL_INT(0, voiceTrack(SIT_OVERPRESSURE)); // buzzer only, no voice
    TEST_ASSERT_EQUAL_INT(0, voiceTrack(SIT_REGULATION));
    TEST_ASSERT_EQUAL_INT(0, voiceTrack(SIT_NORMAL));
    TEST_ASSERT_EQUAL_INT(0, voiceTrack(SIT_COLD));
}

// ---------- DFPlayer frame builder ----------

void test_frame_play_track_1(void) {
    // 7E FF 06 03 00 00 01 FE F7 EF
    uint8_t expected[DF_FRAME_LEN] = {0x7E, 0xFF, 0x06, 0x03, 0x00, 0x00, 0x01, 0xFE, 0xF7, 0xEF};
    uint8_t out[DF_FRAME_LEN];
    dfplayerFrame(DF_CMD_PLAY_INDEX, 1, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, DF_FRAME_LEN);
}

void test_frame_set_volume_30(void) {
    // 7E FF 06 06 00 00 1E FE D7 EF
    uint8_t expected[DF_FRAME_LEN] = {0x7E, 0xFF, 0x06, 0x06, 0x00, 0x00, 0x1E, 0xFE, 0xD7, 0xEF};
    uint8_t out[DF_FRAME_LEN];
    dfplayerFrame(DF_CMD_SET_VOLUME, 30, out);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, out, DF_FRAME_LEN);
}

void test_frame_markers_and_checksum(void) {
    uint8_t out[DF_FRAME_LEN];
    dfplayerFrame(DF_CMD_PLAY_INDEX, 5, out);
    TEST_ASSERT_EQUAL_HEX8(0x7E, out[0]);          // start marker
    TEST_ASSERT_EQUAL_HEX8(0xEF, out[DF_FRAME_LEN - 1]); // end marker
    // checksum = -(sum of bytes 1..6), so payload sum + 16-bit checksum == 0 mod 0x10000
    uint16_t sum = out[1] + out[2] + out[3] + out[4] + out[5] + out[6];
    uint16_t chk = (uint16_t)((out[7] << 8) | out[8]);
    TEST_ASSERT_EQUAL_HEX16(0, (uint16_t)(sum + chk));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_voice_track_mapping);
    RUN_TEST(test_voice_no_track_for_silent_situations);
    RUN_TEST(test_frame_play_track_1);
    RUN_TEST(test_frame_set_volume_30);
    RUN_TEST(test_frame_markers_and_checksum);
    return UNITY_END();
}
