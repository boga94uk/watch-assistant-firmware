# Firmware architecture (ESP-IDF)

```
core 1  ui / LVGL          screens, galaxy, touch
core 1  capture            ES8311 I2S record into PSRAM
core 1  upload / exec      HTTPS (pinned with audio DMA)
core 0  app                state machine, interaction IDs
core 0  input              GPIO0 hold/click, bottom button
core 0  wifi / httpd / dns STA plus optional captive AP
```

Events go through a FreeRTOS queue (`app_evt_t` with `ix`). Late results from a cancelled interaction are dropped.

`HOLD record` → WAV → `recq` (SD FIFO, RAM fallback) → POST `/transcribe` → processing → GET TTS → say/ask/confirm.

## Components (`firmware/components`)

| Component | Owns |
|---|---|
| `board` | Pins, I2C, TCA9554, AXP2101, IMU, RTC time |
| `display` | QSPI SH8601 + FT3168 poll |
| `ui` | LVGL 8.4, galaxy, settings overlay |
| `audio` | ES8311, 16 kHz WAV, playback |
| `sd` | SDMMC 1-bit mount `/sdcard` |
| `queue` | Durable recording FIFO (`recq`) |
| `input` | Debounce, hold-to-record, triple-click, 5 s power |
| `net` | Wi-Fi STA, HMAC, `/transcribe` `/execute`, cancel |
| `app` | State machine + interaction IDs |
| `provision` | Captive AP, portal, OTA, SD browser |
| `settings` | NVS Wi-Fi, URL, secret, levels, optional Root CA |
| `console_hw` | USB console `test` / `screen` |

`main` starts NVS, board, SD/queue, audio, UI, input, app, then STA or provision AP.

## Pins

See `firmware/components/board/include/board_pins.h`.

## Flash map (16 MB)

```
nvs       0x9000    20 KB
otadata   0xE000     8 KB
ota_0     0x10000    3 MB
ota_1     0x310000   3 MB
storage   0x610000   ~9.9 MB  SPIFFS (boot/click wavs)
```

Rollback is enabled. A new OTA image stays pending-verify until `esp_ota_mark_app_valid_cancel_rollback()` after a successful boot. A crash before that rolls back to the previous slot.

## Bring-up order

1. I2C, AXP2101, TCA9554 reset
2. SD + recq
3. Audio (I2S DMA before LVGL)
4. SH8601 + touch + LVGL
5. Buttons, app task
6. Mark OTA valid
7. No Wi-Fi or boot-hold → provision AP; else STA
