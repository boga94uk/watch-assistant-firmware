#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t sd_mount(void);
esp_err_t sd_format(void);
bool sd_ok(void);
const char *sd_root(void);
bool sd_path_ok(const char *path);

#ifdef __cplusplus
}
#endif
