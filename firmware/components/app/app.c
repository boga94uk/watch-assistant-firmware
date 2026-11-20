#include "app_events.h"
#include "app_fsm.h"
#include "ui.h"
#include "display.h"
#include "settings.h"
#include "recq.h"
#include "audio.h"
#include "net.h"
#include "board.h"

#include "esp_sleep.h"

extern esp_err_t provision_open_ap(void);

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "app";
static QueueHandle_t s_q;
static app_state_t s_state = APP_STATE_BOOT;
static uint32_t s_ix = 1;
static char s_conv[48];
static char s_pending_cmd[64];
static char s_pending_tts[768];
static int64_t s_last_act;
static bool s_dimmed;

#define DIM_US    6000000LL
#define SLEEP_US  10000000LL

static bool s_blank;
static int64_t s_ignore_wake_until;
static int64_t s_ignore_power_click_until;
static bool s_auto_off_posted;

static void note_act(void)
{
    s_last_act = esp_timer_get_time();
    s_auto_off_posted = false;
    if (s_dimmed && s_state != APP_STATE_SLEEP) {
        display_set_brightness(settings_get()->brightness);
        s_dimmed = false;
    }
}

app_state_t app_state(void)
{
    return s_state;
}

const char *app_state_name(app_state_t s)
{
    switch (s) {
    case APP_STATE_BOOT: return "boot";
    case APP_STATE_IDLE: return "idle";
    case APP_STATE_RECORDING: return "recording";
    case APP_STATE_UPLOADING: return "uploading";
    case APP_STATE_PROCESSING: return "processing";
    case APP_STATE_DOWNLOADING: return "downloading";
    case APP_STATE_PLAYING_SAY: return "say";
    case APP_STATE_PLAYING_ASK: return "ask";
    case APP_STATE_PLAYING_CONFIRM: return "confirm";
    case APP_STATE_QUEUED: return "queued";
    case APP_STATE_SETTINGS: return "settings";
    case APP_STATE_SLEEP: return "sleep";
    case APP_STATE_PROVISION: return "provision";
    case APP_STATE_ERROR: return "error";
    default: return "?";
    }
}

void app_set_conversation(const char *id)
{
    if (!id) {
        s_conv[0] = 0;
        return;
    }
    strncpy(s_conv, id, sizeof(s_conv) - 1);
    s_conv[sizeof(s_conv) - 1] = 0;
}

const char *app_conversation(void)
{
    return s_conv;
}

void app_reset_conversation(void)
{
    s_conv[0] = 0;
    s_pending_cmd[0] = 0;
}

void app_post(app_evt_type_t type)
{
    app_post_text(type, NULL, NULL);
}

void app_post_text(app_evt_type_t type, const char *text, const char *extra)
{
    app_post_ix(s_ix, type, text, extra);
}

void app_post_ix(uint32_t ix, app_evt_type_t type, const char *text, const char *extra)
{
    app_evt_t e = { .type = type, .ix = ix };
    if (text) {
        strncpy(e.text, text, sizeof(e.text) - 1);
    }
    if (extra) {
        strncpy(e.extra, extra, sizeof(e.extra) - 1);
    }
    if (s_q) {
        xQueueSend(s_q, &e, 0);
    }
}

uint32_t app_interaction(void)
{
    return s_ix;
}

uint32_t app_begin_interaction(void)
{
    s_ix++;
    if (s_ix == 0) {
        s_ix = 1;
    }
    return s_ix;
}

void app_cancel_work(void)
{
    app_begin_interaction();
    audio_stop();
    audio_stop_recording();
    net_cancel();
    ESP_LOGI(TAG, "cancel ix=%u queue=%d", (unsigned)s_ix, recq_count());
}

static void show(ui_screen_t scr)
{
    if (!ui_lock_timeout(500)) {
        ESP_LOGW(TAG, "ui lock timeout showing %d", (int)scr);
        return;
    }
    ui_show(scr);
    ui_unlock();
}

static void brightness_locked(uint8_t pct)
{
    if (!ui_lock_timeout(500)) {
        ESP_LOGW(TAG, "ui lock timeout brightness, skip");
        return;
    }
    display_set_brightness(pct);
    ui_unlock();
}

