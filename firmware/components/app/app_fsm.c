#include "app_fsm.h"

bool app_evt_is_input(app_evt_type_t t)
{
    switch (t) {
    case APP_EVT_BTN_TOP_CLICK:
    case APP_EVT_BTN_TOP_HOLD:
    case APP_EVT_BTN_TOP_RELEASE:
    case APP_EVT_BTN_TOP_TRIPLE:
    case APP_EVT_BTN_BOTTOM_CLICK:
    case APP_EVT_BTN_BOTTOM_HOLD_POWER:
    case APP_EVT_WAKE:
    case APP_EVT_ACTIVITY:
    case APP_EVT_SLEEP:
    case APP_EVT_OPEN_SETTINGS:
    case APP_EVT_ENTER_SETUP:
    case APP_EVT_BOOT_DONE:
    case APP_EVT_POWER_OFF:
    case APP_EVT_QUEUE_ERASE_ONE:
    case APP_EVT_QUEUE_ERASE_ALL:
        return true;
    default:
        return false;
    }
}

bool app_evt_stale(uint32_t ev_ix, uint32_t cur_ix, app_evt_type_t t)
{
    if (app_evt_is_input(t)) {
        return false;
    }
    if (ev_ix == 0 || cur_ix == 0) {
        return false;
    }
    return ev_ix != cur_ix;
}

app_dec_t app_decide(app_state_t state, app_evt_type_t ev, bool net_up, int queue_n, char reply_type, bool have_tts)
{
    (void)reply_type;
    (void)have_tts;
    if (ev == APP_EVT_BTN_BOTTOM_CLICK) {
        switch (state) {
        case APP_STATE_RECORDING:
        case APP_STATE_UPLOADING:
        case APP_STATE_PROCESSING:
        case APP_STATE_DOWNLOADING:
        case APP_STATE_PLAYING_SAY:
        case APP_STATE_PLAYING_ASK:
        case APP_STATE_PLAYING_CONFIRM:
            return APP_DEC_CANCEL;
        case APP_STATE_SETTINGS:
            return APP_DEC_IDLE;
        default:
            return APP_DEC_IGNORE;
        }
    }

    switch (state) {
    case APP_STATE_IDLE:
    case APP_STATE_QUEUED:
        if (ev == APP_EVT_BTN_TOP_HOLD) {
            return APP_DEC_RECORD;
        }
        if (ev == APP_EVT_NET_UP && queue_n > 0) {
            return APP_DEC_DRAIN;
        }
        if (ev == APP_EVT_BTN_TOP_CLICK && state == APP_STATE_QUEUED && queue_n > 0) {
            return APP_DEC_DRAIN;
        }
        if (ev == APP_EVT_OPEN_SETTINGS || ev == APP_EVT_BTN_TOP_TRIPLE) {
            return APP_DEC_SETTINGS;
        }
        if (ev == APP_EVT_SLEEP) {
            return APP_DEC_SLEEP;
        }
        if (ev == APP_EVT_BTN_BOTTOM_HOLD_POWER) {
            return APP_DEC_SLEEP;
        }
        break;
    case APP_STATE_UPLOADING:
    case APP_STATE_PROCESSING:
        if (ev == APP_EVT_UPLOAD_SENT) {
            return APP_DEC_PROCESS;
        }
        if (ev == APP_EVT_RECEIVING) {
            return APP_DEC_RECEIVE;
        }
        if (ev == APP_EVT_UPLOAD_OK) {
            return APP_DEC_APPLY;
        }
        if (ev == APP_EVT_UPLOAD_FAIL) {
            return APP_DEC_QUEUE;
        }
        break;
    case APP_STATE_DOWNLOADING:
        if (ev == APP_EVT_UPLOAD_OK || ev == APP_EVT_TTS_READY) {
            return APP_DEC_APPLY;
        }
        if (ev == APP_EVT_UPLOAD_FAIL) {
            return queue_n > 0 ? APP_DEC_QUEUE : APP_DEC_IDLE;
        }
        break;
    case APP_STATE_PLAYING_SAY:
        if (ev == APP_EVT_PLAY_DONE) {
            return queue_n > 0 ? (net_up ? APP_DEC_DRAIN : APP_DEC_QUEUE) : APP_DEC_IDLE;
        }
        break;
    case APP_STATE_PLAYING_CONFIRM:
        if (ev == APP_EVT_BTN_TOP_CLICK) {
            return APP_DEC_EXECUTE;
        }
        break;
    default:
        break;
    }
    return APP_DEC_IGNORE;
}
