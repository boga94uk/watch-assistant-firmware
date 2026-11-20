#include "settings.h"

#include "esp_check.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "settings";
static const char *NS = "watch";
static settings_t s_cur;
static char s_ca[4096];
static bool s_ca_set;

static const int64_t k_auto_off_us[SETTINGS_AUTO_OFF_COUNT] = {
    0,
    30LL * 1000000,
    60LL * 1000000,
    5LL * 60 * 1000000,
    10LL * 60 * 1000000,
    15LL * 60 * 1000000,
    60LL * 60 * 1000000,
};

static const char *k_auto_off_label[SETTINGS_AUTO_OFF_COUNT] = {
    "Off",
    "30 seconds",
    "1 minute",
    "5 minutes",
    "10 minutes",
    "15 minutes",
    "1 hour",
};

static void defaults(settings_t *s)
{
    memset(s, 0, sizeof(*s));
    /* Placeholders. Set the real URL and secret on the setup page, or replace
       these before flashing. The secret is 64 hex chars and must match devices.yaml. */
    strncpy(s->server_url, "https://your-server.example.com", sizeof(s->server_url) - 1);
    strncpy(s->device_secret, "0000000000000000000000000000000000000000000000000000000000000000",
            sizeof(s->device_secret) - 1);
    s->volume = 80;
    s->mic_gain = 5;
    s->brightness = 80;
    s->schema = 1;
    s->auto_off = SETTINGS_AUTO_OFF_NEVER;
}

esp_err_t settings_init(void)
{
    defaults(&s_cur);
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "no saved settings, using defaults");
        return ESP_OK;
    }
    size_t len = sizeof(s_cur.wifi_ssid);
    nvs_get_str(h, "ssid", s_cur.wifi_ssid, &len);
    len = sizeof(s_cur.wifi_pass);
    nvs_get_str(h, "pass", s_cur.wifi_pass, &len);
    len = sizeof(s_cur.server_url);
    nvs_get_str(h, "url", s_cur.server_url, &len);
    len = sizeof(s_cur.device_secret);
    nvs_get_str(h, "secret", s_cur.device_secret, &len);
    nvs_get_u8(h, "vol", &s_cur.volume);
    nvs_get_u8(h, "mic", &s_cur.mic_gain);
    nvs_get_u8(h, "bright", &s_cur.brightness);
    nvs_get_u8(h, "autooff", &s_cur.auto_off);
    if (s_cur.auto_off >= SETTINGS_AUTO_OFF_COUNT) {
        s_cur.auto_off = SETTINGS_AUTO_OFF_NEVER;
    }
    size_t ca_len = sizeof(s_ca);
    if (nvs_get_str(h, "rootca", s_ca, &ca_len) == ESP_OK && s_ca[0]) {
        s_ca_set = true;
        ESP_LOGI(TAG, "custom Root CA %u bytes", (unsigned)strlen(s_ca));
    }
    nvs_close(h);
    if (!s_cur.device_secret[0]) {
        strncpy(s_cur.device_secret, "0000000000000000000000000000000000000000000000000000000000000000",
                sizeof(s_cur.device_secret) - 1);
    }
    ESP_LOGI(TAG, "loaded ssid='%s' url='%s'", s_cur.wifi_ssid, s_cur.server_url);
    return ESP_OK;
}

const settings_t *settings_get(void)
{
    return &s_cur;
}

bool settings_has_wifi(void)
{
    return s_cur.wifi_ssid[0] != 0;
}

void settings_set_tune(uint8_t brightness, uint8_t volume, uint8_t mic_gain)
{
    s_cur.brightness = brightness;
    s_cur.volume = volume;
    s_cur.mic_gain = mic_gain;
}

void settings_set_auto_off(uint8_t mode)
{
    if (mode >= SETTINGS_AUTO_OFF_COUNT) {
        mode = SETTINGS_AUTO_OFF_NEVER;
    }
    s_cur.auto_off = mode;
    (void)settings_save(&s_cur);
}

int64_t settings_auto_off_us(void)
{
    if (s_cur.auto_off >= SETTINGS_AUTO_OFF_COUNT) {
        return 0;
    }
    return k_auto_off_us[s_cur.auto_off];
}

const char *settings_auto_off_option_label(uint8_t mode)
{
    if (mode >= SETTINGS_AUTO_OFF_COUNT) {
        return k_auto_off_label[0];
    }
    return k_auto_off_label[mode];
}

const char *settings_auto_off_label(void)
{
    return settings_auto_off_option_label(s_cur.auto_off);
}

esp_err_t settings_save(const settings_t *in)
{
    ESP_RETURN_ON_FALSE(in, ESP_ERR_INVALID_ARG, TAG, "null");
    s_cur = *in;
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "nvs");
    nvs_set_str(h, "ssid", s_cur.wifi_ssid);
    nvs_set_str(h, "pass", s_cur.wifi_pass);
    nvs_set_str(h, "url", s_cur.server_url);
    nvs_set_str(h, "secret", s_cur.device_secret);
    nvs_set_u8(h, "vol", s_cur.volume);
    nvs_set_u8(h, "mic", s_cur.mic_gain);
    nvs_set_u8(h, "bright", s_cur.brightness);
    nvs_set_u8(h, "schema", s_cur.schema);
    nvs_set_u8(h, "autooff", s_cur.auto_off);
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}

const char *settings_root_ca(void)
{
    return s_ca_set ? s_ca : "";
}

bool settings_has_root_ca(void)
{
    return s_ca_set;
}

esp_err_t settings_set_root_ca(const char *pem)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "nvs");
    if (!pem || !pem[0]) {
        s_ca[0] = 0;
        s_ca_set = false;
        nvs_erase_key(h, "rootca");
        ESP_LOGI(TAG, "cleared custom Root CA");
    } else {
        strncpy(s_ca, pem, sizeof(s_ca) - 1);
        s_ca[sizeof(s_ca) - 1] = 0;
        s_ca_set = true;
        nvs_set_str(h, "rootca", s_ca);
        ESP_LOGI(TAG, "saved custom Root CA %u bytes", (unsigned)strlen(s_ca));
    }
    esp_err_t err = nvs_commit(h);
    nvs_close(h);
    return err;
}
