#include "audio.h"
#include "wav_util.h"
#include "app_events.h"
#include "board.h"
#include "board_pins.h"
#include "es8311.h"
#include "settings.h"
#include "ui.h"

#include "driver/i2s_std.h"
#include "esp_cache.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "audio";
#define RATE 16000
#define FRAMES 256
#define MAX_PCM (RATE * 2 * 60)

static i2s_chan_handle_t s_tx, s_rx;
static es8311_handle_t s_codec;
static uint8_t *s_wav;
static size_t s_wav_len;
static uint8_t *s_tts;
static size_t s_tts_len;
static volatile bool s_recording;
static volatile bool s_playing;
static float s_level;

static void wav_header(uint8_t *h, uint32_t data_bytes)
{
    wav_write_header(h, data_bytes, RATE);
}

static void wav_cache_sync(uint8_t *buf, size_t len, uint32_t dir)
{
    if (!buf || !len) {
        return;
    }
    size_t sync = (len + 127u) & ~((size_t)127u);
    const size_t max_bytes = 44 + MAX_PCM;
    if (sync > max_bytes) {
        sync = max_bytes;
    }
    (void)esp_cache_msync(buf, sync, dir | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

static void capture_task(void *arg)
{
    (void)arg;
    int16_t stereo[FRAMES * 2];
    size_t pcm = 0;
    for (;;) {
        size_t n = 0;
        if (i2s_channel_read(s_rx, stereo, sizeof(stereo), &n, 100) != ESP_OK) {
            continue;
        }
        unsigned count = n / 4;
        int peak = 0;
        if (s_recording && s_wav && pcm + count * 2 < MAX_PCM) {
            int16_t *dst = (int16_t *)(s_wav + 44 + pcm);
            for (unsigned i = 0; i < count; i++) {
                /* Astra: ES8311 mono is LEFT, never average slots. */
                dst[i] = stereo[i * 2];
                int a = dst[i] < 0 ? -dst[i] : dst[i];
                if (a > peak) {
                    peak = a;
                }
            }
            pcm += count * 2;
            s_level = peak / 32768.0f;
            if (pcm + FRAMES * 2 >= MAX_PCM) {
                ESP_LOGI(TAG, "hit %d s limit", MAX_PCM / (RATE * 2));
                s_recording = false;
            }
        } else if (s_recording && s_wav) {
            s_recording = false;
        } else if (!s_recording && pcm) {
            wav_header(s_wav, (uint32_t)pcm);
            s_wav_len = 44 + pcm;
            /* Capture is core 1 / PSRAM; app+queue read on core 0. */
            wav_cache_sync(s_wav, s_wav_len, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
            pcm = 0;
            app_post(APP_EVT_RECORD_READY);
        } else if (!s_recording) {
            pcm = 0;
        }
    }
}

esp_err_t audio_init(void)
{
    board_pa_enable(false);

    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = 12;
    chan.dma_frame_num = FRAMES;
    chan.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &s_tx, &s_rx), TAG, "i2s chan");

    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_BCLK,
            .ws = BOARD_I2S_WS,
            .dout = BOARD_I2S_DOUT,
            .din = BOARD_I2S_DIN,
        },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "tx");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std), TAG, "rx");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "tx en");

    s_codec = es8311_create(0, ES8311_ADDRESS_0);
    ESP_RETURN_ON_FALSE(s_codec, ESP_FAIL, TAG, "es8311");
    es8311_clock_config_t clk = {
        .mclk_from_mclk_pin = true,
        .mclk_frequency = RATE * 256,
        .sample_frequency = RATE,
    };
    ESP_RETURN_ON_ERROR(es8311_init(s_codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16), TAG, "codec init");
    ESP_RETURN_ON_ERROR(es8311_microphone_config(s_codec, false), TAG, "mic");
    audio_set_mic_gain(settings_get()->mic_gain);
    audio_set_volume(settings_get()->volume);
    ESP_RETURN_ON_ERROR(es8311_voice_mute(s_codec, false), TAG, "unmute");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "rx en");

    s_wav = heap_caps_malloc(44 + MAX_PCM, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(s_wav, ESP_ERR_NO_MEM, TAG, "wav");
    xTaskCreatePinnedToCore(capture_task, "capture", 6144, NULL, 20, NULL, 1);
    ESP_LOGI(TAG, "Astra-style ES8311 16k/256x ready");
    return ESP_OK;
}

