#include "display.h"

#include "board.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_sh8601.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "display";

#define FT3168_ADDR        0x38
#define FT3168_FINGERNUM   0x02
#define FT3168_X1POSH      0x03
#define FT3168_POWER_MODE  0xA5
#define FT3168_POWER_ACTIVE 0x00

static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static i2c_master_dev_handle_t s_touch;

static const sh8601_lcd_init_cmd_t s_init_cmds[] = {
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x44, (uint8_t[]){0x01, 0xD1}, 2, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x28}, 1, 10},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x6F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xBF}, 4, 0},
    {0x51, (uint8_t[]){0x00}, 1, 10},
    {0x29, (uint8_t[]){0x00}, 0, 10},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
};

esp_lcd_panel_handle_t display_panel(void)
{
    return s_panel;
}

static volatile int s_pending_bright = -1;

/* QSPI command encoding used by the SH8601 driver and the Waveshare Arduino example:
 * opcode 0x02, command in bits 15:8. Raw 0x51 is ignored on this bus. */
#define SH8601_QSPI_CMD(cmd)  ((int)((0x02 << 24) | (((cmd) & 0xff) << 8)))

static esp_err_t sh8601_cmd(uint8_t cmd, const uint8_t *data, size_t n)
{
    return esp_lcd_panel_io_tx_param(s_io, SH8601_QSPI_CMD(cmd), data, n);
}

esp_err_t display_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    s_pending_bright = (int)percent;
    return ESP_OK;
}

esp_err_t display_apply_pending_brightness(void)
{
    int percent = s_pending_bright;
    if (percent < 0) {
        return ESP_OK;
    }
    if (!s_io) {
        return ESP_ERR_INVALID_STATE;
    }
    s_pending_bright = -1;
    uint8_t val = (uint8_t)((percent * 255) / 100);
    /* Same as Arduino_SH8601::Display_Brightness — 0x51 only, 0–255. */
    esp_err_t err = sh8601_cmd(0x51, &val, 1);
    ESP_LOGI(TAG, "brightness %d%% -> 0x%02X (%s)", percent, val, esp_err_to_name(err));
    if (err != ESP_OK) {
        s_pending_bright = percent;
    }
    return err;
}

static int s_touch_fail;
static int64_t s_touch_skip_until;

static esp_err_t ft3168_write_u8(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_touch, buf, 2, 20);
}

static esp_err_t ft3168_read(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_touch, &reg, 1, data, len, 20);
}

static void ft3168_wake(void)
{
    (void)ft3168_write_u8(FT3168_POWER_MODE, FT3168_POWER_ACTIVE);
}

static esp_err_t touch_init(void)
{
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT3168_ADDR,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(board_i2c(), &dev, &s_touch), TAG, "ft3168 add");
    /* ACTIVE so polling works without the INT pin. */
    ESP_RETURN_ON_ERROR(ft3168_write_u8(FT3168_POWER_MODE, FT3168_POWER_ACTIVE), TAG, "pwr");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_LOGI(TAG, "FT3168 poll mode");
    return ESP_OK;
}

void display_touch_reset_backoff(void)
{
    s_touch_fail = 0;
    s_touch_skip_until = 0;
}

bool display_touch_read(uint16_t *x, uint16_t *y)
{
    if (!s_touch || !x || !y) {
        return false;
    }
    int64_t now = esp_timer_get_time();
    if (now < s_touch_skip_until) {
        return false;
    }
    uint8_t n = 0;
    if (ft3168_read(FT3168_FINGERNUM, &n, 1) != ESP_OK) {
        s_touch_fail++;
        if (s_touch_fail == 1 || (s_touch_fail % 40) == 0) {
            ESP_LOGW(TAG, "touch nack x%d, waking", s_touch_fail);
            ft3168_wake();
        }
        if (s_touch_fail >= 3) {
            s_touch_skip_until = now + 250000;
        }
        return false;
    }
    if (s_touch_fail) {
        ESP_LOGI(TAG, "touch recovered after %d nacks", s_touch_fail);
        s_touch_fail = 0;
    }
    n &= 0x0F;
    if (n == 0 || n > 2) {
        return false;
    }
    uint8_t xy[4];
    if (ft3168_read(FT3168_X1POSH, xy, 4) != ESP_OK) {
        return false;
    }
    *x = (uint16_t)(((xy[0] & 0x0F) << 8) | xy[1]);
    *y = (uint16_t)(((xy[2] & 0x0F) << 8) | xy[3]);
    if (*x >= BOARD_LCD_H_RES) {
        *x = BOARD_LCD_H_RES - 1;
    }
    if (*y >= BOARD_LCD_V_RES) {
        *y = BOARD_LCD_V_RES - 1;
    }
    return true;
}

esp_err_t display_init(esp_lcd_panel_io_color_trans_done_cb_t on_color_done, void *user_ctx)
{
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(
        BOARD_LCD_PCLK,
        BOARD_LCD_SDIO0,
        BOARD_LCD_SDIO1,
        BOARD_LCD_SDIO2,
        BOARD_LCD_SDIO3,
        BOARD_LCD_H_RES * BOARD_LCD_V_RES * 2);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO), TAG, "spi");

    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(BOARD_LCD_CS, on_color_done, user_ctx);
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BOARD_LCD_SPI_HOST, &io_config, &s_io), TAG, "io");

    sh8601_vendor_config_t vendor = {
        .init_cmds = s_init_cmds,
        .init_cmds_size = sizeof(s_init_cmds) / sizeof(s_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = &vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_sh8601(s_io, &panel_config, &s_panel), TAG, "sh8601");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "on");
    ESP_RETURN_ON_ERROR(touch_init(), TAG, "touch");
    ESP_LOGI(TAG, "AMOLED 368x448 + FT3168");
    return ESP_OK;
}
