#include "provision.h"
#include "provision_html.h"
#include "app_events.h"
#include "audio.h"
#include "board.h"
#include "display.h"
#include "settings.h"

#include "esp_check.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_net_stack.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#if CONFIG_LWIP_IPV6
#include "lwip/netif.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *TAG = "provision";
static httpd_handle_t s_httpd;
static bool s_ap_on;
static bool s_dns_on;
static esp_netif_t *s_ap_netif;

bool provision_boot_hold(void)
{
    const TickType_t t0 = xTaskGetTickCount();
    while ((xTaskGetTickCount() - t0) < pdMS_TO_TICKS(3000)) {
        if (!board_bottom_button_pressed()) {
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

static void html_escape(const char *in, char *out, size_t n)
{
    size_t o = 0;
    for (; *in && o + 8 < n; in++) {
        if (*in == '<') {
            memcpy(out + o, "&lt;", 4);
            o += 4;
        } else if (*in == '>') {
            memcpy(out + o, "&gt;", 4);
            o += 4;
        } else if (*in == '&') {
            memcpy(out + o, "&amp;", 5);
            o += 5;
        } else if (*in == '"') {
            memcpy(out + o, "&quot;", 6);
            o += 6;
        } else {
            out[o++] = *in;
        }
    }
    out[o] = 0;
}

static esp_err_t send_html(httpd_req_t *req, const char *html)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t root_get(httpd_req_t *req)
{
    const settings_t *s = settings_get();
    char ssid[48], url[160], mac[24];
    html_escape(s->wifi_ssid, ssid, sizeof(ssid));
    html_escape(s->server_url, url, sizeof(url));
    uint8_t m[6];
    esp_read_mac(m, ESP_MAC_WIFI_STA);
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    int bat = board_battery_percent();
    int mv = board_battery_mv();

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_sendstr_chunk(req, PORTAL_HEAD);

    char *body = malloc(5200);
    if (!body) {
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_FAIL;
    }
    const char *ca_note = settings_has_root_ca()
        ? "A custom cert is stored. Paste a new PEM to replace, or leave blank to keep it."
        : "Using the built-in public CA bundle.";
    snprintf(body, 5200,
             "<div class=head><h1>ESP Assistant</h1>"
             "<p>Test and configuration</p>"
             "<p class=mac>%s</p></div>"
             "<div class=tabs>"
             "<button class='tab on' id=tstatus onclick=\"tab('status')\">Status</button>"
             "<button class=tab id=tconfig onclick=\"tab('config')\">Config</button>"
             "<button class=tab id=tlevels onclick=\"tab('levels')\">Levels</button>"
             "</div>"

             "<div id=status class='pane on'>"
             "<div class=card><h2>System</h2>"
             "<div class=row><span>Wi-Fi now</span><b>%s</b></div>"
             "<div class=row><span>Battery</span><b>%d%%  %d mV%s</b></div>"
             "<div class=row><span>Brightness</span><b>%u%%</b></div>"
             "<div class=row><span>Volume</span><b>%u%%</b></div>"
             "<div class=row><span>Mic gain</span><b>%u / 7</b></div>"
             "<div class=row><span>Setup AP</span><b>ESP Assistant &nbsp; 192.168.4.1</b></div>"
             "</div>"
             "<div class=card><h2>microSD</h2>"
             "<p>Browse recordings, download WAVs, or play them on the watch.</p>"
             "<a class=btn href=/files>Open file browser</a> "
             "<a class='btn warn' href=/format-sd>Format / empty queue</a></div>"
             "<div class=card><h2>Firmware update</h2>"
             "<a class=btn href=/ota>OTA software update</a></div>"
             "<div class=card><h2>Exit setup</h2>"
             "<p class=infobox>Reboot into normal mode. The watch reconnects to shop Wi-Fi.</p>"
             "<a class='btn warn' href=/reboot>Reboot to normal mode</a></div>"
             "</div>"

             "<div id=config class=pane>"
             "<div class=card><h2>Shop Wi-Fi</h2>"
             "<form method=post action=/wifi>"
             "<label>Network name</label><input name=ssid value='%s' autocomplete=off>"
             "<label>Password</label><input name=pass type=password placeholder='leave blank to keep'>"
             "<button class=ok>Save Wi-Fi and reboot</button></form></div>"
             "<div class=card><h2>Shop server</h2>"
             "<form method=post action=/server>"
             "<label>Server URL</label><input name=url value='%s'>"
             "<label>Device secret</label><input name=secret type=password placeholder='leave blank to keep'>"
             "<details style='margin-top:12px'><summary style='cursor:pointer;color:#667eea;font-weight:700'>SSL Root CA (optional)</summary>"
             "<p class=infobox>Not needed when the server URL uses a public certificate (Cloudflare or another public CA). Paste a PEM chain only if you use a private or custom server. %s</p>"
             "<label>Root CA PEM</label>"
             "<textarea name=rootca rows=8 style='width:100%%;font-family:monospace;font-size:11px;padding:8px'></textarea>"
             "</details>"
             "<button class=ghost>Save server</button></form></div>"
             "</div>"

             "<div id=levels class=pane>"
             "<div class=card><h2>Display and audio</h2>"
             "<form method=post action=/tune>"
             "<label>Brightness 8-100</label><input name=bright type=number min=8 max=100 value='%u'>"
             "<label>Volume 0-100</label><input name=vol type=number min=0 max=100 value='%u'>"
             "<label>Mic gain 0-7</label><input name=mic type=number min=0 max=7 value='%u'>"
             "<button>Save levels</button></form>"
             "</div></div>",
             mac,
             ssid[0] ? ssid : "(none)",
             bat, mv, board_battery_charging() ? " charging" : "",
             (unsigned)s->brightness, (unsigned)s->volume, (unsigned)s->mic_gain,
             ssid, url, ca_note,
             (unsigned)s->brightness, (unsigned)s->volume, (unsigned)s->mic_gain);
    httpd_resp_sendstr_chunk(req, body);
    free(body);
    httpd_resp_sendstr_chunk(req, PORTAL_TAIL);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t captive_get(httpd_req_t *req)
{
    /* Official IDF pattern: 302 + body. iOS ignores empty redirects. */
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req,
                           "<html><head><meta http-equiv='refresh' content='0;url=http://192.168.4.1/'></head>"
                           "<body>Sign in to network</body></html>",
                           HTTPD_RESP_USE_STRLEN);
}

static esp_err_t http_404_handler(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    return captive_get(req);
}

static esp_err_t read_form(httpd_req_t *req, char *buf, size_t n)
{
    int len = req->content_len;
    if (len <= 0 || (size_t)len >= n) {
        return ESP_FAIL;
    }
    int r = httpd_req_recv(req, buf, len);
    if (r <= 0) {
        return ESP_FAIL;
    }
    buf[r] = 0;
    return ESP_OK;
}

static void url_decode(char *s)
{
    char *o = s;
    while (*s) {
        if (*s == '+') {
            *o++ = ' ';
            s++;
        } else if (*s == '%' && s[1] && s[2]) {
            unsigned v = 0;
            sscanf(s + 1, "%2x", &v);
            *o++ = (char)v;
            s += 3;
        } else {
            *o++ = *s++;
        }
    }
    *o = 0;
}

static bool form_get(char *body, const char *key, char *out, size_t n)
{
    char pat[32];
    snprintf(pat, sizeof(pat), "%s=", key);
    char *p = strstr(body, pat);
    if (!p) {
        return false;
    }
    p += strlen(pat);
    char *e = strchr(p, '&');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    if (len >= n) {
        len = n - 1;
    }
    memcpy(out, p, len);
    out[len] = 0;
    url_decode(out);
    return true;
}

static esp_err_t wifi_post(httpd_req_t *req)
{
    char body[256];
    if (read_form(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "form");
    }
    settings_t s = *settings_get();
    form_get(body, "ssid", s.wifi_ssid, sizeof(s.wifi_ssid));
    form_get(body, "pass", s.wifi_pass, sizeof(s.wifi_pass));
    settings_save(&s);
    send_html(req, "<body style='font-family:system-ui;background:#0b0b0e;color:#f1efe6;padding:40px'>Saved. Rebooting…");
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
    return ESP_OK;
}

static esp_err_t server_post(httpd_req_t *req)
{
    int len = req->content_len;
    if (len <= 0 || len > 5000) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "form");
    }
    char *body = malloc((size_t)len + 1);
    if (!body) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    }
    int r = httpd_req_recv(req, body, len);
    if (r <= 0) {
        free(body);
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "form");
    }
    body[r] = 0;
    settings_t s = *settings_get();
    form_get(body, "url", s.server_url, sizeof(s.server_url));
    char secret[129] = { 0 };
    if (form_get(body, "secret", secret, sizeof(secret)) && secret[0]) {
        strncpy(s.device_secret, secret, sizeof(s.device_secret) - 1);
    }
    char *ca = malloc(4096);
    if (ca) {
        ca[0] = 0;
        if (form_get(body, "rootca", ca, 4096) && ca[0]) {
            settings_set_root_ca(ca);
        }
        free(ca);
    }
    free(body);
    settings_save(&s);
    return send_html(req, "<body style='font-family:system-ui;padding:40px'>Server saved. <a href=/>Back</a>");
}

