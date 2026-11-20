#pragma once

#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t display_init(esp_lcd_panel_io_color_trans_done_cb_t on_color_done, void *user_ctx);
esp_lcd_panel_handle_t display_panel(void);
esp_err_t display_set_brightness(uint8_t percent);
esp_err_t display_apply_pending_brightness(void);
bool display_touch_read(uint16_t *x, uint16_t *y);
void display_touch_reset_backoff(void);

#ifdef __cplusplus
}
#endif