static void set_state(app_state_t next)
{
    ESP_LOGI(TAG, "%s -> %s", app_state_name(s_state), app_state_name(next));
    s_state = next;
    switch (next) {
    case APP_STATE_IDLE: show(UI_SCREEN_IDLE); break;
    case APP_STATE_RECORDING: show(UI_SCREEN_RECORDING); break;
    case APP_STATE_UPLOADING: show(UI_SCREEN_UPLOADING); break;
    case APP_STATE_PROCESSING: show(UI_SCREEN_PROCESSING); break;
    case APP_STATE_DOWNLOADING: show(UI_SCREEN_DOWNLOADING); break;
    case APP_STATE_PLAYING_SAY: show(UI_SCREEN_PLAYING_SAY); break;
    case APP_STATE_PLAYING_ASK: show(UI_SCREEN_PLAYING_ASK); break;
    case APP_STATE_PLAYING_CONFIRM: show(UI_SCREEN_PLAYING_CONFIRM); break;
    case APP_STATE_QUEUED: show(UI_SCREEN_QUEUE); break;
    case APP_STATE_SETTINGS: show(UI_SCREEN_SETTINGS); break;
    case APP_STATE_SLEEP: show(UI_SCREEN_SLEEP); break;
    case APP_STATE_PROVISION: show(UI_SCREEN_PROVISION); break;
    case APP_STATE_ERROR: show(UI_SCREEN_ERROR); break;
    default: break;
    }
}

static void apply_reply(const app_evt_t *e)
{
    strncpy(s_pending_tts, e->text, sizeof(s_pending_tts) - 1);
    s_pending_tts[sizeof(s_pending_tts) - 1] = 0;
    const char typ = e->extra[0] ? e->extra[0] : 'T';
    const bool have_tts = e->extra[1] == 'A';
    ESP_LOGI(TAG, "reply type=%c tts=%d %s", typ, (int)have_tts, s_pending_tts);
    ui_lock();
    ui_set_message(NULL, s_pending_tts);
    ui_unlock();
    if (typ == 'A') {
        set_state(APP_STATE_PLAYING_ASK);
    } else if (typ == 'C') {
        strncpy(s_pending_cmd, e->extra[1] ? e->extra + 2 : e->extra + 1, sizeof(s_pending_cmd) - 1);
        s_pending_cmd[sizeof(s_pending_cmd) - 1] = 0;
        set_state(APP_STATE_PLAYING_CONFIRM);
    } else {
        set_state(APP_STATE_PLAYING_SAY);
    }
    ui_lock();
    ui_set_message(NULL, s_pending_tts);
    ui_unlock();
    audio_play_last_tts();
}

static void start_upload(void)
{
    app_begin_interaction();
    set_state(APP_STATE_UPLOADING);
    net_upload_last_recording();
}

static void after_cancel(void)
{
    app_cancel_work();
    app_reset_conversation();
    if (recq_count() > 0) {
        set_state(APP_STATE_QUEUED);
    } else {
        set_state(APP_STATE_IDLE);
    }
}

/* audio.h / net.h / recq.h included above */

static void enter_screensaver(bool from_power_tap)
{
    if (from_power_tap) {
        s_ignore_wake_until = esp_timer_get_time() + 5000000LL;
        ESP_LOGI(TAG, "screensaver from power tap, ignore touch/imu 5s");
    } else {
        s_ignore_wake_until = 0;
    }
    s_dimmed = true;
    s_blank = false;
    brightness_locked(8);
    set_state(APP_STATE_SLEEP);
}

