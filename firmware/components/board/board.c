#include "board.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_io_expander_tca9554.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>

static const char *TAG = "board";
/* UK: GMT in winter, BST (UTC+1) last Sunday Mar 01:00 → last Sunday Oct 02:00. */
#define LOCAL_TZ "GMT0BST,M3.5.0/1,M10.5.0/2"
static board_t s_board;
static i2c_master_dev_handle_t s_axp;
static i2c_master_dev_handle_t s_imu;
static i2c_master_dev_handle_t s_rtc;
static float s_imu_last_mag = 1.0f;

#define AXP2101_STATUS1            0x00
#define AXP2101_STATUS2            0x01
#define AXP2101_COMMON_CONFIG      0x10
#define AXP2101_CHARGE_GAUGE_WDT   0x18
#define AXP2101_ADC_CHANNEL_CTRL   0x30
#define AXP2101_ADC_VBAT_H         0x34
#define AXP2101_ADC_VBAT_L         0x35
#define AXP2101_BAT_DET_CTRL       0x68
#define AXP2101_FUEL_GAUGE_CTRL    0xA2
#define AXP2101_BAT_PERCENT        0xA4

static esp_err_t axp_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_axp, buf, 2, 50);
}

static esp_err_t axp_read(uint8_t reg, uint8_t *val)
{
    return i2c_master_transmit_receive(s_axp, &reg, 1, val, 1, 50);
}

static esp_err_t axp_update_bit(uint8_t reg, uint8_t bit)
{
    uint8_t v = 0;
    ESP_RETURN_ON_ERROR(axp_read(reg, &v), TAG, "axp rd");
    v = (uint8_t)(v | (1u << bit));
    return axp_write(reg, v);
}

static int mv_to_percent(int mv)
{
    if (mv >= 4100) {
        return 100;
    }
    if (mv >= 4000) {
        return 90;
    }
    if (mv >= 3900) {
        return 75;
    }
    if (mv >= 3800) {
        return 60;
    }
    if (mv >= 3700) {
        return 45;
    }
    if (mv >= 3600) {
        return 30;
    }
    if (mv >= 3500) {
        return 18;
    }
    if (mv >= 3400) {
        return 8;
    }
    if (mv >= 3300) {
        return 3;
    }
    return 0;
}

static int axp_vbat_mv(void)
{
    uint8_t hi = 0, lo = 0;
    if (axp_read(AXP2101_ADC_VBAT_H, &hi) != ESP_OK || axp_read(AXP2101_ADC_VBAT_L, &lo) != ESP_OK) {
        return 0;
    }
    return ((hi & 0x1F) << 8) | lo;
}

static esp_err_t axp_init(void)
{
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_AXP2101_ADDR,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_board.i2c, &dev, &s_axp), TAG, "axp add");
    /* Same enables as the working Arduino XPowersLib init. */
    (void)axp_update_bit(AXP2101_BAT_DET_CTRL, 0);      /* batt detection */
    (void)axp_update_bit(AXP2101_ADC_CHANNEL_CTRL, 0);  /* batt voltage ADC */
    (void)axp_update_bit(AXP2101_ADC_CHANNEL_CTRL, 2);  /* VBUS ADC */
    (void)axp_update_bit(AXP2101_ADC_CHANNEL_CTRL, 3);  /* VSYS ADC */
    (void)axp_update_bit(AXP2101_ADC_CHANNEL_CTRL, 4);  /* die temp ADC */
    (void)axp_update_bit(AXP2101_CHARGE_GAUGE_WDT, 3);  /* enableGauge */
    (void)axp_update_bit(AXP2101_CHARGE_GAUGE_WDT, 1);  /* cell charge */
    (void)axp_update_bit(AXP2101_FUEL_GAUGE_CTRL, 4);   /* do not write gauge ROM */
    (void)axp_update_bit(AXP2101_FUEL_GAUGE_CTRL, 0);   /* fuel gauge on */

    uint8_t st1 = 0, st2 = 0, pct = 0;
    (void)axp_read(AXP2101_STATUS1, &st1);
    (void)axp_read(AXP2101_STATUS2, &st2);
    (void)axp_read(AXP2101_BAT_PERCENT, &pct);
    ESP_LOGI(TAG, "AXP2101 st1=0x%02X st2=0x%02X pct=%u vbat=%d mV", st1, st2, pct, axp_vbat_mv());
    return ESP_OK;
}

int board_battery_mv(void)
{
    if (!s_axp) {
        return 0;
    }
    return axp_vbat_mv();
}

