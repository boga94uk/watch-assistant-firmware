#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t net_init(void);
esp_err_t net_upload_last_recording(void);
esp_err_t net_execute(const char *conv, const char *cmd);
void net_cancel(void);
bool net_is_connected(void);
int net_wifi_bars(void);
int net_wifi_rssi(void);

#ifdef __cplusplus
}
#endif