static void handle(const app_evt_t *e)
{
    if (app_evt_stale(e->ix, s_ix, e->type)) {
        ESP_LOGW(TAG, "drop stale %d ix=%u cur=%u", (int)e->type, (unsigned)e->ix, (unsigned)s_ix);
        return;
    }
    if (e->type == APP_EVT_POWER_OFF) {
        ESP_LOGW(TAG, "power off");
        audio_stop();
        audio_stop_recording();
        brightness_locked(0);
        board_pa_enable(false);
        vTaskDelay(pdMS_TO_TICKS(80));
        if (board_power_off() != ESP_OK) {
            ESP_LOGE(TAG, "AXP softoff failed");
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        esp_deep_sleep_start();
        return;
    }
    if (e->type == APP_EVT_BTN_BOTTOM_HOLD_POWER && s_state != APP_STATE_SLEEP) {
        ui_lock();
        ui_queue_dialog_close();
        ui_power_dialog_open(board_vbus_in());
        ui_unlock();
        return;
    }
    if (e->type == APP_EVT_QUEUE_ERASE_ONE) {
        recq_pop();
        ESP_LOGI(TAG, "erased oldest queue n=%d", recq_count());
        recq_dump();
        if (recq_count() > 0) {
            set_state(APP_STATE_QUEUED);
            ui_lock();
            ui_set_queue_info(recq_count(), NULL);
            ui_unlock();
        } else {
            set_state(APP_STATE_IDLE);
        }
        return;
    }
    if (e->type == APP_EVT_QUEUE_ERASE_ALL) {
        recq_clear();
        ESP_LOGI(TAG, "cleared queue");
        recq_dump();
        set_state(APP_STATE_IDLE);
        return;
    }
    if (ui_power_dialog_is_open()) {
        if (e->type == APP_EVT_BTN_BOTTOM_CLICK) {
            ui_lock();
            ui_power_dialog_close();
            ui_unlock();
            return;
        }
        if (e->type == APP_EVT_SLEEP) {
            return;
        }
    }
    if (ui_queue_dialog_is_open()) {
        if (e->type == APP_EVT_BTN_BOTTOM_CLICK || e->type == APP_EVT_SLEEP) {
            ui_lock();
            ui_queue_dialog_close();
            ui_unlock();
            return;
        }
        if (e->type == APP_EVT_BTN_TOP_CLICK && recq_count() > 0) {
            ui_lock();
            ui_queue_dialog_close();
            ui_unlock();
            start_upload();
            return;
        }
    }
    switch (s_state) {
    case APP_STATE_BOOT:
        if (e->type == APP_EVT_ENTER_SETUP) {
            set_state(APP_STATE_PROVISION);
        } else if (e->type == APP_EVT_BOOT_DONE) {
            if (recq_count() > 0) {
                set_state(APP_STATE_QUEUED);
                /* NET_UP can arrive while still in BOOT and get dropped. */
                if (net_is_connected()) {
                    start_upload();
                }
            } else {
                set_state(APP_STATE_IDLE);
            }
        }
        break;

    case APP_STATE_IDLE:
        if (e->type == APP_EVT_BTN_TOP_HOLD) {
            if (recq_full()) {
                ui_lock();
                ui_set_message("Queue full", "Wait for the shop");
                ui_unlock();
            } else if (audio_start_recording() == ESP_OK) {
                app_begin_interaction();
                set_state(APP_STATE_RECORDING);
            } else {
                ui_lock();
                ui_set_message("Mic", "Could not start recording");
                ui_unlock();
                set_state(APP_STATE_ERROR);
            }
        } else if (e->type == APP_EVT_BTN_TOP_TRIPLE || e->type == APP_EVT_OPEN_SETTINGS) {
            set_state(APP_STATE_SETTINGS);
        } else if (e->type == APP_EVT_BTN_BOTTOM_CLICK) {
            if (esp_timer_get_time() < s_ignore_power_click_until) {
                ESP_LOGI(TAG, "ignore power click just after wake");
                break;
            }
            enter_screensaver(true);
        } else if (e->type == APP_EVT_SLEEP) {
            if (esp_timer_get_time() < s_ignore_power_click_until) {
                ESP_LOGI(TAG, "ignore sleep tap just after wake");
                break;
            }
            enter_screensaver(e->extra[0] == 't');
        }
        break;

    case APP_STATE_QUEUED:
        if (e->type == APP_EVT_BTN_TOP_HOLD) {
            if (recq_full()) {
                ui_lock();
                ui_set_message("Queue full", "Wait for the shop");
                ui_unlock();
            } else if (audio_start_recording() == ESP_OK) {
                app_begin_interaction();
                set_state(APP_STATE_RECORDING);
            } else {
                ui_lock();
                ui_set_message("Mic", "Could not start recording");
                ui_unlock();
                set_state(APP_STATE_ERROR);
            }
        } else if (e->type == APP_EVT_BTN_TOP_CLICK) {
            start_upload();
        } else if (e->type == APP_EVT_NET_UP) {
            start_upload();
        } else if (e->type == APP_EVT_BTN_TOP_TRIPLE || e->type == APP_EVT_OPEN_SETTINGS) {
            set_state(APP_STATE_SETTINGS);
        } else if (e->type == APP_EVT_BTN_BOTTOM_CLICK || e->type == APP_EVT_SLEEP) {
            ui_lock();
            ui_queue_dialog_open(recq_count());
            ui_unlock();
        }
        break;

    case APP_STATE_RECORDING:
        if (e->type == APP_EVT_BTN_BOTTOM_CLICK) {
            after_cancel();
        } else if (e->type == APP_EVT_BTN_TOP_RELEASE || e->type == APP_EVT_BTN_TOP_CLICK) {
            audio_stop_recording();
        } else if (e->type == APP_EVT_RECORD_READY) {
            size_t n = 0;
            const uint8_t *wav = audio_wav(&n);
            ESP_LOGI(TAG, "RECORD_READY n=%u ptr=%p q=%d", (unsigned)n, (void *)wav, recq_count());
            if (n < 44 + 8000) {
                ui_lock();
                ui_set_message("Too short", "Hold longer and speak");
                ui_unlock();
                set_state(recq_count() > 0 ? APP_STATE_QUEUED : APP_STATE_IDLE);
            } else if (recq_push(wav, n) != ESP_OK) {
                const char *why = recq_last_error();
                ESP_LOGE(TAG, "save failed: %s", why);
                recq_dump();
                ui_lock();
                ui_set_message("Problem", why[0] ? why : "Could not save recording");
                ui_unlock();
                set_state(APP_STATE_ERROR);
            } else {
                ui_lock();
                ui_set_queue_info(recq_count(), "saved");
                ui_unlock();
                if (net_is_connected()) {
                    start_upload();
                } else {
                    set_state(APP_STATE_QUEUED);
                }
            }
        }
        break;

    case APP_STATE_UPLOADING:
    case APP_STATE_PROCESSING:
        if (e->type == APP_EVT_BTN_BOTTOM_CLICK) {
            after_cancel();
        } else if (e->type == APP_EVT_UPLOAD_SENT) {
            if (s_state != APP_STATE_PROCESSING) {
                set_state(APP_STATE_PROCESSING);
            }
        } else if (e->type == APP_EVT_RECEIVING) {
            set_state(APP_STATE_DOWNLOADING);
        } else if (e->type == APP_EVT_UPLOAD_OK) {
            apply_reply(e);
        } else if (e->type == APP_EVT_UPLOAD_FAIL) {
            ui_lock();
            ui_set_message("Queued", e->text[0] ? e->text : "Will retry when online");
            ui_unlock();
            if (e->extra[0] == 'n' && recq_count() > 0 && net_is_connected()) {
                start_upload();
            } else {
                set_state(APP_STATE_QUEUED);
            }
        }
        break;

    case APP_STATE_DOWNLOADING:
        if (e->type == APP_EVT_UPLOAD_OK) {
            apply_reply(e);
        } else if (e->type == APP_EVT_TTS_READY) {
            audio_play_last_tts();
            set_state(APP_STATE_PLAYING_SAY);
            if (s_pending_tts[0]) {
                ui_lock();
                ui_set_message(NULL, s_pending_tts);
                ui_unlock();
            }
        } else if (e->type == APP_EVT_UPLOAD_FAIL) {
            if (e->extra[0] == 'n' && recq_count() > 0 && net_is_connected()) {
                start_upload();
            } else {
                set_state(recq_count() > 0 ? APP_STATE_QUEUED : APP_STATE_IDLE);
            }
        }
        break;

    case APP_STATE_PLAYING_SAY:
        if (e->type == APP_EVT_TTS_READY) {
            audio_play_last_tts();
        } else if (e->type == APP_EVT_PLAY_DONE) {
            app_reset_conversation();
            if (recq_count() > 0 && net_is_connected()) {
                start_upload();
            } else if (recq_count() > 0) {
                set_state(APP_STATE_QUEUED);
            } else {
                set_state(APP_STATE_IDLE);
            }
        } else if (e->type == APP_EVT_BTN_BOTTOM_CLICK) {
            after_cancel();
        }
        break;

    case APP_STATE_PLAYING_ASK:
        if (e->type == APP_EVT_TTS_READY) {
            audio_play_last_tts();
        } else if (e->type == APP_EVT_BTN_TOP_HOLD) {
            if (recq_full()) {
                ui_lock();
                ui_set_message("Queue full", "Wait for the shop");
                ui_unlock();
            } else if (audio_start_recording() == ESP_OK) {
                app_begin_interaction();
                set_state(APP_STATE_RECORDING);
            }
        } else if (e->type == APP_EVT_BTN_BOTTOM_CLICK) {
            after_cancel();
        }
        break;

    case APP_STATE_PLAYING_CONFIRM:
        if (e->type == APP_EVT_TTS_READY) {
            audio_play_last_tts();
        } else if (e->type == APP_EVT_BTN_TOP_HOLD) {
            audio_stop();
            if (recq_full()) {
                ui_lock();
                ui_set_message("Queue full", "Wait for the shop");
                ui_unlock();
            } else if (audio_start_recording() == ESP_OK) {
                app_begin_interaction();
                set_state(APP_STATE_RECORDING);
            }
        } else if (e->type == APP_EVT_BTN_TOP_CLICK) {
            audio_stop();
            app_begin_interaction();
            net_execute(s_conv, s_pending_cmd);
            set_state(APP_STATE_PROCESSING);
        } else if (e->type == APP_EVT_BTN_BOTTOM_CLICK) {
            after_cancel();
        }
        break;

    case APP_STATE_SETTINGS:
        if (e->type == APP_EVT_BTN_TOP_TRIPLE || e->type == APP_EVT_BTN_BOTTOM_CLICK) {
            ui_lock();
            if (ui_battery_dialog_is_open()) {
                ui_battery_dialog_close();
                ui_unlock();
                break;
            }
            ui_settings_close();
            ui_unlock();
            set_state(recq_count() > 0 ? APP_STATE_QUEUED : APP_STATE_IDLE);
        } else if (e->type == APP_EVT_ENTER_SETUP) {
            esp_err_t err = provision_open_ap();
            ui_lock();
            if (err == ESP_OK) {
                ui_settings_set_note("AP ESP Assistant  192.168.4.1");
            } else {
                char note[72];
                snprintf(note, sizeof(note), "Setup failed: %s", esp_err_to_name(err));
                ui_settings_set_note(note);
            }
            ui_unlock();
            ESP_LOGI(TAG, "open AP: %s", esp_err_to_name(err));
        }
        break;

    case APP_STATE_SLEEP:
        if (e->type == APP_EVT_NET_UP && recq_count() > 0) {
            s_dimmed = false;
            s_blank = false;
            set_state(APP_STATE_QUEUED);
            start_upload();
            break;
        }
        if (e->type == APP_EVT_ACTIVITY && app_sleep_input_blocked()) {
            break;
        }
        if (e->type == APP_EVT_WAKE || e->type == APP_EVT_ACTIVITY ||
            e->type == APP_EVT_BTN_TOP_CLICK || e->type == APP_EVT_BTN_BOTTOM_CLICK ||
            e->type == APP_EVT_BTN_TOP_HOLD || e->type == APP_EVT_BTN_TOP_TRIPLE ||
            e->type == APP_EVT_BTN_BOTTOM_HOLD_POWER) {
            s_dimmed = false;
            s_blank = false;
            s_ignore_power_click_until = esp_timer_get_time() + 500000LL;
            display_touch_reset_backoff();
            if (!ui_lock_timeout(500)) {
                ESP_LOGW(TAG, "wake ui lock timeout");
            } else {
                display_set_brightness(settings_get()->brightness);
                ui_unlock();
            }
            if (e->type == APP_EVT_BTN_TOP_HOLD) {
                if (recq_full()) {
                    ui_lock();
                    ui_set_message("Queue full", "Wait for the shop");
                    ui_unlock();
                    set_state(recq_count() > 0 ? APP_STATE_QUEUED : APP_STATE_IDLE);
                    break;
                }
                if (audio_start_recording() == ESP_OK) {
                    app_begin_interaction();
                    set_state(APP_STATE_RECORDING);
                    break;
                }
            }
            if (recq_count() > 0) {
                set_state(APP_STATE_QUEUED);
                if (net_is_connected()) {
                    start_upload();
                }
            } else {
                set_state(APP_STATE_IDLE);
            }
        }
        break;

    case APP_STATE_ERROR:
        if (e->type == APP_EVT_BTN_BOTTOM_CLICK || e->type == APP_EVT_BTN_TOP_CLICK) {
            set_state(APP_STATE_IDLE);
        }
        break;

    default:
        break;
    }
}

static void app_task(void *arg)
{
    (void)arg;
    app_evt_t e;
    s_last_act = esp_timer_get_time();
    while (1) {
        if (xQueueReceive(s_q, &e, pdMS_TO_TICKS(250)) == pdTRUE) {
            if (e.type != APP_EVT_SLEEP) {
                note_act();
            }
            handle(&e);
        }
        const int64_t now = esp_timer_get_time();
        static int64_t last_beat;
        if (now - last_beat > 2000000) {
            last_beat = now;
            ESP_LOGI(TAG, "beat %s dim=%d blank=%d", app_state_name(s_state), (int)s_dimmed, (int)s_blank);
        }
        const bool idleish = (s_state == APP_STATE_IDLE || s_state == APP_STATE_QUEUED);
        if (idleish && recq_count() == 0 && now - s_last_act > SLEEP_US &&
            !ui_power_dialog_is_open() && !ui_queue_dialog_is_open() &&
            !ui_battery_dialog_is_open()) {
            ESP_LOGI(TAG, "screensaver after %d s idle", (int)((now - s_last_act) / 1000000));
            enter_screensaver(false);
        } else if (idleish && !s_dimmed && now - s_last_act > DIM_US) {
            uint8_t b = settings_get()->brightness;
            ESP_LOGI(TAG, "dim after %d s idle", (int)((now - s_last_act) / 1000000));
            brightness_locked(b > 24 ? (uint8_t)(b / 3) : 8);
            s_dimmed = true;
        } else if (s_state == APP_STATE_SLEEP) {
            const int64_t off_us = settings_auto_off_us();
            if (off_us > 0 && recq_count() == 0 && !board_vbus_in() &&
                now - s_last_act > off_us && !s_auto_off_posted) {
                s_auto_off_posted = true;
                ESP_LOGW(TAG, "auto power off after %d s idle", (int)((now - s_last_act) / 1000000));
                app_post(APP_EVT_POWER_OFF);
            }
        }
    }
}

void app_open_settings(void)
{
    app_post(APP_EVT_OPEN_SETTINGS);
}

void app_close_overlay(void)
{
    app_post(APP_EVT_BTN_BOTTOM_CLICK);
}

void app_touch_activity(void)
{
    app_post(APP_EVT_ACTIVITY);
}

void app_open_setup_ap(void)
{
    app_post(APP_EVT_ENTER_SETUP);
}

void app_mic_clicked(void)
{
    app_post(APP_EVT_BTN_TOP_HOLD);
}

void app_power_clicked(void)
{
    app_post_text(APP_EVT_SLEEP, NULL, "tap");
}

bool app_sleep_input_blocked(void)
{
    return s_state == APP_STATE_SLEEP && esp_timer_get_time() < s_ignore_wake_until;
}

void app_power_off_confirmed(void)
{
    app_post(APP_EVT_POWER_OFF);
}

void app_queue_erase_one(void)
{
    app_post(APP_EVT_QUEUE_ERASE_ONE);
}

void app_queue_erase_all(void)
{
    app_post(APP_EVT_QUEUE_ERASE_ALL);
}

void app_confirm_clicked(void)
{
    app_post(APP_EVT_BTN_TOP_CLICK);
}

esp_err_t app_start(void)
{
    s_q = xQueueCreate(16, sizeof(app_evt_t));
    ESP_RETURN_ON_FALSE(s_q, ESP_ERR_NO_MEM, TAG, "queue");
    xTaskCreatePinnedToCore(app_task, "app", 8 * 1024, NULL, 5, NULL, 0);
    return ESP_OK;
}
