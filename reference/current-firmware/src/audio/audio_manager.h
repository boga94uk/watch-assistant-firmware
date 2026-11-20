// ============================================
// AUDIO MANAGER - ES8311 + Legacy I2S Driver
// ============================================
// Modular audio system using proven working pattern:
// - Arduino Wire.begin() for I2C
// - Legacy ESP-IDF I2S driver (i2s_driver_install)
// - ES8311 codec driver
// - Recording to SD card with streaming
// - Playback from SD card

#ifndef AUDIO_MANAGER_H
#define AUDIO_MANAGER_H

#include <Arduino.h>

// Audio configuration
#define AUDIO_SAMPLE_RATE 16000
#define AUDIO_BITS_PER_SAMPLE 16
#define AUDIO_CHANNELS 1  // Mono
#define AUDIO_VOICE_VOLUME 80  // 0-100
#define AUDIO_MIC_GAIN 4  // 0-7 (4 = good balance, avoid clipping)

// Recording configuration - STREAMING TO SD
#define AUDIO_RECORD_BUFFER_SIZE (2 * 1024 * 1024)  // 2MB circular buffer (we have 8MB PSRAM!)
#define AUDIO_MAX_RECORD_TIME_MS 60000  // 60 seconds max
#define AUDIO_STREAM_CHUNK_SIZE (64 * 1024)  // Write to SD every 64KB (larger chunks for efficiency)

// Audio state
extern bool audioInitialized;
extern bool audioRecording;
extern bool audioPlaying;

// ============================================
// INITIALIZATION
// ============================================
/**
 * Initialize audio system (ES8311 + I2S)
 * NOTE: Wire.begin() must be called BEFORE this!
 * NOTE: IO Expander must power codec BEFORE this!
 * @return true if successful
 */
bool audio_init();

/**
 * Stop audio system and free resources
 */
void audio_stop();

// ============================================
// RECORDING
// ============================================
/**
 * Start recording to SD card (streaming)
 * @param filename Optional custom filename (without path). If empty, uses RTC timestamp.
 * @return true if successful
 */
bool audio_start_recording(const char* filename = nullptr);

/**
 * Update recording (call in loop while recording)
 * Handles streaming to SD card
 */
void audio_update_recording();

/**
 * Keep codec warm (call in loop when NOT recording)
 * Flushes I2S data to ensure instant capture
 */
void audio_keep_codec_warm();

/**
 * Stop recording and finalize WAV file
 * @return true if successful
 */
bool audio_stop_recording();

/**
 * Check if currently recording
 */
bool audio_is_recording();

/**
 * Get current recording size in bytes
 */
size_t audio_get_recording_size();

/**
 * Get current recording filename
 */
String audio_get_recording_filename();

/**
 * Get recording elapsed time in seconds
 * @return Elapsed seconds (0-60)
 */
int audio_get_recording_seconds();

/**
 * Get complete WAV file from recording buffer (direct upload, no SD reload)
 * Returns pointer to complete WAV: [44B header] + [PCM data]
 * @param size Output parameter for total WAV size (header + PCM)
 * @return Pointer to complete WAV file (PSRAM), or NULL if not available
 * NOTE: Buffer remains valid until audio_free_recording_buffer() is called
 * NOTE: Zero-copy design - header written in-place at stop
 */
uint8_t* audio_get_recording_wav(size_t* size);

/**
 * Free recording buffer (call after successful upload)
 */
void audio_free_recording_buffer();

// ============================================
// PLAYBACK
// ============================================
/**
 * Play audio file from SD card
 * @param filename Path to WAV file
 * @return true if successful
 */
bool audio_play_file(const char* filename);

/**
 * Play audio from PSRAM buffer (e.g., downloaded TTS)
 * @param buffer Pointer to WAV data in PSRAM (must include 44-byte header)
 * @param size Total size including header
 * @return true if successful
 */
bool audio_play_buffer(const uint8_t* buffer, size_t size);

/**
 * Update playback (call in loop while playing)
 * Streams audio data from file/buffer to I2S
 */
void audio_update_playback();

/**
 * Stop playback
 * @return true if successful
 */
bool audio_stop_playback();

/**
 * Check if currently playing
 */
bool audio_is_playing();

/**
 * Get current playback position in seconds
 * Based on 16-bit mono @ 16000 Hz
 */
int audio_get_playback_position_sec();

/**
 * Get total playback duration in seconds
 * Based on 16-bit mono @ 16000 Hz
 */
int audio_get_playback_duration_sec();

// ============================================
// CONTROL
// ============================================
/**
 * Set speaker volume
 * @param volume 0-100
 */
void audio_set_volume(uint8_t volume);

/**
 * Set microphone gain
 * @param gain 0-7
 */
void audio_set_mic_gain(uint8_t gain);

/**
 * Get current volume (0-100)
 */
int audio_get_volume();

/**
 * Get audio status string
 */
String audio_get_status();

/**
 * Get current recording audio level (0.0 to 1.0)
 * Used for visual feedback during recording
 */
float audio_get_recording_level();

#endif // AUDIO_MANAGER_H
