// ============================================
// AUDIO MANAGER - Implementation
// ============================================

#include "audio_manager.h"
#include "audio_pins.h"
#include "es8311_driver.h"
#include "../sd_manager.h"
#include "../rtc_manager.h"
#include "driver/i2s.h"
#include "driver/gpio.h"
#include <Wire.h>
#include <esp_heap_caps.h>
#include <string.h>

// I2S port
#define I2S_PORT I2S_NUM_0

// Audio state
bool audioInitialized = false;
bool audioRecording = false;
bool audioPlaying = false;

// Recording buffer: [44B WAV header][PCM data...]
// Header written in-place at stop for zero-copy upload
#define WAV_HEADER_SIZE 44
static uint8_t* recordBuffer = NULL;        // [header+PCM] in PSRAM
static size_t recordCapacity = 0;           // Total allocated bytes
static size_t pcmBytes = 0;                 // PCM bytes written (no header)
static size_t writePos = WAV_HEADER_SIZE;   // Next write position (skip header)
static bool wavFinalized = false;           // Header written?
static unsigned long recordStartTime = 0;
static size_t recordBufferPos = 0;          // For SD streaming chunks

// SD streaming (POSIX FILE* for ESP-IDF native SD)
static FILE* recordFile = NULL;
static String recordFilename;
static bool streamingToSD = false;

// Audio level tracking (for visual feedback)
static float currentAudioLevel = 0.0f;  // 0.0 to 1.0

// Playback state
static FILE* playbackFile = NULL;
static String playbackFilename;
static size_t playbackDataSize = 0;
static size_t playbackBytesRead = 0;
static uint8_t* playbackBuffer = NULL;
static const size_t PLAYBACK_BUFFER_SIZE = 2048;  // 2KB buffer (we read 1KB at a time)

// Buffer playback state (for PSRAM TTS playback)
static bool playingFromBuffer = false;
static const uint8_t* psramPlaybackBuffer = NULL;
static size_t psramPlaybackSize = 0;
static size_t psramPlaybackPos = 0;

// Current volume
static uint8_t currentVolume = AUDIO_VOICE_VOLUME;

// ============================================
// PRIVATE HELPERS
// ============================================

// Create WAV header in memory
static void makeWavHeader(uint8_t* h, uint32_t dataSize, uint32_t rate, uint16_t ch, uint16_t bps) {
    uint32_t byteRate = rate * ch * (bps / 8);
    uint16_t blockAlign = ch * (bps / 8);
    
    // RIFF chunk
    memcpy(h + 0, "RIFF", 4);
    uint32_t chunkSize = 36 + dataSize;
    memcpy(h + 4, &chunkSize, 4);
    memcpy(h + 8, "WAVE", 4);
    
    // fmt chunk
    memcpy(h + 12, "fmt ", 4);
    uint32_t fmtSize = 16;
    memcpy(h + 16, &fmtSize, 4);
    uint16_t audioFormat = 1;  // PCM
    memcpy(h + 20, &audioFormat, 2);
    memcpy(h + 22, &ch, 2);
    memcpy(h + 24, &rate, 4);
    memcpy(h + 28, &byteRate, 4);
    memcpy(h + 32, &blockAlign, 2);
    memcpy(h + 34, &bps, 2);
    
    // data chunk
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &dataSize, 4);
}

// ============================================
// INITIALIZATION
// ============================================

bool audio_init() {
    // Enable PA (Power Amplifier) - CRITICAL!
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << PA);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
    gpio_set_level((gpio_num_t)PA, 1);
    delay(100);
    
    // Initialize I2S
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,    // 8 buffers (proven working config)
        .dma_buf_len = 1024,   // 1KB each (max allowed by I2S driver)
        .use_apll = false,     // Total: 8 × 1KB = 8KB DMA buffers
        .tx_desc_auto_clear = true,
        .fixed_mclk = AUDIO_SAMPLE_RATE * 384,  // Match working example
    };
    
    if (i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL) != ESP_OK) {
        Serial.println("❌ I2S driver install failed!");
        return false;
    }
    
    // Set I2S pins (see audio_pins.h for pin definitions)
    i2s_pin_config_t pin_config = {
        .mck_io_num = MCLKPIN,
        .bck_io_num = BCLKPIN,
        .ws_io_num = WSPIN,
        .data_out_num = DOPIN,
        .data_in_num = DIPIN
    };
    
    if (i2s_set_pin(I2S_PORT, &pin_config) != ESP_OK) {
        Serial.println("❌ I2S set pin failed!");
        return false;
    }
    
    // Give codec time to stabilize (powered by IO Expander)
    delay(300);
    
    // Initialize ES8311 codec
    if (es8311_codec_init() != ESP_OK) {
        Serial.println("❌ ES8311 initialization failed!");
        return false;
    }
    
    audioInitialized = true;
    return true;
}