static esp_err_t tune_post(httpd_req_t *req)
{
    char body[128];
    if (read_form(req, body, sizeof(body)) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "form");
    }
    settings_t s = *settings_get();
    char tmp[8];
    if (form_get(body, "bright", tmp, sizeof(tmp))) {
        int v = atoi(tmp);
        if (v < 8) {
            v = 8;
        }
        if (v > 100) {
            v = 100;
        }
        s.brightness = (uint8_t)v;
    }
    if (form_get(body, "vol", tmp, sizeof(tmp))) {
        int v = atoi(tmp);
        if (v < 0) {
            v = 0;
        }
        if (v > 100) {
            v = 100;
        }
        s.volume = (uint8_t)v;
    }
    if (form_get(body, "mic", tmp, sizeof(tmp))) {
        int v = atoi(tmp);
        if (v < 0) {
            v = 0;
        }
        if (v > 7) {
            v = 7;
        }
        s.mic_gain = (uint8_t)v;
    }
    settings_save(&s);
    display_set_brightness(s.brightness);
    display_apply_pending_brightness();
    audio_set_volume(s.volume);
    audio_set_mic_gain(s.mic_gain);
    return send_html(req,
                     "<body style='font-family:system-ui;padding:40px'>"
                     "<p>Levels saved and applied.</p><p><a href=/>Back</a></p>");
}

