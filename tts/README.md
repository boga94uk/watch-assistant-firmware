# Pocket TTS (offline)

Kyutai Pocket TTS, copied into this tree so the watch shop does not depend on the Pocket Voice download folder.

- `start-tts.bat` — run the sidecar on `127.0.0.1:8090`
- `install-tts.ps1` — create `YOUR_FIRMWARE_DIR\tts\.venv` and `pip install pocket-tts`
- Output: 16 kHz mono 16-bit WAV for the ESP32

Until `.venv` exists, `start-tts.ps1` can use the Pocket Voice venv as a fallback.