int board_battery_percent(void)
{
    if (!s_axp) {
        return -1;
    }
    uint8_t st = 0, pct = 0;
    if (axp_read(AXP2101_STATUS1, &st) != ESP_OK) {
        return -1;
    }
    const bool present = (st & (1 << 3)) != 0;
    const int mv = axp_vbat_mv();
    if (axp_read(AXP2101_BAT_PERCENT, &pct) == ESP_OK && present && pct > 0 && pct <= 100) {
        return pct;
    }
    if (mv > 2500 && mv < 4600) {
        return mv_to_percent(mv);
    }
    if (present && pct <= 100) {
        return pct;
    }
    return -1;
}

bool board_battery_charging(void)
{
    uint8_t st = 0;
    if (!s_axp || axp_read(AXP2101_STATUS2, &st) != ESP_OK) {
        return false;
    }
    return (st >> 5) == 0x01;
}

bool board_vbus_in(void)
{
    uint8_t st1 = 0, st2 = 0;
    if (!s_axp) {
        return false;
    }
    if (axp_read(AXP2101_STATUS1, &st1) != ESP_OK || axp_read(AXP2101_STATUS2, &st2) != ESP_OK) {
        return false;
    }
    /* XPowers isVbusIn: STATUS2 bit3 == 0 and STATUS1 bit5 (VBUS good). */
    return ((st2 & (1 << 3)) == 0) && ((st1 & (1 << 5)) != 0);
}

esp_err_t board_power_off(void)
{
    if (!s_axp) {
        return ESP_ERR_INVALID_STATE;
    }
    /* XPowersLib AXP2101::shutdown — COMMON_CONFIG 0x10 bit 0. */
    ESP_LOGW(TAG, "AXP2101 softoff");
    return axp_update_bit(AXP2101_COMMON_CONFIG, 0);
}

static uint8_t bcd2dec(uint8_t v)
{
    return (uint8_t)(((v >> 4) * 10) + (v & 0x0f));
}

esp_err_t board_rtc_apply_system_time(void)
{
    if (!s_board.i2c) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_rtc) {
        i2c_device_config_t dev = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BOARD_RTC_ADDR,
            .scl_speed_hz = BOARD_I2C_FREQ_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_board.i2c, &dev, &s_rtc), TAG, "rtc add");
    }
    uint8_t reg = 0x04;
    uint8_t d[7] = { 0 };
    ESP_RETURN_ON_ERROR(i2c_master_transmit_receive(s_rtc, &reg, 1, d, 7, 50), TAG, "rtc rd");
    struct tm tm = { 0 };
    tm.tm_sec = bcd2dec(d[0] & 0x7f);
    tm.tm_min = bcd2dec(d[1] & 0x7f);
    tm.tm_hour = bcd2dec(d[2] & 0x3f);
    tm.tm_mday = bcd2dec(d[3] & 0x3f);
    tm.tm_mon = bcd2dec(d[5] & 0x1f) - 1;
    tm.tm_year = bcd2dec(d[6]) + 100; /* 20xx */
    if (tm.tm_year < (2024 - 1900) || tm.tm_mon < 0 || tm.tm_mon > 11 || tm.tm_mday < 1) {
        ESP_LOGW(TAG, "RTC unset %04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
        return ESP_ERR_INVALID_STATE;
    }
    setenv("TZ", "UTC", 1);
    tzset();
    time_t t = mktime(&tm);
    if (t < 1700000000) {
        ESP_LOGW(TAG, "RTC mktime %ld", (long)t);
        setenv("TZ", LOCAL_TZ, 1);
        tzset();
        return ESP_ERR_INVALID_STATE;
    }
    struct timeval tv = { .tv_sec = t };
    settimeofday(&tv, NULL);
    setenv("TZ", LOCAL_TZ, 1);
    tzset();
    ESP_LOGI(TAG, "RTC %04d-%02d-%02d %02d:%02d:%02d UTC (display TZ %s)",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
             LOCAL_TZ);
    return ESP_OK;
}

i2c_master_bus_handle_t board_i2c(void)
{
    return s_board.i2c;
}

esp_io_expander_handle_t board_expander(void)
{
    return s_board.expander;
}

void board_pa_enable(bool on)
{
    gpio_set_level(BOARD_PA_EN, on ? 1 : 0);
}

bool board_bottom_button_pressed(void)
{
    uint32_t level = 0;
    if (!s_board.expander) {
        return false;
    }
    if (esp_io_expander_get_level(s_board.expander, BOARD_EXPANDER_PIN_BTN, &level) != ESP_OK) {
        return false;
    }
    return level != 0;
}

