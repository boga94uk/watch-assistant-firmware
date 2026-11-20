#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_STATE_BOOT = 0,
    APP_STATE_IDLE,
    APP_STATE_RECORDING,
    APP_STATE_UPLOADING,
    APP_STATE_PROCESSING,
    APP_STATE_DOWNLOADING,
    APP_STATE_PLAYING_SAY,
    APP_STATE_PLAYING_ASK,
    APP_STATE_PLAYING_CONFIRM,
    APP_STATE_QUEUED,
    APP_STATE_SETTINGS,
    APP_STATE_SLEEP,
    APP_STATE_PROVISION,
    APP_STATE_ERROR,
} app_state_t;

typedef enum {
    APP_EVT_NONE = 0,
    APP_EVT_BOOT_DONE,
    APP_EVT_ENTER_SETUP,
    APP_EVT_BTN_TOP_CLICK,
    APP_EVT_BTN_TOP_HOLD,
    APP_EVT_BTN_TOP_RELEASE,
    APP_EVT_BTN_TOP_TRIPLE,
    APP_EVT_BTN_BOTTOM_CLICK,
    APP_EVT_BTN_BOTTOM_HOLD_POWER,
    APP_EVT_RECORD_READY,
    APP_EVT_UPLOAD_SENT,
    APP_EVT_RECEIVING,
    APP_EVT_UPLOAD_OK,
    APP_EVT_UPLOAD_FAIL,
    APP_EVT_TTS_READY,
    APP_EVT_PLAY_DONE,
    APP_EVT_NET_UP,
    APP_EVT_NET_DOWN,
    APP_EVT_SLEEP,
    APP_EVT_WAKE,
    APP_EVT_ACTIVITY,
    APP_EVT_OPEN_SETTINGS,
    APP_EVT_POWER_OFF,
    APP_EVT_QUEUE_ERASE_ONE,
    APP_EVT_QUEUE_ERASE_ALL,
} app_evt_type_t;

typedef struct {
    app_evt_type_t type;
    uint32_t ix;
    char text[768];
    char extra[96];
} app_evt_t;

esp_err_t app_start(void);
void app_post(app_evt_type_t type);
void app_post_text(app_evt_type_t type, const char *text, const char *extra);
void app_post_ix(uint32_t ix, app_evt_type_t type, const char *text, const char *extra);
uint32_t app_interaction(void);
uint32_t app_begin_interaction(void);
void app_cancel_work(void);
app_state_t app_state(void);
const char *app_state_name(app_state_t s);
void app_set_conversation(const char *id);
const char *app_conversation(void);
void app_reset_conversation(void);
void app_open_settings(void);
void app_close_overlay(void);
void app_touch_activity(void);
void app_open_setup_ap(void);
bool app_sleep_input_blocked(void);
void app_power_off_confirmed(void);
void app_queue_erase_one(void);
void app_queue_erase_all(void);

#ifdef __cplusplus
}
#endif
