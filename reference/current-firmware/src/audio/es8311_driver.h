// ============================================
// ES8311 CODEC DRIVER - Header
// ============================================
// Simple wrapper around ES8311 codec functions
// Using the working pattern from audio test

#pragma once

#include <Arduino.h>
#include "driver/i2c.h"
#include "esp_err.h"

// ES8311 functions (implemented in es8311_driver.cpp)
// These wrap the actual ES8311 driver from Device_Rom_Complete

/**
 * Initialize ES8311 codec
 * NOTE: I2C must be initialized BEFORE calling this!
 * NOTE: Wire.begin() must be called BEFORE this!
 * @return ESP_OK on success
 */
esp_err_t es8311_codec_init();

/**
 * Set speaker volume
 * @param volume 0-100
 * @return ESP_OK on success
 */
esp_err_t es8311_codec_set_voice_volume(uint8_t volume);

/**
 * Set microphone gain
 * @param gain 0-7
 * @return ESP_OK on success
 */
esp_err_t es8311_codec_set_mic_gain(uint8_t gain);

/**
 * Mute/unmute output
 * @param mute true to mute, false to unmute
 * @return ESP_OK on success
 */
esp_err_t es8311_codec_set_voice_mute(bool mute);
