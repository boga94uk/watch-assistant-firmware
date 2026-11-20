# Watch firmware — build, flash, run

ESP-IDF firmware for the ESP32-S3 1.8" AMOLED phoneshop voice watch. Pairing with the shop server, placeholders, and the full run steps are in [SETUP.md](../SETUP.md).

**Hardware:** ESP32-S3-N16R8, SH8601 368×448 QSPI AMOLED, ES8311, AXP2101, QMI8658, PCF85063, TCA9554, microSD.

**IDF:** 5.5.x (this tree was built with 5.5.1).

## Prebuilt binary

`dist/watch_assistant.bin` is the last compiled app image (app partition at `0x10000`). Prefer a full `idf.py flash` so bootloader + partition table stay in sync.

## Build and flash (Windows)

```powershell
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\python_env\idf5.5_py3.10_env"
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5.1"
cmd /c "C:\Espressif\frameworks\esp-idf-v5.5.1\export.bat && cd /d YOUR_FIRMWARE_DIR\firmware && idf.py -p YOUR_SERIAL_PORT flash"
```

`YOUR_SERIAL_PORT` is the watch USB serial port, such as `COM8`. `YOUR_FIRMWARE_DIR` is the clone of this repo. See [SETUP.md](../SETUP.md).

First boot: hold the bottom (power) button until **Setup** appears, join Wi-Fi `ESP Assistant`, open `192.168.4.1`, set shop Wi-Fi and the server URL (`https://your-server.example.com` or your LAN IP).

## Shop stack the watch talks to

The watch POSTs signed WAV to the Voice Assistant Server (`:8080`), which uses Pocket TTS on `127.0.0.1:8090`.

On the shop PC, double-click **Shop stack** on the desktop (or run `YOUR_FIRMWARE_DIR\watch-shop-stack.bat`). That watchdog keeps server, TTS, and the Cloudflare tunnel up.

## Host tests (no hardware)

```powershell
python tests/test_host.py
```

## Buttons (short)

- **Top:** hold to talk, tap to confirm / send oldest queued clip, triple-click for settings
- **Bottom / power:** tap sleep or cancel; hold 3 seconds then confirm to power off
