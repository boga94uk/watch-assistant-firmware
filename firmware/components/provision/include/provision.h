#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool provision_boot_hold(void);
esp_err_t provision_start(void);
esp_err_t provision_open_ap(void);

#ifdef __cplusplus
}
#endif
