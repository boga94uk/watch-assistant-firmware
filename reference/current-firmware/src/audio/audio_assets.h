#ifndef AUDIO_ASSETS_H
#define AUDIO_ASSETS_H

#include <Arduino.h>

// ============================================
// AUDIO ASSETS MANAGER
// Plays short WAV files from SPIFFS for UI feedback
// ============================================

/**
 * @brief Initialize SPIFFS and prepare for audio asset playback
 * Call this after audio_init() in setup()
 * @return true if SPIFFS mounted successfully
 */
bool audio_assets_init();

/**
 * @brief Play a WAV file from SPIFFS
 * Streams directly to I2S (blocking)
 * 
 * Expected format: 16kHz, mono, 16-bit PCM WAV
 * 
 * @param path SPIFFS path (e.g., "/boot.wav", "/click.wav")
 * @return true if played successfully
 */
bool audio_play_asset(const char* path);

/**
 * @brief Play boot chime
 * Convenience function that plays "/boot.wav"
 */
void audio_play_boot_sound();

/**
 * @brief Play UI click/tick sound
 * Convenience function that plays "/click.wav"
 */
void audio_play_click_sound();

#endif // AUDIO_ASSETS_H
