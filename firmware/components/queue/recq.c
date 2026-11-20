#include "recq.h"
#include "sd.h"
#include "wav_util.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "recq";
#define QDIR "/sdcard/queue"

static uint32_t s_ids[RECQ_MAX];
static int s_n;
static uint8_t *s_ram;
static size_t s_ram_len;
static bool s_ram_used;
static char s_err[96];

static void set_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_err, sizeof(s_err), fmt, ap);
    va_end(ap);
    ESP_LOGE(TAG, "%s", s_err);
}

static int cmp_id(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    if (x < y) {
        return -1;
    }
    if (x > y) {
        return 1;
    }
    return 0;
}

static void rescan(void)
{
    s_n = 0;
    if (!sd_ok()) {
        return;
    }
    DIR *d = opendir(QDIR);
    if (!d) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL && s_n < RECQ_MAX) {
        unsigned id = 0;
        if (sscanf(e->d_name, "%u.wav", &id) == 1 && id > 0) {
            s_ids[s_n++] = id;
        }
    }
    closedir(d);
    qsort(s_ids, (size_t)s_n, sizeof(s_ids[0]), cmp_id);
}

static esp_err_t save_ram(const uint8_t *wav, size_t len)
{
    if (s_ram_used) {
        set_err("RAM slot used len=%u", (unsigned)len);
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t *buf = heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        set_err("RAM oom %u heap=%u", (unsigned)len, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        return ESP_ERR_NO_MEM;
    }
    memcpy(buf, wav, len);
    s_ram = buf;
    s_ram_len = len;
    s_ram_used = true;
    set_err("ok RAM %u", (unsigned)len);
    ESP_LOGW(TAG, "queued in RAM %u bytes", (unsigned)len);
    return ESP_OK;
}

static bool write_file(const char *path, const uint8_t *wav, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        set_err("fopen %s e=%d", path, errno);
        return false;
    }
    size_t off = 0;
    while (off < len) {
        size_t chunk = len - off;
        if (chunk > 4096) {
            chunk = 4096;
        }
        size_t w = fwrite(wav + off, 1, chunk, f);
        if (w == 0) {
            set_err("fwrite %s off=%u e=%d", path, (unsigned)off, errno);
            fclose(f);
            unlink(path);
            return false;
        }
        off += w;
    }
    if (fflush(f) != 0 || fclose(f) != 0) {
        set_err("close %s e=%d", path, errno);
        unlink(path);
        return false;
    }
    return true;
}

esp_err_t recq_init(void)
{
    s_n = 0;
    s_ram_used = false;
    if (sd_ok()) {
        if (mkdir(QDIR, 0777) != 0 && errno != EEXIST) {
            ESP_LOGW(TAG, "mkdir %s errno=%d", QDIR, errno);
        }
        rescan();
    }
    ESP_LOGI(TAG, "init n=%d sd=%d", s_n, (int)sd_ok());
    recq_dump();
    return ESP_OK;
}

int recq_count(void)
{
    int n = sd_ok() ? s_n : 0;
    if (s_ram_used) {
        n++;
    }
    return n;
}

bool recq_full(void)
{
    return recq_count() >= RECQ_MAX;
}

esp_err_t recq_push(const uint8_t *wav, size_t len)
{
    s_err[0] = 0;
    if (!wav || len < 46) {
        set_err("tiny wav=%p n=%u", (void *)wav, (unsigned)len);
        return ESP_ERR_INVALID_ARG;
    }
    const int valid = wav_valid(wav, len) ? 1 : 0;
    ESP_LOGI(TAG, "push n=%u sd=%d q=%d ram=%d valid=%d",
             (unsigned)len, (int)sd_ok(), s_n, (int)s_ram_used, valid);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, wav, 16, ESP_LOG_INFO);
    if (recq_full()) {
        set_err("full n=%d sd=%d ram=%d", recq_count(), (int)sd_ok(), (int)s_ram_used);
        return ESP_ERR_NO_MEM;
    }
    if (sd_ok()) {
        if (mkdir(QDIR, 0777) != 0 && errno != EEXIST) {
            ESP_LOGW(TAG, "mkdir %s errno=%d", QDIR, errno);
        }
        uint32_t id = s_n ? (s_ids[s_n - 1] + 1) : 1;
        char path[72];
        snprintf(path, sizeof(path), QDIR "/%06u.wav", (unsigned)id);
        if (write_file(path, wav, len)) {
            rescan();
            set_err("ok SD %s n=%d", path, s_n);
            ESP_LOGI(TAG, "pushed %s %u bytes n=%d", path, (unsigned)len, s_n);
            return ESP_OK;
        }
        ESP_LOGW(TAG, "SD write failed, trying RAM");
        return save_ram(wav, len);
    }
    return save_ram(wav, len);
}

