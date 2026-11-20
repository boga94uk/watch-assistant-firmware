#include "audio.h"
#include "recq.h"
#include "sd.h"

#include "esp_app_format.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "provx";

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

static bool query_get(httpd_req_t *req, const char *key, char *out, size_t n)
{
    char q[320];
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) != ESP_OK) {
        return false;
    }
    if (httpd_query_key_value(q, key, out, n) != ESP_OK) {
        return false;
    }
    url_decode(out);
    return true;
}

static esp_err_t send_html(httpd_req_t *req, const char *html)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

static void list_dir_rows(httpd_req_t *req, const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    char line[768];
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') {
            continue;
        }
        char full[300];
        snprintf(full, sizeof(full), "%s/%.80s", dir, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            snprintf(line, sizeof(line),
                     "<tr><td colspan=2><a href='/files?path=%s'>dir %s/</a></td><td></td></tr>",
                     full, ent->d_name);
            httpd_resp_sendstr_chunk(req, line);
            continue;
        }
        int n = snprintf(line, sizeof(line),
                         "<tr><td>%.80s</td><td>%ld B</td><td>", ent->d_name, (long)st.st_size);
        if (n < 0 || n >= (int)sizeof(line)) {
            continue;
        }
        if (strstr(ent->d_name, ".wav") || strstr(ent->d_name, ".WAV")) {
            n += snprintf(line + n, sizeof(line) - (size_t)n,
                          "<a class=btn href='/play?f=%s'>Play on watch</a> ", full);
        }
        if (n > 0 && n < (int)sizeof(line)) {
            snprintf(line + n, sizeof(line) - (size_t)n,
                     "<a class=btn href='/dl?f=%s'>Download</a> "
                     "<a class='btn warn' href='/rm?f=%s'>Delete</a></td></tr>",
                     full, full);
        }
        httpd_resp_sendstr_chunk(req, line);
    }
    closedir(d);
}

