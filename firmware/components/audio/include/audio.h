#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t audio_init(void);
esp_err_t audio_start_recording(void);
void audio_stop_recording(void);
const uint8_t *audio_wav(size_t *len);
void audio_free_wav(void);
esp_err_t audio_play_wav(const uint8_t *data, size_t len);
void audio_play_last_tts(void);
bool audio_has_tts(void);
void audio_stop(void);
void audio_set_tts(const uint8_t *data, size_t len);
bool audio_is_recording(void);
float audio_level(void);
void audio_set_volume(uint8_t percent);
void audio_set_mic_gain(uint8_t steps);
void audio_play_beep(void);
esp_err_t audio_play_file(const char *path);

#ifdef __cplusplus
}
#endif