void audio_stop() {
    if (audioRecording) {
        audio_stop_recording();
    }
    if (audioPlaying) {
        audio_stop_playback();
    }
    
    if (recordBuffer) {
        free(recordBuffer);
        recordBuffer = NULL;
    }
    
    // Disable PA
    gpio_set_level((gpio_num_t)PA, 0);
    
    audioInitialized = false;
    Serial.println("🎙️ Audio stopped");
}

// ============================================
// RECORDING
// ============================================

bool audio_start_recording(const char* filename) {
    if (!audioInitialized) {
        Serial.println("❌ Audio not initialized");
        return false;
    }
    
    if (audioRecording) {
        Serial.println("⚠️  Already recording");
        return false;
    }
    
    // Allocate buffer: [44B header] + [PCM data]
    recordCapacity = WAV_HEADER_SIZE + AUDIO_RECORD_BUFFER_SIZE;
    Serial.printf("💾 Allocating %d KB recording buffer from PSRAM\n", recordCapacity / 1024);
    
    recordBuffer = (uint8_t*)heap_caps_malloc(recordCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!recordBuffer) {
        Serial.println("❌ Failed to allocate recording buffer");
        return false;
    }
    
    // Zero out header region (will be written on stop)
    memset(recordBuffer, 0, WAV_HEADER_SIZE);
    
    // Generate filename and try to open SD file
    createDirectory("/sdcard/recordings");
    
    if (filename != nullptr && strlen(filename) > 0) {
        // Use provided filename
        recordFilename = "/sdcard/recordings/" + String(filename);
    } else {
        // Use RTC timestamp (or millis fallback)
        String timestamp = getTimestampForFilename();
        recordFilename = "/sdcard/recordings/rec_" + timestamp + ".wav";
    }
    
    // Try to open recording file on SD card
    recordFile = openRecordingFile(recordFilename.c_str());
    
    // Check if SD card is available for streaming
    if (recordFile == NULL) {
        Serial.println("⚠️  SD card not available - recording to PSRAM only");
        streamingToSD = false;
        recordFilename = ""; // No file saved
    } else {
        Serial.println("✅ Recording started (streaming to SD)");
        Serial.printf("   File: %s\n", recordFilename.c_str());
        streamingToSD = true;
    }
    
    // Initialize recording state (works with or without SD)
    wavFinalized = false;
    pcmBytes = 0;
    writePos = WAV_HEADER_SIZE;
    recordBufferPos = 0;
    recordStartTime = millis();
    audioRecording = true;
    currentAudioLevel = 0.0f;  // Reset audio level
    
    return true;
}

void audio_update_recording() {
    if (!audioRecording) {
        return;
    }
    
    // Check time limit
    if (millis() - recordStartTime >= AUDIO_MAX_RECORD_TIME_MS) {
        Serial.println("⏱️  Max recording time reached");
        audio_stop_recording();
        return;
    }
    
    // Read available I2S data
    size_t bytesRead = 0;
    int16_t stereoBuffer[512];
    
    esp_err_t result = i2s_read(I2S_PORT, stereoBuffer, sizeof(stereoBuffer), &bytesRead, pdMS_TO_TICKS(10));
    
    if (result == ESP_OK && bytesRead > 0) {
        // Convert stereo to mono (average L+R)
        size_t samples = bytesRead / 4;  // 4 bytes per stereo sample
        size_t monoBytes = samples * 2;
        
        // Check capacity
        if (writePos + monoBytes > recordCapacity) {
            Serial.println("⚠️  Recording buffer full");
            audio_stop_recording();
            return;
        }
        
        // Write mono samples to buffer at writePos (after header)
        int16_t* monoData = (int16_t*)&recordBuffer[writePos];
        
        // TODO: DISABLED FOR TESTING - Audio level calculation may be slowing down I2S reads
        // int32_t sumSquares = 0;
        for (size_t i = 0; i < samples; i++) {
            int32_t avg = ((int32_t)stereoBuffer[i * 2] + (int32_t)stereoBuffer[i * 2 + 1]) / 2;
            monoData[i] = (int16_t)avg;
            
            // TODO: DISABLED FOR TESTING
            // // Calculate RMS for audio level (use absolute value for simplicity)
            // int32_t absVal = (avg < 0) ? -avg : avg;
            // sumSquares += absVal;
        }
        
        // TODO: DISABLED FOR TESTING
        // // Calculate average amplitude (0 to 32767)
        // if (samples > 0) {
        //     float avgAmplitude = (float)sumSquares / (float)samples;
        //     // Normalize to 0.0-1.0 range (assuming max amplitude is 8192 for typical speech)
        //     // Lower threshold = more sensitive visualizer
        //     currentAudioLevel = avgAmplitude / 8192.0f;
        //     if (currentAudioLevel > 1.0f) currentAudioLevel = 1.0f;
        // }
        
        writePos += monoBytes;
        pcmBytes += monoBytes;
        recordBufferPos += monoBytes;
        
        // Stream to SD when buffer chunk is full (only if SD available)
        if (streamingToSD && recordBufferPos >= AUDIO_STREAM_CHUNK_SIZE) {
            writeRecordingData(recordFile, recordBuffer + (writePos - recordBufferPos), recordBufferPos);
            recordBufferPos = 0;
        }
    }
}

void audio_keep_codec_warm() {
    if (!audioInitialized || audioRecording || audioPlaying) {
        return;
    }
    
    // Flush I2S data to keep codec ready
    size_t bytesRead = 0;
    int16_t flushBuffer[256];
    i2s_read(I2S_PORT, flushBuffer, sizeof(flushBuffer), &bytesRead, 0);
}

bool audio_stop_recording() {
    if (!audioRecording) {
        return false;
    }
    
    // Only handle SD operations if we were streaming to SD
    if (streamingToSD && recordFile != NULL) {
        // Flush remaining buffer to SD
        if (recordBufferPos > 0) {
            writeRecordingData(recordFile, recordBuffer + (writePos - recordBufferPos), recordBufferPos);
            recordBufferPos = 0;
        }
        
        // Finalize recording file (SD manager updates WAV header and closes file)
        finalizeRecordingFile(recordFile, pcmBytes, AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, AUDIO_BITS_PER_SAMPLE);
        recordFile = NULL;
        
        Serial.println("✅ Recording stopped");
        Serial.printf("   Saved: %s (%d bytes)\n", recordFilename.c_str(), pcmBytes);
    } else {
        // PSRAM-only mode (no SD card)
        Serial.println("✅ Recording stopped (PSRAM only - SD not available)");
        Serial.printf("   Captured: %d bytes PCM\n", pcmBytes);
    }
    
    // Write WAV header in PSRAM buffer (in-place, zero-copy for upload!)
    makeWavHeader(recordBuffer, (uint32_t)pcmBytes, AUDIO_SAMPLE_RATE, AUDIO_CHANNELS, AUDIO_BITS_PER_SAMPLE);
    wavFinalized = true;
    
    audioRecording = false;
    streamingToSD = false;
    
    Serial.printf("   📦 WAV file ready in PSRAM: %d bytes (44 header + %d PCM)\n", 
                  WAV_HEADER_SIZE + pcmBytes, pcmBytes);
    
    return true;
}

bool audio_is_recording() {
    return audioRecording;
}

size_t audio_get_recording_size() {
    return pcmBytes;
}

String audio_get_recording_filename() {
    return recordFilename;
}

int audio_get_recording_seconds() {
    if (!audioRecording || recordStartTime == 0) {
        return 0;
    }
    return (millis() - recordStartTime) / 1000;
}

// NEW: Get complete WAV file (header + PCM) for direct upload
uint8_t* audio_get_recording_wav(size_t* size) {
    if (!recordBuffer || !wavFinalized) {
        if (size) *size = 0;
        return nullptr;
    }
    if (size) {
        *size = WAV_HEADER_SIZE + pcmBytes;
    }
    return recordBuffer;  // Points to [44B header][PCM data]
}

void audio_free_recording_buffer() {
    if (recordBuffer) {
        heap_caps_free(recordBuffer);
        recordBuffer = NULL;
        recordCapacity = 0;
        pcmBytes = 0;
        writePos = WAV_HEADER_SIZE;
        wavFinalized = false;
        recordBufferPos = 0;
    }
}

// ============================================
// PLAYBACK
// ============================================

bool audio_play_file(const char* filename) {
    if (!audioInitialized) {
        Serial.println("❌ Audio not initialized");
        return false;
    }
    
    if (audioPlaying) {
        Serial.println("⚠️  Already playing");
        return false;
    }
    
    if (audioRecording) {
        Serial.println("❌ Cannot play while recording");
        return false;
    }
    
    // Open WAV file
    playbackFile = fopen(filename, "rb");
    if (playbackFile == NULL) {
        Serial.printf("❌ Cannot open: %s\n", filename);
        return false;
    }
    
    // Parse WAV header (simplified - assumes 16-bit mono WAV)
    uint8_t header[44];
    if (fread(header, 1, 44, playbackFile) != 44) {
        Serial.println("❌ Invalid WAV file (header too short)");
        fclose(playbackFile);
        playbackFile = NULL;
        return false;
    }
    
    // Verify RIFF/WAVE format
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        Serial.println("❌ Not a valid WAV file");
        fclose(playbackFile);
        playbackFile = NULL;
        return false;
    }
    
    // Get data size from header (bytes 40-43)
    playbackDataSize = header[40] | (header[41] << 8) | (header[42] << 16) | (header[43] << 24);
    
    // Allocate playback buffer
    playbackBuffer = (uint8_t*)malloc(PLAYBACK_BUFFER_SIZE);
    if (!playbackBuffer) {
        Serial.println("❌ Failed to allocate playback buffer");
        fclose(playbackFile);
        playbackFile = NULL;
        return false;
    }
    
    playbackFilename = String(filename);
    playbackBytesRead = 0;
    audioPlaying = true;
    
    // Clear I2S DMA buffer to ensure clean start
    i2s_zero_dma_buffer(I2S_PORT);
    
    // Reset debug flags (these are static in audio_update_playback)
    extern bool g_first_read_checked;
    extern int g_write_count;
    g_first_read_checked = false;
    g_write_count = 0;
    
    Serial.printf("🎵 Playing: %s (%d bytes)\n", filename, playbackDataSize);
    Serial.printf("   📊 Current volume setting: %d\n", currentVolume);
    
    // Re-apply volume (direct mapping - respects user setting)
    es8311_codec_set_voice_volume(currentVolume);
    Serial.printf("   🔊 Applied codec volume: %d\n", currentVolume);
    
    return true;
}

bool audio_play_buffer(const uint8_t* buffer, size_t size) {
    if (!audioInitialized) {
        Serial.println("❌ Audio not initialized");
        return false;
    }
    
    if (audioRecording) {
        Serial.println("❌ Cannot play while recording");
        return false;
    }
    
    if (audioPlaying) {
        Serial.println("⚠️  Already playing - stopping first");
        audio_stop_playback();
    }
    
    if (!buffer || size < 44) {
        Serial.println("❌ Invalid buffer or size");
        return false;
    }
    
    // Verify WAV header
    if (memcmp(buffer, "RIFF", 4) != 0 || memcmp(buffer + 8, "WAVE", 4) != 0) {
        Serial.println("❌ Not a valid WAV buffer");
        return false;
    }
    
    // Get data size from header (bytes 40-43) - PCM data size
    uint32_t dataSize = buffer[40] | (buffer[41] << 8) | (buffer[42] << 16) | (buffer[43] << 24);
    
    // Set up buffer playback
    playingFromBuffer = true;
    psramPlaybackBuffer = buffer;
    psramPlaybackSize = size;
    psramPlaybackPos = 44;  // Skip WAV header
    audioPlaying = true;
    
    Serial.printf("🎵 Playing buffer: %u bytes (PCM: %u bytes)\n", (unsigned)size, (unsigned)dataSize);
    
    return true;
}

/**
 * @brief Update audio playback (call from loop)
 */
void audio_update_playback() {
    if (!audioPlaying) {
        return;
    }
    
    // Performance monitoring (disabled for production)
    // Update runs at ~30 Hz during playback
    
    // Handle PSRAM buffer playback
    if (playingFromBuffer) {
        if (psramPlaybackPos >= psramPlaybackSize) {
            Serial.println("✅ Buffer playback finished");
            audio_stop_playback();
            return;
        }
        
        // Read mono audio data from PSRAM buffer
        size_t bytesToRead = 1024;  // Read 1KB at a time
        if (psramPlaybackPos + bytesToRead > psramPlaybackSize) {
            bytesToRead = psramPlaybackSize - psramPlaybackPos;
        }
        
        // Convert mono to stereo
        size_t samples = bytesToRead / 2;  // 16-bit samples
        int16_t* monoData = (int16_t*)(psramPlaybackBuffer + psramPlaybackPos);
        
        int16_t* stereoBuffer = (int16_t*)malloc(samples * 4);
        if (!stereoBuffer) {
            Serial.println("❌ Failed to allocate stereo buffer");
            audio_stop_playback();
            return;
        }
        
        for (size_t i = 0; i < samples; i++) {
            stereoBuffer[i * 2] = monoData[i];      // Left
            stereoBuffer[i * 2 + 1] = monoData[i];  // Right
        }
        
        // Write to I2S
        size_t bytesWritten = 0;
        i2s_write(I2S_PORT, stereoBuffer, samples * 4, &bytesWritten, portMAX_DELAY);
        
        free(stereoBuffer);
        psramPlaybackPos += bytesToRead;
        return;
    }
    
    // Handle file playback
    if (playbackFile == NULL) {
        return;
    }
    
    // Check if finished
    if (playbackBytesRead >= playbackDataSize) {
        Serial.println("✅ Playback finished");
        audio_stop_playback();
        return;
    }
    
    // Read mono audio data from file (smaller chunks to avoid stack overflow)
    size_t bytesToRead = 1024;  // Read 1KB at a time
    if (playbackBytesRead + bytesToRead > playbackDataSize) {
        bytesToRead = playbackDataSize - playbackBytesRead;
    }
    
    size_t bytesRead = fread(playbackBuffer, 1, bytesToRead, playbackFile);
    if (bytesRead == 0) {
        Serial.println("⚠️  EOF or read error");
        audio_stop_playback();
        return;
    }
    
    // Debug: Check first read for valid data
    extern bool g_first_read_checked;
    if (!g_first_read_checked && playbackBytesRead == 0) {
        int16_t* firstSamples = (int16_t*)playbackBuffer;
        Serial.printf("   📊 First read: %u bytes, first 4 samples: %d, %d, %d, %d\n",
                      bytesRead, firstSamples[0], firstSamples[1], firstSamples[2], firstSamples[3]);
        g_first_read_checked = true;
    }
    
    playbackBytesRead += bytesRead;
    
    // Convert mono to stereo by duplicating each sample
    size_t samples = bytesRead / 2;  // 16-bit samples
    int16_t* monoData = (int16_t*)playbackBuffer;
    
    // Allocate stereo buffer dynamically (avoid stack overflow)
    int16_t* stereoBuffer = (int16_t*)malloc(samples * 4);
    if (!stereoBuffer) {
        Serial.println("❌ Failed to allocate stereo buffer");
        audio_stop_playback();
        return;
    }
    
    for (size_t i = 0; i < samples; i++) {
        stereoBuffer[i * 2] = monoData[i];      // Left channel
        stereoBuffer[i * 2 + 1] = monoData[i];  // Right channel (duplicate)
    }
    
    // Write to I2S
    size_t bytesWritten = 0;
    i2s_write(I2S_PORT, stereoBuffer, samples * 4, &bytesWritten, portMAX_DELAY);
    
    // Debug: Log first few writes and check data validity
    extern int g_write_count;
    if (g_write_count < 3) {
        // Check if data is non-zero
        int16_t maxSample = 0;
        for (size_t i = 0; i < samples && i < 10; i++) {
            int16_t sample = abs(monoData[i]);
            if (sample > maxSample) maxSample = sample;
        }
        Serial.printf("   🔊 I2S write #%d: %u bytes requested, %u written, max sample: %d\n", 
                      g_write_count + 1, samples * 4, bytesWritten, maxSample);
        g_write_count++;
    }
    
    free(stereoBuffer);
}

bool audio_stop_playback() {
    if (!audioPlaying) {
        return false;
    }
    
    // Close file
    if (playbackFile) {
        fclose(playbackFile);
        playbackFile = NULL;
    }
    
    // Free buffer
    if (playbackBuffer) {
        free(playbackBuffer);
        playbackBuffer = NULL;
    }
    
    // Clear PSRAM buffer state
    playingFromBuffer = false;
    psramPlaybackBuffer = NULL;
    psramPlaybackSize = 0;
    psramPlaybackPos = 0;
    playbackFilename = "";
    playbackDataSize = 0;
    playbackBytesRead = 0;
    
    audioPlaying = false;
    Serial.println("⏹️  Playback stopped");
    
    return true;
}

// Debug flags (global so they can be reset from audio_play_file)
bool g_first_read_checked = false;
int g_write_count = 0;

bool audio_is_playing() {
    return audioPlaying;
}

/**
 * @brief Get current playback position in seconds
 * Calculates from bytes played and sample rate (16-bit mono @ 16000 Hz)
 */
int audio_get_playback_position_sec() {
    if (!audioPlaying) return 0;
    
    // For 16-bit mono @ 16000 Hz: 32000 bytes/sec
    const int BYTES_PER_SECOND = 32000;
    
    if (playingFromBuffer) {
        // PSRAM buffer playback
        size_t bytesPlayed = (psramPlaybackPos > 44) ? (psramPlaybackPos - 44) : 0;
        return bytesPlayed / BYTES_PER_SECOND;
    } else if (playbackFile) {
        // File playback
        return playbackBytesRead / BYTES_PER_SECOND;
    }
    
    return 0;
}

/**
 * @brief Get total playback duration in seconds
 * Calculates from total data size and sample rate (16-bit mono @ 16000 Hz)
 */
int audio_get_playback_duration_sec() {
    if (!audioPlaying) return 0;
    
    // For 16-bit mono @ 16000 Hz: 32000 bytes/sec
    const int BYTES_PER_SECOND = 32000;
    
    if (playingFromBuffer) {
        // PSRAM buffer: size minus 44-byte header
        size_t dataSize = (psramPlaybackSize > 44) ? (psramPlaybackSize - 44) : 0;
        return dataSize / BYTES_PER_SECOND;
    } else if (playbackFile) {
        // File playback
        return playbackDataSize / BYTES_PER_SECOND;
    }
    
    return 0;
}

// ============================================
// CONTROL
// ============================================

void audio_set_volume(uint8_t volume) {
    if (volume > 100) volume = 100;
    currentVolume = volume;
    
    // Apply lighter volume normalization (previous was too aggressive)
    // Map user range 0-100% to codec range 0-100% with slight boost
    // This respects user settings better while still compensating for recordings
    uint8_t normalizedVolume = 50 + (volume / 2);  // Direct mapping - let user control volume
    
    es8311_codec_set_voice_volume(normalizedVolume);
    Serial.printf("🔊 Volume: %d (normalized to codec: %d)\n", volume, normalizedVolume);
}

void audio_set_mic_gain(uint8_t gain) {
    if (gain > 7) gain = 7;
    es8311_codec_set_mic_gain(gain);
    Serial.printf("🎤 Mic gain: %d\n", gain);
}

int audio_get_volume() {
    return currentVolume;
}

String audio_get_status() {
    if (!audioInitialized) return "Not initialized";
    if (audioRecording) return "Recording";
    if (audioPlaying) return "Playing";
    return "Ready";
}

float audio_get_recording_level() {
    return currentAudioLevel;
}
