# Capability inventory

## Implemented in this IDF firmware

### Hardware drivers
- SH8601 QSPI AMOLED 368×448, brightness opcode 0x51
- FT3168 touch (I2C 0x38)
- TCA9554: reset pulse, bottom button on pin 4
- ES8311 16 kHz / 16-bit record and play, PA GPIO46
- AXP2101 battery / charge / USB
- PCF85063 RTC applied at boot; SNTP after STA
- QMI8658 shake-to-wake
- SDMMC 1-bit (CLK=2, CMD=1, D0=3)

### Voice loop
- Hold top to record (auto-stop at 60 s)
- WAV validated (PCM mono 16 kHz 16-bit; extra chunks such as ffmpeg LIST are skipped)
- Durable queue on `/sdcard/queue` (RAM fallback if no card or SD write fails)
- New recordings do not overwrite pending files
- POST `/transcribe`, HMAC headers, optional conversation id
- Stages: recording/sending (vivid red) → processing (orange) → receiving (green)
- Types say / ask / confirm; confirm POST `/execute`
- Interaction ID: cancel stops net/play and drops late results
- Missing TTS: show text and complete (does not wait forever)
- Offline: queue on SD, retry on Wi-Fi or top click

### UI
- Idle galaxy, recording ring, send/process/receive orbs
- Say: green tick; ask: yellow `?`; confirm: tick + mic + red tick + X
- Vivid green playback bar under the reply text
- Reply text in a large-font scroll box that follows playback
- Settings: brightness, volume, mic gain, open AP
- Sleep clock, dim, blank
- Tap power: screensaver (ignore touch/IMU 5 s)
- Hold power 3 s: power-off dialog (AXP2101 softoff)

### Provisioning and OTA
- Captive AP `ESP Assistant`, DNS hijack to 192.168.4.1
- Portal: Wi-Fi, server URL/secret, levels, SD files, OTA
- Dual 3 MB OTA slots + rollback after failed boot

### Storage
- NVS: SSID, URL, secret, volume, gain, brightness, optional Root CA
- NVS kept across app flash

## Hardware-verified (on the shop watch, COM8)

- Panel, galaxy, brightness slider, settings layout
- Record / upload / confirm / execute against VoiceAssistantServer
- Captive portal load (after AP IPv4 + DHCP 114)
- Battery, Wi-Fi bars, sleep/wake
- RAM/TLS uploads after internal-heap fixes

Re-verify after each flash: record a short command end-to-end.

## Planned / not in this binary

- Heartbeat POST `/status`
- SPIFFS click/boot sounds in the idle path
- `esp_https_ota` from the shop server (portal OTA is implemented)
- Host C unit tests linked against IDF (Python host tests exist)

## Host tests (no hardware)

```
python firmware/tests/test_host.py
```

Covers WAV header layout, HMAC-SHA256 compatibility, reply JSON type/command, and cancel / queue / confirm / failed-TTS decisions. I2C and display stay in `tools/hwtest.py`.