esp_err_t provision_files_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>microSD</title><style>"
        "body{font-family:sans-serif;padding:16px;background:#f5f5f5}"
        ".wrap{max-width:720px;margin:0 auto;background:#fff;padding:20px;border-radius:12px}"
        "table{width:100%;border-collapse:collapse}td,th{padding:10px;border-bottom:1px solid #eee;text-align:left}"
        ".btn{display:inline-block;padding:8px 12px;background:#667eea;color:#fff;text-decoration:none;border-radius:8px;margin:2px;font-size:13px}"
        ".warn{background:#dc3545}.back{background:#6c757d}"
        "</style></head><body><div class=wrap><h1>microSD</h1>");
    if (!sd_ok()) {
        httpd_resp_sendstr_chunk(req,
            "<p>No SD card mounted.</p>"
            "<p><a class='btn warn' href=/format-sd>Format / empty queue</a> "
            "<a class='btn back' href=/>Back</a></p>");
        httpd_resp_sendstr_chunk(req, "</div></body></html>");
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
    }
    char path[192] = "/sdcard";
    query_get(req, "path", path, sizeof(path));
    if (!sd_path_ok(path)) {
        strcpy(path, "/sdcard");
    }
    char head[256];
    snprintf(head, sizeof(head), "<p>Path: <b>%s</b></p><table><tr><th>Name</th><th>Size</th><th></th></tr>", path);
    httpd_resp_sendstr_chunk(req, head);
    list_dir_rows(req, path);
    httpd_resp_sendstr_chunk(req,
        "</table><p><a class='btn back' href=/>Back</a> "
        "<a class='btn warn' href=/format-sd>Format / empty queue</a></p></div></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

esp_err_t provision_dl_get(httpd_req_t *req)
{
    char path[192];
    if (!query_get(req, "f", path, sizeof(path)) || !sd_path_ok(path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
    }
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    char disp[96];
    snprintf(disp, sizeof(disp), "attachment; filename=\"%.64s\"", name);
    httpd_resp_set_type(req, strstr(name, ".wav") ? "audio/wav" : "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", disp);
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            break;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t provision_play_get(httpd_req_t *req)
{
    char path[192];
    if (!query_get(req, "f", path, sizeof(path)) || !sd_path_ok(path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    esp_err_t err = audio_play_file(path);
    char html[320];
    snprintf(html, sizeof(html),
             "<body style='font-family:sans-serif;padding:40px'><h1>%s</h1><p>%s</p>"
             "<p><a href=/files>Back to files</a></p>",
             err == ESP_OK ? "Playing on the watch" : "Play failed", path);
    return send_html(req, html);
}

esp_err_t provision_rm_get(httpd_req_t *req)
{
    char path[192];
    if (!query_get(req, "f", path, sizeof(path)) || !sd_path_ok(path)) {
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad path");
    }
    int rc = remove(path);
    char html[280];
    snprintf(html, sizeof(html),
             "<meta http-equiv=refresh content='1;url=/files'><body style='font-family:sans-serif;padding:40px'>"
             "<h1>%s</h1>", rc == 0 ? "Deleted" : "Delete failed");
    return send_html(req, html);
}

static void drain_body(httpd_req_t *req)
{
    char tmp[64];
    int left = req->content_len;
    while (left > 0) {
        int n = httpd_req_recv(req, tmp, left > (int)sizeof(tmp) ? (int)sizeof(tmp) : left);
        if (n <= 0) {
            break;
        }
        left -= n;
    }
}

static esp_err_t do_wipe(httpd_req_t *req, const char *what)
{
    esp_err_t err = ESP_OK;
    const char *title;
    const char *detail;
    if (what && strcmp(what, "queue") == 0) {
        err = recq_clear();
        title = err == ESP_OK ? "Queue emptied" : "Clear failed";
        detail = err == ESP_OK ? "Failed uploads were removed. New recordings can be sent." : esp_err_to_name(err);
    } else {
        err = sd_format();
        if (err == ESP_OK) {
            (void)recq_init();
        }
        title = err == ESP_OK ? "Card formatted" : "Format failed";
        detail = err == ESP_OK ? "The microSD card is empty." : esp_err_to_name(err);
    }
    char html[400];
    snprintf(html, sizeof(html),
             "<!doctype html><html><head><meta charset=utf-8>"
             "<meta http-equiv=refresh content='2;url=/files'>"
             "<body style='font-family:sans-serif;padding:32px'>"
             "<h1>%s</h1><p>%s</p><p><a href=/files>Back</a></p>",
             title, detail);
    return send_html(req, html);
}

esp_err_t provision_format_get(httpd_req_t *req)
{
    char wipe[12] = { 0 };
    if (query_get(req, "wipe", wipe, sizeof(wipe))) {
        return do_wipe(req, wipe);
    }
    /* Tiny page: Chrome on captive portals often 431s a POST with long headers. */
    return send_html(req,
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>microSD</title></head>"
        "<body style='font-family:sans-serif;padding:24px;max-width:520px'>"
        "<h1>microSD</h1>"
        "<p>Failed uploads sit in the queue on this card. Empty the queue, or wipe the whole card.</p>"
        "<p><a href='/format-sd?wipe=queue'>Empty upload queue</a></p>"
        "<p><a href='/format-sd?wipe=1'>Format entire card</a> (erases everything)</p>"
        "<p><a href=/files>Cancel</a></p>"
        "</body></html>");
}

esp_err_t provision_format_post(httpd_req_t *req)
{
    drain_body(req);
    return do_wipe(req, "1");
}

esp_err_t provision_ota_get(httpd_req_t *req)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *upd = esp_ota_get_next_update_partition(NULL);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr_chunk(req,
        "<!doctype html><html><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>OTA</title><style>body{font-family:sans-serif;padding:20px;background:#f5f5f5}"
        ".wrap{max-width:640px;margin:0 auto;background:#fff;padding:24px;border-radius:12px}"
        ".btn{padding:12px 20px;background:#667eea;color:#fff;border:0;border-radius:8px;font-weight:700}"
        ".bar{height:16px;background:#eee;border-radius:8px;overflow:hidden;margin-top:12px}"
        ".fill{height:100%;width:0;background:#667eea}</style></head><body><div class=wrap>"
        "<h1>Firmware update</h1>");
    char info[240];
    snprintf(info, sizeof(info),
             "<p>Running: <b>%s</b></p><p>Write to: <b>%s</b> (%lu KB)</p>",
             run ? run->label : "?", upd ? upd->label : "?",
             (unsigned long)((upd ? upd->size : 0) / 1024));
    httpd_resp_sendstr_chunk(req, info);
    httpd_resp_sendstr_chunk(req,
        "<p>Upload watch_assistant.bin. The other OTA slot is used; failed boots roll back.</p>"
        "<input id=f type=file accept=.bin>"
        "<p><button class=btn onclick=go()>Upload and reboot</button></p>"
        "<div class=bar><div class=fill id=p></div></div><p id=s></p>"
        "<p><a href=/>Back</a></p></div>"
        "<script>function go(){var f=document.getElementById('f').files[0];if(!f){alert('Choose a .bin');return;}"
        "var x=new XMLHttpRequest();x.open('POST','/ota');x.setRequestHeader('Content-Type','application/octet-stream');"
        "x.upload.onprogress=function(e){if(e.lengthComputable){document.getElementById('p').style.width=(100*e.loaded/e.total)+'%';"
        "document.getElementById('s').innerText=Math.round(e.loaded/1024)+' / '+Math.round(e.total/1024)+' KB';}};"
        "x.onload=function(){document.getElementById('s').innerText=x.responseText;setTimeout(function(){location.href='/'},4000)};"
        "x.send(f);}</script></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

esp_err_t provision_ota_post(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no ota partition");
    }
    ESP_LOGI(TAG, "OTA begin %s size=%d", part->label, req->content_len);
    esp_ota_handle_t h;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &h);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }
    char buf[2048];
    int left = req->content_len;
    while (left > 0) {
        int n = httpd_req_recv(req, buf, left > (int)sizeof(buf) ? (int)sizeof(buf) : left);
        if (n <= 0) {
            esp_ota_abort(h);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv");
        }
        err = esp_ota_write(h, buf, n);
        if (err != ESP_OK) {
            esp_ota_abort(h);
            return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
        }
        left -= n;
    }
    err = esp_ota_end(h);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));
    }
    httpd_resp_send(req, "Update written. Rebooting.", HTTPD_RESP_USE_STRLEN);
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}