esp_err_t audio_start_recording(void)
{
    if (!s_wav || s_recording) {
        return ESP_ERR_INVALID_STATE;
    }
    s_wav_len = 0;
    s_level = 0;
    wav_header(s_wav, 0);
    s_recording = true;
    return ESP_OK;
}

void audio_stop_recording(void)
{
    s_recording = false;
}

const uint8_t *audio_wav(size_t *len)
{
    if (s_wav && s_wav_len) {
        wav_cache_sync(s_wav, s_wav_len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    }
    if (len) {
        *len = s_wav_len;
    }
    return s_wav;
}

void audio_free_wav(void)
{
    s_wav_len = 0;
}

void audio_stop(void)
{
    s_playing = false;
}

void audio_set_tts(const uint8_t *data, size_t len)
{
    audio_stop();
    vTaskDelay(pdMS_TO_TICKS(30));
    free(s_tts);
    s_tts = NULL;
    s_tts_len = 0;
    if (!data || !len) {
        return;
    }
    if (!wav_valid(data, len)) {
        ESP_LOGW(TAG, "tts not a valid 16 kHz mono WAV");
        return;
    }
    s_tts = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_tts) {
        return;
    }
    memcpy(s_tts, data, len);
    s_tts_len = len;
}

static void report_play_progress(int pct)
{
    if (pct < 0) {
        pct = 0;
    }
    if (pct > 100) {
        pct = 100;
    }
    if (ui_lock_timeout(20)) {
        ui_set_play_progress(pct);
        ui_unlock();
    }
}

esp_err_t audio_play_wav(const uint8_t *data, size_t len)
{
    wav_info_t inf;
    if (!wav_parse(data, len, &inf) || inf.format != 1 || inf.channels != 1 || inf.bits != 16 ||
        inf.pcm_off + inf.pcm_bytes > len) {
        ESP_LOGW(TAG, "reject play, invalid wav %u", (unsigned)len);
        app_post(APP_EVT_PLAY_DONE);
        return ESP_ERR_INVALID_ARG;
    }
    if (inf.rate != RATE) {
        ESP_LOGW(TAG, "wav rate %u, playing at %d", (unsigned)inf.rate, RATE);
    }
    s_playing = true;
    audio_set_volume(settings_get()->volume);
    (void)es8311_voice_mute(s_codec, false);
    board_pa_enable(true);
    vTaskDelay(pdMS_TO_TICKS(80));
    const int16_t *mono = (const int16_t *)(data + inf.pcm_off);
    unsigned total = (unsigned)(inf.pcm_bytes / 2);
    int16_t out[512];
    int last_pct = -1;
    report_play_progress(0);
    for (unsigned pos = 0; pos < total && s_playing; ) {
        unsigned n = total - pos;
        if (n > 256) {
            n = 256;
        }
        for (unsigned i = 0; i < n; i++) {
            unsigned ramp = 160;
            if (pos + i < ramp) {
                ramp = pos + i;
            }
            if (total - pos - i - 1 < ramp) {
                ramp = total - pos - i - 1;
            }
            int16_t s = (int16_t)((int32_t)mono[pos + i] * (int)ramp / 160);
            out[2 * i] = s;
            out[2 * i + 1] = s;
        }
        size_t sent = 0;
        while (sent < n * 4 && s_playing) {
            size_t z = 0;
            i2s_channel_write(s_tx, (uint8_t *)out + sent, n * 4 - sent, &z, 1000);
            sent += z;
        }
        pos += n;
        int pct = total ? (int)((pos * 100ull) / total) : 100;
        if (pct != last_pct && (pct == 100 || pct >= last_pct + 2)) {
            last_pct = pct;
            report_play_progress(pct);
        }
    }
    bool finished = s_playing;
    if (finished) {
        report_play_progress(100);
    }
    vTaskDelay(pdMS_TO_TICKS(250));
    board_pa_enable(false);
    s_playing = false;
    if (finished) {
        app_post(APP_EVT_PLAY_DONE);
    }
    return ESP_OK;
}

