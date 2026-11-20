#pragma once

#include "app_events.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool app_evt_is_input(app_evt_type_t t);
bool app_evt_stale(uint32_t ev_ix, uint32_t cur_ix, app_evt_type_t t);

typedef enum {
    APP_DEC_IGNORE = 0,
    APP_DEC_IDLE,
    APP_DEC_RECORD,
    APP_DEC_UPLOAD,
    APP_DEC_PROCESS,
    APP_DEC_RECEIVE,
    APP_DEC_PLAY,
    APP_DEC_QUEUE,
    APP_DEC_SETTINGS,
    APP_DEC_SLEEP,
    APP_DEC_CANCEL,
    APP_DEC_APPLY,
    APP_DEC_EXECUTE,
    APP_DEC_DRAIN,
} app_dec_t;

app_dec_t app_decide(app_state_t state, app_evt_type_t ev, bool net_up, int queue_n, char reply_type, bool have_tts);

#ifdef __cplusplus
}
#endif
