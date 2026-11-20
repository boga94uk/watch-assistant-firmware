#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char wifi_ssid[33];
    char wifi_pass[65];
    char server_url[128];
    char device_secret[129];
    uint8_t volume;
    uint8_t mic_gain;
    uint8_t brightness;
    uint8_t schema;
    uint8_t auto_off; /* 0=off, then 30s, 1m, 5m, 10m, 15m, 1h */
} settings_t;

#define SETTINGS_AUTO_OFF_NEVER  0
#define SETTINGS_AUTO_OFF_COUNT  7

esp_err_t settings_init(void);
const settings_t *settings_get(void);
esp_err_t settings_save(const settings_t *in);
void settings_set_tune(uint8_t brightness, uint8_t volume, uint8_t mic_gain);
void settings_set_auto_off(uint8_t mode);
int64_t settings_auto_off_us(void);
const char *settings_auto_off_label(void);
const char *settings_auto_off_option_label(uint8_t mode);
bool settings_has_wifi(void);
const char *settings_root_ca(void);
bool settings_has_root_ca(void);
esp_err_t settings_set_root_ca(const char *pem);

#ifdef __cplusplus
}
#endif