static void play_task(void *arg)
{
    (void)arg;
    if (s_tts && s_tts_len && wav_valid(s_tts, s_tts_len)) {
        audio_play_wav(s_tts, s_tts_len);
    } else {
        /* No speech: leave the text on screen long enough to read. */
        vTaskDelay(pdMS_TO_TICKS(2500));
        app_post(APP_EVT_PLAY_DONE);
    }
    vTaskDelete(NULL);
}

bool audio_has_tts(void)
{
    return s_tts && s_tts_len >= 46 && wav_valid(s_tts, s_tts_len);
}

void audio_play_last_tts(void)
{
    xTaskCreate(play_task, "play", 6 * 1024, NULL, 7, NULL);
}

static char s_play_path[192];

static void play_file_task(void *arg)
{
    (void)arg;
    FILE *f = fopen(s_play_path, "rb");
    if (!f) {
        ESP_LOGW("audio", "open %s failed", s_play_path);
        app_post(APP_EVT_PLAY_DONE);
        vTaskDelete(NULL);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 46 || sz > 2 * 1024 * 1024) {
        fclose(f);
        app_post(APP_EVT_PLAY_DONE);
        vTaskDelete(NULL);
        return;
    }
    uint8_t *buf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        fclose(f);
        app_post(APP_EVT_PLAY_DONE);
        vTaskDelete(NULL);
        return;
    }
    size_t n = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    audio_play_wav(buf, n);
    free(buf);
    vTaskDelete(NULL);
}

esp_err_t audio_play_file(const char *path)
{
    if (!path || !path[0]) {
        return ESP_ERR_INVALID_ARG;
    }
    audio_stop();
    strncpy(s_play_path, path, sizeof(s_play_path) - 1);
    s_play_path[sizeof(s_play_path) - 1] = 0;
    xTaskCreate(play_file_task, "playf", 6 * 1024, NULL, 7, NULL);
    return ESP_OK;
}

static void beep_task(void *arg)
{
    (void)arg;
    const unsigned n = RATE / 5; /* 200 ms */
    size_t len = 44 + n * 2;
    uint8_t *wav = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wav) {
        app_post(APP_EVT_PLAY_DONE);
        vTaskDelete(NULL);
        return;
    }
    wav_header(wav, n * 2);
    int16_t *pcm = (int16_t *)(wav + 44);
    for (unsigned i = 0; i < n; i++) {
        float env = 1.0f;
        if (i < 80) {
            env = i / 80.0f;
        } else if (i + 80 > n) {
            env = (n - i) / 80.0f;
        }
        pcm[i] = (int16_t)(sinf(2.0f * 3.14159265f * 880.0f * i / RATE) * 18000.0f * env);
    }
    audio_play_wav(wav, len);
    free(wav);
    vTaskDelete(NULL);
}

void audio_play_beep(void)
{
    if (s_playing || s_recording) {
        return;
    }
    xTaskCreate(beep_task, "beep", 4 * 1024, NULL, 6, NULL);
}

bool audio_is_recording(void)
{
    return s_recording;
}

float audio_level(void)
{
    return s_level;
}

void audio_set_volume(uint8_t percent)
{
    if (!s_codec) {
        return;
    }
    if (percent > 100) {
        percent = 100;
    }
    (void)es8311_voice_volume_set(s_codec, percent, NULL);
}

void audio_set_mic_gain(uint8_t steps)
{
    if (!s_codec) {
        return;
    }
    if (steps > 7) {
        steps = 7;
    }
    (void)es8311_microphone_gain_set(s_codec, (es8311_mic_gain_t)(ES8311_MIC_GAIN_0DB + steps));
}
