#include "wav_util.h"
#include "esp_log.h"
#include <string.h>

void wav_write_header(uint8_t *h, uint32_t data_bytes, uint32_t rate)
{
    uint32_t br = rate * 2, riff = 36 + data_bytes;
    uint16_t ch = 1, bps = 16, fmt = 1, align = 2, fmtsz = 16;
    memcpy(h + 0, "RIFF", 4);
    memcpy(h + 4, &riff, 4);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    memcpy(h + 16, &fmtsz, 4);
    memcpy(h + 20, &fmt, 2);
    memcpy(h + 22, &ch, 2);
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &br, 4);
    memcpy(h + 32, &align, 2);
    memcpy(h + 34, &bps, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &data_bytes, 4);
}

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool wav_parse(const uint8_t *data, size_t len, wav_info_t *out)
{
    if (!data || !out || len < 12 + 8) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    if (memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4)) {
        return false;
    }

    bool have_fmt = false;
    bool have_data = false;
    size_t off = 12;
    while (off + 8 <= len) {
        const uint8_t *id = data + off;
        uint32_t sz = u32(data + off + 4);
        size_t payload = off + 8;
        if (payload > len) {
            break;
        }
        size_t avail = len - payload;
        if (memcmp(id, "fmt ", 4) == 0) {
            if (sz >= 16 && avail >= 16) {
                out->format = u16(data + payload);
                out->channels = u16(data + payload + 2);
                out->rate = u32(data + payload + 4);
                out->bits = u16(data + payload + 14);
                have_fmt = true;
            }
        } else if (memcmp(id, "data", 4) == 0) {
            out->pcm_off = payload;
            out->pcm_bytes = sz <= avail ? sz : avail;
            /* Header-only buffers (44-byte peek) still count as having data. */
            have_data = sz >= 2;
            break;
        }
        if (sz > avail) {
            break;
        }
        size_t step = 8u + (size_t)sz;
        if (sz & 1u) {
            step++;
        }
        if (step < 8 || off + step < off) {
            break;
        }
        off += step;
    }
    return have_fmt && have_data;
}

bool wav_valid(const uint8_t *data, size_t len)
{
    wav_info_t info;
    if (!wav_parse(data, len, &info)) {
        ESP_LOGW("wav", "parse fail n=%u magic=%.4s", (unsigned)len, data ? (const char *)data : "null");
        return false;
    }
    const bool ok = info.format == 1 && info.channels == 1 && info.rate == WAV_RATE && info.bits == 16;
    if (!ok) {
        ESP_LOGW("wav", "fmt=%u ch=%u rate=%u bits=%u off=%u bytes=%u",
                 info.format, info.channels, (unsigned)info.rate, info.bits,
                 (unsigned)info.pcm_off, (unsigned)info.pcm_bytes);
    }
    return ok;
}