static esp_err_t reboot_get(httpd_req_t *req)
{
    send_html(req, "<body style='font-family:system-ui;padding:40px'><h1>Rebooting…</h1>"
                   "<p>The watch will leave setup and reconnect to shop Wi-Fi.</p>");
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
    return ESP_OK;
}

static int dns_skip_name(const uint8_t *p, int n, int off)
{
    int hops = 0;
    while (off < n && hops++ < 20) {
        uint8_t len = p[off];
        if (len == 0) {
            return off + 1;
        }
        if ((len & 0xC0) == 0xC0) {
            return (off + 1 < n) ? off + 2 : -1;
        }
        off += 1 + (int)len;
    }
    return -1;
}

static void dns_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns socket");
        vTaskDelete(NULL);
        return;
    }
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        /* Bind only the AP address so STA DNS to the shop is not hijacked. */
        .sin_addr.s_addr = ESP_IP4TOADDR(192, 168, 4, 1),
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        ESP_LOGE(TAG, "dns bind 192.168.4.1:53");
        close(sock);
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "captive DNS catch-all 192.168.4.1:53");
    uint8_t buf[512];
    static const uint8_t ans_a[] = {
        0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01,
        0x00, 0x00, 0x00, 0x1E, 0x00, 0x04,
        192, 168, 4, 1
    };
    for (;;) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        int n = recvfrom(sock, buf, sizeof(buf) - 32, 0, (struct sockaddr *)&from, &flen);
        if (n < 12) {
            continue;
        }
        if (buf[2] & 0x80) {
            continue; /* already a response */
        }
        uint16_t qd = (uint16_t)((buf[4] << 8) | buf[5]);
        if (qd == 0) {
            qd = 1;
        }
        int q = 12;
        for (uint16_t i = 0; i < qd; i++) {
            q = dns_skip_name(buf, n, q);
            if (q < 0 || q + 4 > n) {
                q = -1;
                break;
            }
            q += 4;
        }
        if (q < 0 || q + (int)sizeof(ans_a) > (int)sizeof(buf)) {
            continue;
        }
        /* QR + AA (+ RD if the query had it), RA. Drop NS/AR (EDNS). */
        buf[2] = (uint8_t)(0x84 | (buf[2] & 0x01));
        buf[3] = 0x80;
        buf[6] = 0;
        buf[7] = 1;
        buf[8] = 0;
        buf[9] = 0;
        buf[10] = 0;
        buf[11] = 0;
        memcpy(buf + q, ans_a, sizeof(ans_a));
        sendto(sock, buf, q + (int)sizeof(ans_a), 0, (struct sockaddr *)&from, flen);
    }
}

