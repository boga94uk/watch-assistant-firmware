#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RECQ_MAX 8

esp_err_t recq_init(void);
int recq_count(void);
bool recq_full(void);
esp_err_t recq_push(const uint8_t *wav, size_t len);
esp_err_t recq_head_path(char *path, size_t n);
const uint8_t *recq_ram_head(size_t *len);
esp_err_t recq_pop(void);
esp_err_t recq_clear(void);
const char *recq_last_error(void);
void recq_dump(void);

#ifdef __cplusplus
}
#endif