static esp_err_t expander_release_panel(void)
{
    ESP_RETURN_ON_ERROR(
        esp_io_expander_new_i2c_tca9554(s_board.i2c, BOARD_EXPANDER_ADDR, &s_board.expander),
        TAG, "tca9554");

    const uint32_t rst = BOARD_EXPANDER_PIN_RST0 | BOARD_EXPANDER_PIN_RST1 |
                         BOARD_EXPANDER_PIN_RST2 | BOARD_EXPANDER_PIN_RST6;
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(s_board.expander, rst | BOARD_EXPANDER_PIN_EN7, IO_EXPANDER_OUTPUT), TAG, "dir");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_dir(s_board.expander, BOARD_EXPANDER_PIN_BTN, IO_EXPANDER_INPUT), TAG, "btn dir");

    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_board.expander, rst, 0), TAG, "rst low");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_board.expander, rst, 1), TAG, "rst high");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(s_board.expander, BOARD_EXPANDER_PIN_EN7, 1), TAG, "en7");

    ESP_LOGI(TAG, "TCA9554 panel reset done");
    return ESP_OK;
}

esp_err_t board_init(board_t *out)
{
    gpio_config_t pa = {
        .pin_bit_mask = 1ULL << BOARD_PA_EN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&pa), TAG, "pa gpio");
    gpio_set_level(BOARD_PA_EN, 0);

    gpio_config_t top = {
        .pin_bit_mask = 1ULL << BOARD_BTN_TOP,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&top), TAG, "top btn");

    i2c_master_bus_config_t bus = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags = { .enable_internal_pullup = true },
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus, &s_board.i2c), TAG, "i2c");

    ESP_RETURN_ON_ERROR(expander_release_panel(), TAG, "expander");
    if (axp_init() != ESP_OK) {
        ESP_LOGW(TAG, "AXP2101 init failed");
    }

    uint8_t found[16];
    int n = board_i2c_scan(found, 16);
    ESP_LOGI(TAG, "I2C scan: %d device(s)", n);
    for (int i = 0; i < n; i++) {
        ESP_LOGI(TAG, "  0x%02X", found[i]);
    }

    if (out) {
        *out = s_board;
    }
    /* Driver logs every NACK at ERROR; touch polls ~50 Hz and that flood freezes USB-JTAG. */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    setenv("TZ", LOCAL_TZ, 1);
    tzset();
    (void)board_rtc_apply_system_time();
    (void)board_imu_init();
    ESP_LOGI(TAG, "board ready (S3 N16R8 368x448)");
    return ESP_OK;
}

#define QMI8658_WHOAMI  0x00
#define QMI8658_CTRL1   0x02
#define QMI8658_CTRL2   0x03
#define QMI8658_CTRL7   0x08
#define QMI8658_AX_L    0x35

static esp_err_t imu_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return i2c_master_transmit(s_imu, buf, 2, 20);
}

static esp_err_t imu_read(uint8_t reg, uint8_t *data, size_t n)
{
    return i2c_master_transmit_receive(s_imu, &reg, 1, data, n, 20);
}

esp_err_t board_imu_init(void)
{
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BOARD_IMU_ADDR,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_board.i2c, &dev, &s_imu), TAG, "imu add");
    uint8_t who = 0;
    if (imu_read(QMI8658_WHOAMI, &who, 1) != ESP_OK || who != 0x05) {
        ESP_LOGW(TAG, "QMI8658 whoami 0x%02X", who);
        return ESP_FAIL;
    }
    (void)imu_write(QMI8658_CTRL1, 0x40); /* auto-increment */
    (void)imu_write(QMI8658_CTRL2, 0x16); /* ±4g, 125 Hz */
    (void)imu_write(QMI8658_CTRL7, 0x01); /* accel on */
    ESP_LOGI(TAG, "QMI8658 accel ready");
    return ESP_OK;
}

bool board_imu_shake(void)
{
    if (!s_imu) {
        return false;
    }
    uint8_t raw[6];
    if (imu_read(QMI8658_AX_L, raw, 6) != ESP_OK) {
        return false;
    }
    int16_t ax = (int16_t)((raw[1] << 8) | raw[0]);
    int16_t ay = (int16_t)((raw[3] << 8) | raw[2]);
    int16_t az = (int16_t)((raw[5] << 8) | raw[4]);
    const float scale = 4.0f / 32768.0f;
    float x = ax * scale, y = ay * scale, z = az * scale;
    float mag = sqrtf(x * x + y * y + z * z);
    float d = mag - s_imu_last_mag;
    if (d < 0) {
        d = -d;
    }
    s_imu_last_mag = mag;
    return d > 0.35f || mag > 1.85f;
}

bool board_top_button_pressed(void)
{
    return gpio_get_level(BOARD_BTN_TOP) == 0;
}

int board_i2c_scan(uint8_t *found, int max)
{
    int n = 0;
    if (!s_board.i2c || !found || max <= 0) {
        return 0;
    }
    for (uint8_t addr = 0x08; addr < 0x78 && n < max; addr++) {
        if (i2c_master_probe(s_board.i2c, addr, 50) == ESP_OK) {
            found[n++] = addr;
        }
    }
    return n;
}

bool board_i2c_has(uint8_t addr)
{
    return s_board.i2c && i2c_master_probe(s_board.i2c, addr, 50) == ESP_OK;
}
