#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SH8601 QSPI AMOLED */
#define BOARD_LCD_SDIO0        GPIO_NUM_4
#define BOARD_LCD_SDIO1        GPIO_NUM_5
#define BOARD_LCD_SDIO2        GPIO_NUM_6
#define BOARD_LCD_SDIO3        GPIO_NUM_7
#define BOARD_LCD_PCLK         GPIO_NUM_11
#define BOARD_LCD_CS           GPIO_NUM_12
#define BOARD_LCD_H_RES        368
#define BOARD_LCD_V_RES        448
#define BOARD_LCD_SPI_HOST     SPI2_HOST

/* Shared I2C: touch, expander, AXP2101, IMU, RTC, ES8311 control */
#define BOARD_I2C_SDA          GPIO_NUM_15
#define BOARD_I2C_SCL          GPIO_NUM_14
#define BOARD_I2C_PORT         I2C_NUM_0
#define BOARD_I2C_FREQ_HZ      200000

#define BOARD_TOUCH_INT        GPIO_NUM_21
#define BOARD_TOUCH_I2C_ADDR   0x38

#define BOARD_EXPANDER_ADDR    0x20  /* TCA9554 A2:A0 = 000 */
#define BOARD_EXPANDER_PIN_RST0   (1 << 0)
#define BOARD_EXPANDER_PIN_RST1   (1 << 1)
#define BOARD_EXPANDER_PIN_RST2   (1 << 2)
#define BOARD_EXPANDER_PIN_BTN    (1 << 4)  /* bottom button, pressed = 1 */
#define BOARD_EXPANDER_PIN_RST6   (1 << 6)
#define BOARD_EXPANDER_PIN_EN7    (1 << 7)

#define BOARD_BTN_TOP          GPIO_NUM_0

/* ES8311 I2S */
#define BOARD_I2S_MCLK         GPIO_NUM_16
#define BOARD_I2S_BCLK         GPIO_NUM_9
#define BOARD_I2S_WS           GPIO_NUM_45
#define BOARD_I2S_DOUT         GPIO_NUM_8   /* ESP → codec (speaker) */
#define BOARD_I2S_DIN          GPIO_NUM_10  /* codec → ESP (mic) */
#define BOARD_PA_EN            GPIO_NUM_46
#define BOARD_ES8311_ADDR      0x18

/* SDMMC 1-bit */
#define BOARD_SD_CLK           GPIO_NUM_2
#define BOARD_SD_CMD           GPIO_NUM_1
#define BOARD_SD_D0            GPIO_NUM_3

#define BOARD_AXP2101_ADDR     0x34
#define BOARD_RTC_ADDR         0x51
#define BOARD_IMU_ADDR         0x6B

#ifdef __cplusplus
}
#endif