static void disable_ap_ipv6(esp_netif_t *ap)
{
#if CONFIG_LWIP_IPV6
    struct netif *n = (struct netif *)esp_netif_get_netif_impl(ap);
    if (!n) {
        return;
    }
    n->ip6_autoconfig_enabled = 0;
#ifdef NETIF_FLAG_MLD6
    n->flags = (uint8_t)(n->flags & ~NETIF_FLAG_MLD6);
#endif
    for (int i = 0; i < LWIP_IPV6_NUM_ADDRESSES; i++) {
        netif_ip6_addr_set_state(n, i, IP6_ADDR_INVALID);
    }
    ESP_LOGI(TAG, "AP IPv6 disabled");
#else
    (void)ap;
#endif
}

static void configure_ap_ipv4(esp_netif_t *ap)
{
    esp_netif_dhcps_stop(ap);
    disable_ap_ipv6(ap);

    esp_netif_ip_info_t ip = { 0 };
    ip.ip.addr = ESP_IP4TOADDR(192, 168, 4, 1);
    ip.gw.addr = ESP_IP4TOADDR(192, 168, 4, 1);
    ip.netmask.addr = ESP_IP4TOADDR(255, 255, 255, 0);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_ip_info(ap, &ip));

    uint8_t offer_dns = 1;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(
        ap, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns)));

    esp_netif_dns_info_t dns = { 0 };
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ESP_IP4TOADDR(192, 168, 4, 1);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_dns_info(ap, ESP_NETIF_DNS_MAIN, &dns));

    /* RFC 8910 DHCP option 114 — this is what pops “Sign in to network”. */
    static char portal_uri[] = "http://192.168.4.1";
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(
        ap, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, portal_uri, strlen(portal_uri)));

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(ap));
    ESP_LOGI(TAG, "AP DHCP 192.168.4.1 DNS + captive URI");
}

