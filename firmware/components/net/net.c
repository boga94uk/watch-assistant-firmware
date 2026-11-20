#include "net.h"
#include "app_events.h"
#include "audio.h"
#include "settings.h"
#include "recq.h"
#include "reply_parse.h"
#include "wav_util.h"

#include "cJSON.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_event.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_netif_sntp.h"
#include "esp_tls.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "mbedtls/md.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "net";
#define GOT_IP BIT0
static EventGroupHandle_t s_wifi_eg;
static bool s_connected;
static char s_device_id[24];
static volatile bool s_cancel;
static uint32_t s_job_ix;

void net_cancel(void)
{
    s_cancel = true;
}

static bool job_ok(uint32_t ix)
{
    return !s_cancel && ix == app_interaction();
}

bool net_is_connected(void)
{
    return s_connected;
}

int net_wifi_rssi(void)
{
    if (!s_connected) {
        return -127;
    }
    wifi_ap_record_t ap = { 0 };
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) {
        return -127;
    }
    return ap.rssi;
}

int net_wifi_bars(void)
{
    if (!s_connected) {
        return 0;
    }
    const int rssi = net_wifi_rssi();
    if (rssi == -127) {
        return 1;
    }
    if (rssi >= -50) {
        return 4;
    }
    if (rssi >= -60) {
        return 3;
    }
    if (rssi >= -70) {
        return 2;
    }
    return 1;
}

static void hex_encode(const uint8_t *in, size_t n, char *out)
{
    static const char *h = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = h[in[i] >> 4];
        out[i * 2 + 1] = h[in[i] & 0xf];
    }
    out[n * 2] = 0;
}

static void hex_decode(const char *hex, uint8_t *out, size_t *out_len)
{
    size_t n = strlen(hex) / 2;
    for (size_t i = 0; i < n; i++) {
        unsigned v = 0;
        sscanf(hex + i * 2, "%2x", &v);
        out[i] = (uint8_t)v;
    }
    *out_len = n;
}

static void hmac_sign(const char *hex_key, const char *msg, char sig_hex[65])
{
    uint8_t key[64];
    size_t key_len = 0;
    hex_decode(hex_key, key, &key_len);
    uint8_t mac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, key, key_len);
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)msg, strlen(msg));
    mbedtls_md_hmac_finish(&ctx, mac);
    mbedtls_md_free(&ctx);
    hex_encode(mac, 32, sig_hex);
}

static void now_iso(char *buf, size_t n)
{
    time_t t = time(NULL);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(buf, n, "%Y-%m-%dT%H:%M:%SZ", &tm);
}

static void fill_device_id(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(s_device_id, sizeof(s_device_id), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void add_auth(esp_http_client_handle_t http)
{
    const settings_t *s = settings_get();
    char ts[32];
    now_iso(ts, sizeof(ts));
    char msg[64];
    snprintf(msg, sizeof(msg), "%s|%s", s_device_id, ts);
    char sig[65] = { 0 };
    if (s->device_secret[0]) {
        hmac_sign(s->device_secret, msg, sig);
    }
    ESP_LOGI(TAG, "auth t=%s", ts);
    esp_http_client_set_header(http, "X-Device-ID", s_device_id);
    esp_http_client_set_header(http, "X-Device-Time", ts);
    if (sig[0]) {
        esp_http_client_set_header(http, "X-Device-Signature", sig);
    }
}

static void wifi_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        app_post(APP_EVT_NET_DOWN);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = true;
        xEventGroupSetBits(s_wifi_eg, GOT_IP);
        esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if (sta) {
            esp_netif_dns_info_t dns = { 0 };
            dns.ip.type = ESP_IPADDR_TYPE_V4;
            dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
            (void)esp_netif_set_dns_info(sta, ESP_NETIF_DNS_FALLBACK, &dns);
        }
        static bool sntp_started;
        if (!sntp_started) {
            esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("time.cloudflare.com");
            sntp.wait_for_sync = false;
            esp_netif_sntp_init(&sntp);
            sntp_started = true;
        }
        app_post(APP_EVT_NET_UP);
    }
}