const char *recq_last_error(void)
{
    return s_err;
}

void recq_dump(void)
{
    printf("recq sd=%d n=%d ram=%d ram_len=%u full=%d err=%s\n",
           (int)sd_ok(), s_n, (int)s_ram_used, (unsigned)s_ram_len,
           (int)recq_full(), s_err[0] ? s_err : "(none)");
    for (int i = 0; i < s_n; i++) {
        char path[72];
        snprintf(path, sizeof(path), QDIR "/%06u.wav", (unsigned)s_ids[i]);
        uint8_t head[16] = { 0 };
        long sz = -1;
        FILE *f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            (void)fread(head, 1, sizeof(head), f);
            fclose(f);
        }
        printf("recq id=%06u sz=%ld magic=%02x%02x%02x%02x %02x%02x%02x%02x\n",
               (unsigned)s_ids[i], sz, head[0], head[1], head[2], head[3],
               head[4], head[5], head[6], head[7]);
    }
    DIR *d = opendir(QDIR);
    if (!d) {
        printf("recq opendir %s failed e=%d sd=%d\n", QDIR, errno, (int)sd_ok());
        d = opendir("/sdcard");
        if (!d) {
            printf("recq opendir /sdcard failed e=%d\n", errno);
            return;
        }
        printf("recq listing /sdcard\n");
    } else {
        printf("recq listing %s\n", QDIR);
    }
    int listed = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && listed < 32) {
        printf("recq file %s\n", e->d_name);
        listed++;
    }
    closedir(d);
    printf("recq listed=%d\n", listed);
}

esp_err_t recq_head_path(char *path, size_t n)
{
    if (!path || n < 8 || !sd_ok() || s_n <= 0) {
        return ESP_ERR_NOT_FOUND;
    }
    snprintf(path, n, QDIR "/%06u.wav", (unsigned)s_ids[0]);
    return ESP_OK;
}

const uint8_t *recq_ram_head(size_t *len)
{
    if (!s_ram_used) {
        if (len) {
            *len = 0;
        }
        return NULL;
    }
    if (len) {
        *len = s_ram_len;
    }
    return s_ram;
}

esp_err_t recq_pop(void)
{
    if (sd_ok() && s_n > 0) {
        char path[72];
        snprintf(path, sizeof(path), QDIR "/%06u.wav", (unsigned)s_ids[0]);
        unlink(path);
        ESP_LOGI(TAG, "pop %s", path);
        rescan();
        return ESP_OK;
    }
    if (s_ram_used) {
        free(s_ram);
        s_ram = NULL;
        s_ram_len = 0;
        s_ram_used = false;
        return ESP_OK;
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t recq_clear(void)
{
    if (s_ram_used) {
        free(s_ram);
        s_ram = NULL;
        s_ram_len = 0;
        s_ram_used = false;
    }
    if (!sd_ok()) {
        ESP_LOGI(TAG, "clear RAM only");
        return ESP_OK;
    }
    DIR *d = opendir(QDIR);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (e->d_name[0] == '.') {
                continue;
            }
            if (strlen(e->d_name) > 40) {
                continue;
            }
            char path[72];
            snprintf(path, sizeof(path), QDIR "/%s", e->d_name);
            unlink(path);
        }
        closedir(d);
    }
    rescan();
    ESP_LOGI(TAG, "cleared queue n=%d", s_n);
    return ESP_OK;
}