static esp_err_t start_httpd(void)
{
    if (s_httpd) {
        goto dns;
    }
    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.max_uri_handlers = 26;
    /* 7 + 3 internal = 10. Must stay <= LWIP_MAX_SOCKETS. 12 used to fail start. */
    hc.max_open_sockets = 7;
    hc.lru_purge_enable = true;
    hc.stack_size = 10 * 1024;
    hc.max_req_hdr_len = 4096;
    hc.max_uri_len = 1024;
    hc.uri_match_fn = httpd_uri_match_wildcard;
    esp_err_t err = httpd_start(&s_httpd, &hc);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "httpd 7 sockets: %s, retry 4", esp_err_to_name(err));
        hc.max_open_sockets = 4;
        err = httpd_start(&s_httpd, &hc);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "httpd_start %s", esp_err_to_name(err));
            return err;
        }
    }
    httpd_register_err_handler(s_httpd, HTTPD_404_NOT_FOUND, http_404_handler);
    extern esp_err_t provision_files_get(httpd_req_t *req);
    extern esp_err_t provision_dl_get(httpd_req_t *req);
    extern esp_err_t provision_play_get(httpd_req_t *req);
    extern esp_err_t provision_rm_get(httpd_req_t *req);
    extern esp_err_t provision_ota_get(httpd_req_t *req);
    extern esp_err_t provision_ota_post(httpd_req_t *req);
    extern esp_err_t provision_format_get(httpd_req_t *req);
    extern esp_err_t provision_format_post(httpd_req_t *req);
    const httpd_uri_t uris[] = {
        { .uri = "/", .method = HTTP_GET, .handler = root_get },
        { .uri = "/wifi", .method = HTTP_POST, .handler = wifi_post },
        { .uri = "/server", .method = HTTP_POST, .handler = server_post },
        { .uri = "/tune", .method = HTTP_POST, .handler = tune_post },
        { .uri = "/reboot", .method = HTTP_GET, .handler = reboot_get },
        { .uri = "/files", .method = HTTP_GET, .handler = provision_files_get },
        { .uri = "/dl", .method = HTTP_GET, .handler = provision_dl_get },
        { .uri = "/play", .method = HTTP_GET, .handler = provision_play_get },
        { .uri = "/rm", .method = HTTP_GET, .handler = provision_rm_get },
        { .uri = "/ota", .method = HTTP_GET, .handler = provision_ota_get },
        { .uri = "/ota", .method = HTTP_POST, .handler = provision_ota_post },
        { .uri = "/format-sd", .method = HTTP_GET, .handler = provision_format_get },
        { .uri = "/format-sd", .method = HTTP_POST, .handler = provision_format_post },
        { .uri = "/generate_204", .method = HTTP_GET, .handler = captive_get },
        { .uri = "/gen_204", .method = HTTP_GET, .handler = captive_get },
        { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_get },
        { .uri = "/library/test/success.html", .method = HTTP_GET, .handler = captive_get },
        { .uri = "/ncsi.txt", .method = HTTP_GET, .handler = captive_get },
        { .uri = "/connecttest.txt", .method = HTTP_GET, .handler = captive_get },
        { .uri = "/canonical.html", .method = HTTP_GET, .handler = captive_get },
        { .uri = "/success.txt", .method = HTTP_GET, .handler = captive_get },
        /* Last: any other path the phone probes (google.com/..., connectivitycheck, etc.) */
        { .uri = "/*", .method = HTTP_GET, .handler = captive_get },
        { .uri = "/*", .method = HTTP_HEAD, .handler = captive_get },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        esp_err_t reg = httpd_register_uri_handler(s_httpd, &uris[i]);
        if (reg != ESP_OK) {
            ESP_LOGW(TAG, "uri %s: %s", uris[i].uri, esp_err_to_name(reg));
        }
    }
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);
dns:
    if (!s_dns_on) {
        s_dns_on = true;
        xTaskCreate(dns_task, "dns", 4096, NULL, 3, NULL);
    }
    return ESP_OK;
}

static esp_err_t start_softap(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_WIFI_INIT_STATE) {
        return err;
    }

    s_ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!s_ap_netif) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
        if (!s_ap_netif) {
            ESP_LOGE(TAG, "WIFI_AP_DEF create failed");
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "created WIFI_AP_DEF");
    }

    wifi_mode_t mode = WIFI_MODE_NULL;
    (void)esp_wifi_get_mode(&mode);
    if (mode == WIFI_MODE_STA || mode == WIFI_MODE_APSTA) {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_APSTA), TAG, "apsta");
    } else {
        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "ap");
    }

    uint8_t channel = 1;
    wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
    if (esp_wifi_get_channel(&channel, &second) != ESP_OK || channel == 0) {
        channel = 1;
    }

    wifi_config_t ap = { 0 };
    strncpy((char *)ap.ap.ssid, "ESP Assistant", sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len = strlen("ESP Assistant");
    ap.ap.channel = channel;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.beacon_interval = 100;
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ap), TAG, "ap cfg");
    (void)esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);

    esp_err_t st = esp_wifi_start();
    if (st != ESP_OK && st != ESP_ERR_WIFI_CONN) {
        ESP_LOGW(TAG, "wifi start: %s", esp_err_to_name(st));
    }

    configure_ap_ipv4(s_ap_netif);
    s_ap_on = true;
    ESP_LOGI(TAG, "AP 'ESP Assistant' ch=%u at http://192.168.4.1/", (unsigned)channel);
    return ESP_OK;
}

esp_err_t provision_open_ap(void)
{
    ESP_RETURN_ON_ERROR(start_softap(), TAG, "ap");
    return start_httpd();
}

esp_err_t provision_start(void)
{
    ESP_RETURN_ON_ERROR(start_softap(), TAG, "ap");
    return start_httpd();
}
