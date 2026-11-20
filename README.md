# Watch Assistant (ESP-IDF)

ESP-IDF firmware for the ESP32-S3 1.8" AMOLED phoneshop voice watch. It is the watch half of [voice-assistant-server](https://github.com/boga94uk/voice-assistant-server). How they run together, and what to write for each placeholder, is in [SETUP.md](SETUP.md).

```
YOUR_FIRMWARE_DIR\
  firmware\     what we flash (idf.py -p YOUR_SERIAL_PORT flash)
  tts\          Pocket TTS sidecar on 127.0.0.1:8090
  tools\        secret import + hardware test helpers
  docs\         architecture and capability inventory
  secrets\      drop-in API keys (never commit)
  reference\    previous Arduino firmware and shop protocol
```

**Hardware:** ESP32-S3-N16R8, SH8601 368×448 QSPI AMOLED, FT3168, ES8311, AXP2101, QMI8658, PCF85063, TCA9554, SDMMC 1-bit.

**IDF:** 5.5.1 at `C:\Espressif\frameworks\esp-idf-v5.5.1` (Python env `idf5.5_py3.10_env`).

## Build and flash

```powershell
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.5_py3.10_env"
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5.1"
cmd /c "C:\Espressif\frameworks\esp-idf-v5.5.1\export.bat && cd /d YOUR_FIRMWARE_DIR\firmware && idf.py -p YOUR_SERIAL_PORT flash"
```

Host tests (no hardware):

```powershell
python YOUR_FIRMWARE_DIR\firmware\tests\test_host.py
```

Shop server: VoiceAssistantServer on :8080, tunnel `https://your-server.example.com`. Offline TTS: `YOUR_FIRMWARE_DIR\tts\start-tts.bat`.

Keys: drop `openai.key` or `xai.key` in `YOUR_FIRMWARE_DIR\secrets\` (see `secrets/INSTRUCTIONS.txt`).

## Tasks and cores (as implemented)

| Task | Core | Role |
|---|---|---|
| `ui` / LVGL | 1 | Display, galaxy, touch |
| `capture` | 1 | I2S mic → WAV in PSRAM |
| `app` | 0 | State machine |
| `upload` / `exec` | 1 | HTTPS to the shop |
| `input` | 0 | GPIO0 + expander button |
| `dns` | 0 | Captive portal only |

See `docs/CAPABILITIES.md` for implemented vs hardware-verified vs planned.