esp_err_t net_init(void)
{
    fill_device_id();
    s_wifi_eg = xEventGroupCreate();
    if (!esp_netif_create_default_wifi_sta()) {
        return ESP_FAIL;
    }
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&cfg), TAG, "wifi init");
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_handler, NULL));

    const settings_t *s = settings_get();
    if (!s->wifi_ssid[0]) {
        ESP_LOGW(TAG, "no Wi-Fi saved");
        return ESP_OK;
    }
    wifi_config_t wc = { 0 };
    strncpy((char *)wc.sta.ssid, s->wifi_ssid, sizeof(wc.sta.ssid) - 1);
    strncpy((char *)wc.sta.password, s->wifi_pass, sizeof(wc.sta.password) - 1);
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc), TAG, "cfg");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");
    (void)esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "STA connecting to %s as %s", s->wifi_ssid, s_device_id);
    return ESP_OK;
}

static char *read_body(esp_http_client_handle_t http, int *status)
{
    int len = esp_http_client_fetch_headers(http);
    *status = esp_http_client_get_status_code(http);
    if (len < 0) {
        len = 8192;
    }
    char *buf = malloc((size_t)len + 1);
    if (!buf) {
        return NULL;
    }
    int n = esp_http_client_read_response(http, buf, len);
    if (n < 0) {
        n = 0;
    }
    buf[n] = 0;
    return buf;
}

static void handle_json(uint32_t ix, const char *json)
{
    voice_reply_t r;
    if (!voice_reply_parse(json, &r)) {
        if (job_ok(ix)) {
            app_post_ix(ix, APP_EVT_UPLOAD_FAIL, "bad json", NULL);
        }
        return;
    }
    if (r.conversation_id[0]) {
        app_set_conversation(r.conversation_id);
    }
    char extra[96];
    memset(extra, 0, sizeof(extra));
    extra[0] = r.type ? r.type : 'T';
    extra[1] = '0';
    if (r.type == 'C' && r.command[0]) {
        strncpy(extra + 2, r.command, sizeof(extra) - 3);
    }
    bool have_tts = false;
    if (r.audio_url[0]) {
        if (job_ok(ix)) {
            app_post_ix(ix, APP_EVT_RECEIVING, NULL, NULL);
        }
        const char *urlstr = r.audio_url;
        char full[320];
        if (strncmp(urlstr, "http", 4) == 0) {
            strncpy(full, urlstr, sizeof(full) - 1);
            full[sizeof(full) - 1] = 0;
        } else {
            snprintf(full, sizeof(full), "%s%s", settings_get()->server_url, urlstr);
        }
        ESP_LOGI(TAG, "tts GET %s", full);
        esp_http_client_config_t cfg = {
            .url = full,
            .timeout_ms = 30000,
            .keep_alive_enable = false,
            .addr_type = HTTP_ADDR_TYPE_INET,
        };
        if (settings_has_root_ca()) {
            cfg.cert_pem = settings_root_ca();
        } else {
            cfg.crt_bundle_attach = esp_crt_bundle_attach;
        }
        esp_http_client_handle_t dl = esp_http_client_init(&cfg);
        if (dl) {
            add_auth(dl);
            esp_err_t oerr = esp_http_client_open(dl, 0);
            if (oerr == ESP_OK) {
                int cl = esp_http_client_fetch_headers(dl);
                int st = esp_http_client_get_status_code(dl);
                ESP_LOGI(TAG, "tts HTTP %d cl=%d", st, cl);
                if (st == 200 && (cl < 0 || (cl > 44 && cl < 2 * 1024 * 1024))) {
                    int cap = (cl > 44) ? cl : (512 * 1024);
                    uint8_t *buf = heap_caps_malloc((size_t)cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                    if (buf) {
                        int n = 0;
                        while (n < cap) {
                            int r = esp_http_client_read(dl, (char *)buf + n, cap - n);
                            if (r < 0) {
                                n = -1;
                                break;
                            }
                            if (r == 0) {
                                break;
                            }
                            n += r;
                        }
                        if (n > 44 && wav_valid(buf, (size_t)n) && job_ok(ix)) {
                            audio_set_tts(buf, (size_t)n);
                            have_tts = audio_has_tts();
                            ESP_LOGI(TAG, "tts accepted n=%d", n);
                        } else {
                            ESP_LOGW(TAG, "tts rejected n=%d magic=%.4s", n, n > 4 ? (char *)buf : "----");
                        }
                        free(buf);
                    } else {
                        ESP_LOGW(TAG, "tts oom %d", cap);
                    }
                } else {
                    ESP_LOGW(TAG, "tts bad HTTP %d len %d", st, cl);
                }
            } else {
                ESP_LOGW(TAG, "tts open %s", esp_err_to_name(oerr));
            }
            esp_http_client_cleanup(dl);
        }
    }
    extra[1] = have_tts ? 'A' : '0';
    ESP_LOGI(TAG, "reply type=%c tts=%d cmd=%s", extra[0], (int)have_tts, extra[0] == 'C' ? extra + 2 : "-");
    if (job_ok(ix)) {
        recq_pop();
        app_post_ix(ix, APP_EVT_UPLOAD_OK, r.message, extra);
    }
}

static void wait_for_time(void)
{
    /* RTC can be ~80s off; shop HMAC window is 60s. Wait for SNTP. */
    esp_err_t err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(8000));
    time_t now = time(NULL);
    ESP_LOGI(TAG, "time %s t=%ld", esp_err_to_name(err), (long)now);
}

static void host_from_url(const char *url, char *out, size_t n)
{
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t i = 0;
    while (p[i] && p[i] != '/' && p[i] != ':' && i + 1 < n) {
        out[i] = p[i];
        i++;
    }
    out[i] = 0;
}

static bool resolve_v4(const char *host)
{
    struct addrinfo hints = {
        .ai_family = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
    int err = getaddrinfo(host, "443", &hints, &res);
    if (err != 0 || !res) {
        ESP_LOGE(TAG, "DNS %s failed %d", host, err);
        return false;
    }
    char ip[16];
    inet_ntoa_r(((struct sockaddr_in *)res->ai_addr)->sin_addr, ip, sizeof(ip));
    ESP_LOGI(TAG, "DNS %s -> %s", host, ip);
    freeaddrinfo(res);
    return true;
}

static void force_dns_1111(void)
{
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!sta) {
        return;
    }
    esp_netif_dns_info_t dns = { 0 };
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(1, 1, 1, 1);
    (void)esp_netif_set_dns_info(sta, ESP_NETIF_DNS_MAIN, &dns);
    ESP_LOGW(TAG, "DNS set to 1.1.1.1");
}

static void post_fail(uint32_t ix, const char *msg)
{
    char line[96];
    size_t i = 0;
    if (!msg) {
        msg = "upload failed";
    }
    for (; msg[i] && i + 1 < sizeof(line); i++) {
        char c = msg[i];
        line[i] = (c == '\n' || c == '\r') ? ' ' : c;
    }
    line[i] = 0;
    ESP_LOGE(TAG, "FAIL %s", line);
    if (job_ok(ix)) {
        app_post_ix(ix, APP_EVT_UPLOAD_FAIL, line, NULL);
    }
}

static void upload_task(void *arg)
{
    (void)arg;
    const uint32_t ix = app_interaction();
    s_job_ix = ix;
    s_cancel = false;
    wait_for_time();
    if (!job_ok(ix)) {
        vTaskDelete(NULL);
        return;
    }
    if (!net_is_connected()) {
        ESP_LOGW(TAG, "upload waiting for Wi-Fi");
        xEventGroupWaitBits(s_wifi_eg, GOT_IP, pdFALSE, pdTRUE, pdMS_TO_TICKS(10000));
        if (!net_is_connected() || !job_ok(ix)) {
            post_fail(ix, "offline");
            vTaskDelete(NULL);
            return;
        }
    }
    char path[72] = { 0 };
    FILE *f = NULL;
    size_t len = 0;
    const uint8_t *ram = NULL;
    if (recq_head_path(path, sizeof(path)) == ESP_OK) {
        f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (sz > 44) {
                len = (size_t)sz;
            }
        } else {
            ESP_LOGW(TAG, "open %s failed, trying RAM", path);
        }
    }
    if (!f) {
        ram = recq_ram_head(&len);
    }
    uint8_t hdr[WAV_HDR];
    if (f) {
        size_t hn = fread(hdr, 1, sizeof(hdr), f);
        if (hn != sizeof(hdr) || memcmp(hdr, "RIFF", 4) != 0 || len < 46) {
            ESP_LOGW(TAG, "bad queued wav %s n=%u read=%u", path, (unsigned)len, (unsigned)hn);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, hdr, hn > 16 ? 16 : hn, ESP_LOG_WARN);
            fclose(f);
            recq_pop();
            app_post_ix(ix, APP_EVT_UPLOAD_FAIL, "bad queued wav", "next");
            vTaskDelete(NULL);
            return;
        }
        fseek(f, 0, SEEK_SET);
    } else if (!ram || len < 46 || memcmp(ram, "RIFF", 4) != 0) {
        recq_pop();
        app_post_ix(ix, APP_EVT_UPLOAD_FAIL, "empty recording", "next");
        vTaskDelete(NULL);
        return;
    }
    const settings_t *s = settings_get();
    char url[160];
    snprintf(url, sizeof(url), "%s/transcribe", s->server_url);

    const char *bound = "----ESP32Boundary12345";
    char pre[256];
    int pre_n = snprintf(pre, sizeof(pre),
                         "--%s\r\nContent-Disposition: form-data; name=\"audio\"; filename=\"rec.wav\"\r\nContent-Type: audio/wav\r\n\r\n",
                         bound);
    char conv_part[192] = { 0 };
    int conv_n = 0;
    if (app_conversation()[0]) {
        conv_n = snprintf(conv_part, sizeof(conv_part),
                          "\r\n--%s\r\nContent-Disposition: form-data; name=\"conversation_id\"\r\n\r\n%s",
                          bound, app_conversation());
    }
    char postb[80];
    int post_n = snprintf(postb, sizeof(postb), "\r\n--%s--\r\n", bound);
    const int total = pre_n + (int)len + conv_n + post_n;

    ESP_LOGI(TAG, "upload %u bytes %s connected=%d t=%ld int=%u",
             (unsigned)len, path[0] ? path : "ram", (int)net_is_connected(), (long)time(NULL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    char host[80];
    host_from_url(url, host, sizeof(host));
    if (host[0] && !resolve_v4(host)) {
        force_dns_1111();
        vTaskDelay(pdMS_TO_TICKS(200));
        if (!resolve_v4(host)) {
            if (f) {
                fclose(f);
            }
            post_fail(ix, "dns failed");
            vTaskDelete(NULL);
            return;
        }
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .addr_type = HTTP_ADDR_TYPE_INET,
        .keep_alive_enable = false,
    };
    if (settings_has_root_ca()) {
        cfg.cert_pem = settings_root_ca();
    } else {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    if (!http) {
        if (f) {
            fclose(f);
        }
        post_fail(ix, "http init");
        vTaskDelete(NULL);
        return;
    }
    add_auth(http);
    char ctype[96];
    snprintf(ctype, sizeof(ctype), "multipart/form-data; boundary=%s", bound);
    esp_http_client_set_header(http, "Content-Type", ctype);
    esp_err_t oerr = esp_http_client_open(http, total);
    if (oerr != ESP_OK || !job_ok(ix)) {
        int en = esp_http_client_get_errno(http);
        ESP_LOGE(TAG, "upload open %s errno=%d", esp_err_to_name(oerr), en);
        char fail[64];
        snprintf(fail, sizeof(fail), "connect %s e=%d", esp_err_to_name(oerr), en);
        if (f) {
            fclose(f);
        }
        post_fail(ix, fail);
        esp_http_client_cleanup(http);
        vTaskDelete(NULL);
        return;
    }
    esp_http_client_write(http, pre, pre_n);
    if (f) {
        char chunk[2048];
        size_t n;
        while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) {
            if (!job_ok(ix)) {
                break;
            }
            esp_http_client_write(http, chunk, (int)n);
        }
        fclose(f);
        f = NULL;
    } else {
        esp_http_client_write(http, (const char *)ram, (int)len);
    }
    if (!job_ok(ix)) {
        esp_http_client_cleanup(http);
        vTaskDelete(NULL);
        return;
    }
    if (conv_n) {
        esp_http_client_write(http, conv_part, conv_n);
    }
    esp_http_client_write(http, postb, post_n);
    app_post_ix(ix, APP_EVT_UPLOAD_SENT, NULL, NULL);
    int status = 0;
    char *body = read_body(http, &status);
    esp_http_client_cleanup(http);
    ESP_LOGI(TAG, "upload HTTP %d", status);
    if (!job_ok(ix)) {
        free(body);
        vTaskDelete(NULL);
        return;
    }
    if (!body || status != 200) {
        post_fail(ix, body ? body : "upload failed");
        free(body);
        vTaskDelete(NULL);
        return;
    }
    handle_json(ix, body);
    free(body);
    vTaskDelete(NULL);
}

esp_err_t net_upload_last_recording(void)
{
    s_cancel = false;
    xTaskCreatePinnedToCore(upload_task, "upload", 12 * 1024, NULL, 5, NULL, 1);
    return ESP_OK;
}

static char s_exec_conv[48];
static char s_exec_cmd[64];

static void execute_task(void *arg)
{
    (void)arg;
    const uint32_t ix = app_interaction();
    s_cancel = false;
    const settings_t *s = settings_get();
    char url[160];
    snprintf(url, sizeof(url), "%s/execute", s->server_url);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "command", s_exec_cmd);
    cJSON_AddStringToObject(root, "conversation_id", s_exec_conv);
    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        post_fail(ix, "execute failed");
        vTaskDelete(NULL);
        return;
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000,
        .keep_alive_enable = false,
        .addr_type = HTTP_ADDR_TYPE_INET,
    };
    if (settings_has_root_ca()) {
        cfg.cert_pem = settings_root_ca();
    } else {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    if (!http) {
        post_fail(ix, "execute failed");
        free(payload);
        vTaskDelete(NULL);
        return;
    }
    add_auth(http);
    esp_http_client_set_header(http, "Content-Type", "application/json");
    int plen = (int)strlen(payload);
    esp_err_t err = esp_http_client_open(http, plen);
    int status = 0;
    char *body = NULL;
    if (err == ESP_OK) {
        esp_http_client_write(http, payload, plen);
        app_post_ix(ix, APP_EVT_UPLOAD_SENT, NULL, NULL);
        body = read_body(http, &status);
        ESP_LOGI(TAG, "execute HTTP %d", status);
    } else {
        ESP_LOGW(TAG, "execute open %s", esp_err_to_name(err));
    }
    /* Close /execute before downloading TTS. One TLS socket to the tunnel. */
    esp_http_client_cleanup(http);
    free(payload);
    if (!job_ok(ix)) {
        free(body);
        vTaskDelete(NULL);
        return;
    }
    if (status == 200 && body) {
        handle_json(ix, body);
    } else {
        post_fail(ix, "execute failed");
    }
    free(body);
    vTaskDelete(NULL);
}

esp_err_t net_execute(const char *conv, const char *cmd)
{
    strncpy(s_exec_conv, conv ? conv : "", sizeof(s_exec_conv) - 1);
    strncpy(s_exec_cmd, cmd ? cmd : "", sizeof(s_exec_cmd) - 1);
    s_exec_conv[sizeof(s_exec_conv) - 1] = 0;
    s_exec_cmd[sizeof(s_exec_cmd) - 1] = 0;
    ESP_LOGI(TAG, "execute %s conv=%s", s_exec_cmd, s_exec_conv);
    xTaskCreatePinnedToCore(execute_task, "exec", 8 * 1024, NULL, 5, NULL, 1);
    return ESP_OK;
}
