#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WAV_HDR 44
#define WAV_RATE 16000

typedef struct {
    uint32_t rate;
    uint16_t channels;
    uint16_t bits;
    uint16_t format; /* 1 = PCM */
    size_t pcm_off;
    size_t pcm_bytes;
} wav_info_t;

void wav_write_header(uint8_t *h, uint32_t data_bytes, uint32_t rate);
bool wav_parse(const uint8_t *data, size_t len, wav_info_t *out);
bool wav_valid(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif
