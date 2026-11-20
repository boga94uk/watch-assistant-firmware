// ============================================
// ES8311 CODEC DRIVER - Implementation
// ============================================
// Wrapper around ES8311 codec functions

#include "es8311_driver.h"
#include "audio_pins.h"

// Include the actual ES8311 driver
// These files need to be copied from Platformio_audio_test/src or Device_Rom_Complete
extern "C" {
    #include "es8311.h"
}

// Configuration constants
#define EXAMPLE_SAMPLE_RATE 16000
#define EXAMPLE_VOICE_VOLUME 80
#define EXAMPLE_MIC_GAIN 4

// Global ES8311 codec configuration
static es8311_handle_t es_handle = NULL;

esp_err_t es8311_codec_init() {
    Serial.println("🎙️ Initializing ES8311 codec...");
    
    // Create ES8311 handle
    es_handle = es8311_create(I2C_NUM_0, ES8311_I2C_ADDRESS);
    if (es_handle == NULL) {
        Serial.println("❌ Failed to create ES8311 handle");
        return ESP_FAIL;
    }
    
    // Configure clock
    es8311_clock_config_t clk_cfg = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = EXAMPLE_SAMPLE_RATE * 256,
        .sample_frequency = EXAMPLE_SAMPLE_RATE
    };
    
    // Initialize codec
    if (es8311_init(es_handle, &clk_cfg, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        Serial.println("❌ ES8311 init failed");
        return ESP_FAIL;
    }
    
    // Configure sample frequency
    if (es8311_sample_frequency_config(es_handle, clk_cfg.mclk_frequency, clk_cfg.sample_frequency) != ESP_OK) {
        Serial.println("❌ ES8311 sample frequency config failed");
        return ESP_FAIL;
    }
    
    // Configure microphone
    if (es8311_microphone_config(es_handle, false) != ESP_OK) {
        Serial.println("❌ ES8311 microphone config failed");
        return ESP_FAIL;
    }
    
    // Set volume
    if (es8311_voice_volume_set(es_handle, EXAMPLE_VOICE_VOLUME, NULL) != ESP_OK) {
        Serial.println("❌ ES8311 volume set failed");
        return ESP_FAIL;
    }
    
    // Set mic gain
    if (es8311_microphone_gain_set(es_handle, (es8311_mic_gain_t)EXAMPLE_MIC_GAIN) != ESP_OK) {
        Serial.println("❌ ES8311 mic gain set failed");
        return ESP_FAIL;
    }
    
    // Unmute
    if (es8311_voice_mute(es_handle, false) != ESP_OK) {
        Serial.println("❌ ES8311 unmute failed");
        return ESP_FAIL;
    }
    
    Serial.println("✅ ES8311 codec initialized");
    return ESP_OK;
}

esp_err_t es8311_codec_set_voice_volume(uint8_t volume) {
    if (es_handle == NULL) {
        return ESP_FAIL;
    }
    return es8311_voice_volume_set(es_handle, volume, NULL);
}

esp_err_t es8311_codec_set_mic_gain(uint8_t gain) {
    if (es_handle == NULL) {
        return ESP_FAIL;
    }
    return es8311_microphone_gain_set(es_handle, (es8311_mic_gain_t)gain);
}

esp_err_t es8311_codec_set_voice_mute(bool mute) {
    if (es_handle == NULL) {
        return ESP_FAIL;
    }
    return es8311_voice_mute(es_handle, mute);
}
