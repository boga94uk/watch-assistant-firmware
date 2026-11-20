#pragma once

#include "esp_err.h"
#include "board_pins.h"
#include "driver/i2c_master.h"
#include "esp_io_expander.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_bus_handle_t i2c;
    esp_io_expander_handle_t expander;
} board_t;

esp_err_t board_init(board_t *out);
i2c_master_bus_handle_t board_i2c(void);
esp_io_expander_handle_t board_expander(void);
bool board_bottom_button_pressed(void);
bool board_top_button_pressed(void);
void board_pa_enable(bool on);
int board_i2c_scan(uint8_t *found, int max);
bool board_i2c_has(uint8_t addr);
int board_battery_percent(void);
int board_battery_mv(void);
bool board_battery_charging(void);
bool board_vbus_in(void);
esp_err_t board_imu_init(void);
bool board_imu_shake(void);
esp_err_t board_rtc_apply_system_time(void);
esp_err_t board_power_off(void);

#ifdef __cplusplus
}
#endif
