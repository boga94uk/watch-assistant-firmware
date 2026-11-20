#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_SCREEN_BOOT = 0,
    UI_SCREEN_IDLE,
    UI_SCREEN_RECORDING,
    UI_SCREEN_UPLOADING,
    UI_SCREEN_PROCESSING,
    UI_SCREEN_DOWNLOADING,
    UI_SCREEN_PLAYING_SAY,
    UI_SCREEN_PLAYING_ASK,
    UI_SCREEN_PLAYING_CONFIRM,
    UI_SCREEN_QUEUE,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_SLEEP,
    UI_SCREEN_ERROR,
    UI_SCREEN_PROVISION,
} ui_screen_t;

typedef struct {
    int battery_percent;
    bool charging;
    bool vbus;
    int wifi_bars;
    bool wifi_connected;
    char time[10];
    char date[16];
} ui_status_t;

esp_err_t ui_init(void);
void ui_lock(void);
bool ui_lock_timeout(uint32_t ms);
void ui_unlock(void);
bool ui_is_sleep_screen(void);
void ui_show(ui_screen_t screen);
void ui_set_status(const ui_status_t *status);
void ui_set_message(const char *title, const char *body);
void ui_set_level(float level);
void ui_set_play_progress(int percent);
void ui_set_queue_info(int count, const char *list);
void ui_show_touch(uint16_t x, uint16_t y, bool down);
void ui_settings_open(void);
void ui_settings_close(void);
bool ui_settings_is_open(void);
void ui_note_activity(void);
void ui_power_dialog_open(bool vbus);
void ui_power_dialog_close(void);
bool ui_power_dialog_is_open(void);
void ui_queue_dialog_open(int count);
void ui_queue_dialog_close(void);
bool ui_queue_dialog_is_open(void);
void ui_battery_dialog_open(void);
void ui_battery_dialog_close(void);
bool ui_battery_dialog_is_open(void);

/* Implemented by app.c */
void app_open_settings(void);
void app_close_overlay(void);
void app_touch_activity(void);
void app_open_setup_ap(void);
void app_mic_clicked(void);
void app_power_clicked(void);
void app_confirm_clicked(void);
void app_power_off_confirmed(void);
void app_queue_erase_one(void);
void app_queue_erase_all(void);
bool app_sleep_input_blocked(void);
void ui_settings_set_note(const char *msg);

#ifdef __cplusplus
}
#endif
