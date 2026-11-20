#include "reply_parse.h"
#include <string.h>

bool json_get_str(const char *json, const char *key, char *out, size_t n)
{
    if (!json || !key || !out || n == 0) {
        return false;
    }
    out[0] = 0;
    char pat[48];
    size_t k = strlen(key);
    if (k + 3 >= sizeof(pat)) {
        return false;
    }
    pat[0] = '"';
    memcpy(pat + 1, key, k);
    pat[1 + k] = '"';
    pat[2 + k] = 0;
    const char *p = strstr(json, pat);
    if (!p) {
        return false;
    }
    p = strchr(p + k + 2, ':');
    if (!p) {
        return false;
    }
    p++;
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (*p != '"') {
        return false;
    }
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < n) {
        if (*p == '\\' && p[1]) {
            p++;
        }
        out[i++] = *p++;
    }
    out[i] = 0;
    return true;
}

bool voice_reply_parse(const char *json, voice_reply_t *out)
{
    if (!json || !out) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->type = 'T';
    char typ[16] = { 0 };
    if (json_get_str(json, "type", typ, sizeof(typ))) {
        if (strcmp(typ, "ask") == 0) {
            out->type = 'A';
        } else if (strcmp(typ, "confirm") == 0) {
            out->type = 'C';
        }
    }
    json_get_str(json, "message", out->message, sizeof(out->message));
    json_get_str(json, "command", out->command, sizeof(out->command));
    json_get_str(json, "audio_url", out->audio_url, sizeof(out->audio_url));
    json_get_str(json, "conversation_id", out->conversation_id, sizeof(out->conversation_id));
    return true;
}
