# Voice prompts (DFPlayer Mini)

MP3 voice prompts played over USART1, mapped per `DECISION_MATRIX.md` §7
(`lib/voice/voice.cpp`, `voiceTrack()`). Played by physical file index, so names
**and copy order** both matter.

| File | Track | Trigger | Text (ja) |
|------|-------|---------|-----------|
| `0001.mp3` | 1 | INIT OK / system ready | システム、起動完了。 |
| `0002.mp3` | 2 | `SIT_MILD_OVERHEAT` | 警告。エンジン油温が上昇しています。 |
| `0003.mp3` | 3 | `SIT_LOW_PRESSURE` | 警告。エンジン油圧が低下しています。 |
| `0004.mp3` | 4 | `SIT_STOP_ENGINE` | 緊急警告！エンジンを直ちに停止してください！ |
| `0005.mp3` | 5 | sensor fault / `INIT_FAIL` | センサー異常を検出しました。 |

Generated with Microsoft Edge neural TTS (`ja-JP-NanamiNeural`).

## SD card setup

1. Format the card **FAT32** (2–8 GB recommended).
2. Copy the files to the **root** in numeric order (`0001` → `0005`) on a freshly
   formatted card so FAT directory order matches the index.
3. Keep MP3s ≤128 kbps CBR — DFPlayer is unreliable with VBR / high bitrates.
