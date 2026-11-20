// ============================================
// AUDIO PIN DEFINITIONS
// ============================================
// Pin configuration for ES8311 codec + I2S

#pragma once

#include <driver/i2s.h>

// I2S Port
#define I2S_PORT I2S_NUM_0

// I2C Pins (shared with IO Expander)
#define IIC_SDA 15
#define IIC_SCL 14

// ES8311 I2S Pins (matches ESP-IDF 06_I2SCodec example)
#define MCLKPIN  16  // Master Clock
#define BCLKPIN  9   // Bit Clock
#define WSPIN    45  // Word Select (LRCK)
#define DOPIN    8   // Data OUT from ESP32 to codec (speaker)
#define DIPIN    10  // Data IN from codec to ESP32 (microphone)
#define PA       46  // Power Amplifier Enable

// ES8311 I2C Address (CE pin determines address)
#define ES8311_I2C_ADDRESS 0x18  // CE pin low = 0x18, CE pin high = 0x19
