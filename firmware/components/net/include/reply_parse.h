#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char type; /* T, A, or C */
    char message[768];
    char command[64];
    char audio_url[160];
    char conversation_id[48];
} voice_reply_t;

bool json_get_str(const char *json, const char *key, char *out, size_t n);
bool voice_reply_parse(const char *json, voice_reply_t *out);

#ifdef __cplusplus
}
#endif
