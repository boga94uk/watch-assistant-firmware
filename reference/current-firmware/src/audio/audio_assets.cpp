#include "audio_assets.h"
#include "audio_manager.h"
#include <SPIFFS.h>
#include <driver/i2s.h>
#include "audio_pins.h"

// ============================================
// AUDIO ASSETS MANAGER
// Plays short WAV files from SPIFFS for UI feedback
// ============================================

static bool assetsInitialized = false;

/**
 * @brief Initialize SPIFFS filesystem
 */
bool audio_assets_init() {
    if (assetsInitialized) {
        return true;
    }
    
    Serial.println("📂 Initializing SPIFFS for audio assets...");
    
    // Mount SPIFFS (formatOnFail = true for first boot)
    if (!SPIFFS.begin(true)) {
        Serial.println("❌ SPIFFS mount failed!");
        return false;
    }
    
    // Show SPIFFS info
    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes = SPIFFS.usedBytes();
    Serial.printf("✅ SPIFFS mounted: %.1f MB total, %.1f MB used, %.1f MB free\n",
                  totalBytes / 1024.0 / 1024.0,
                  usedBytes / 1024.0 / 1024.0,
                  (totalBytes - usedBytes) / 1024.0 / 1024.0);
    
    assetsInitialized = true;
    return true;
}

/**
 * @brief Play a WAV file from SPIFFS
 * Blocking function - streams entire file to I2S
 */
bool audio_play_asset(const char* path) {
    if (!assetsInitialized) {
        Serial.println("❌ SPIFFS not initialized");
        return false;
    }
    
    // Don't interrupt recording or playback
    if (audio_is_recording() || audio_is_playing()) {
        Serial.println("⚠️  Audio busy, skipping asset playback");
        return false;
    }
    
    // Open file
    File f = SPIFFS.open(path, "r");
    if (!f) {
        Serial.printf("❌ Cannot open SPIFFS file: %s\n", path);
        return false;
    }
    
    size_t fileSize = f.size();
    if (fileSize < 44) {
        Serial.printf("❌ File too small (not a WAV): %s\n", path);
        f.close();
        return false;
    }
    
    // Read WAV header (44 bytes)
    uint8_t header[44];
    if (f.read(header, 44) != 44) {
        Serial.println("❌ Failed to read WAV header");
        f.close();
        return false;
    }
    
    // Verify RIFF/WAVE format
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        Serial.printf("❌ Not a valid WAV file: %s\n", path);
        f.close();
        return false;
    }
    
    // Extract PCM data size from header (bytes 40-43)
    uint32_t pcmSize = header[40] | (header[41] << 8) | (header[42] << 16) | (header[43] << 24);
    
    Serial.printf("🔊 Playing asset: %s (%u bytes PCM, %.1f sec)\n", 
                  path, pcmSize, pcmSize / 32000.0);
    
    // Stream PCM data to I2S in chunks
    const size_t CHUNK_SIZE = 512;
    uint8_t* chunkBuffer = (uint8_t*)malloc(CHUNK_SIZE);
    
    if (!chunkBuffer) {
        Serial.println("❌ Failed to allocate chunk buffer");
        f.close();
        return false;
    }
    
    size_t totalRead = 0;
    while (totalRead < pcmSize) {
        // Read next chunk (mono PCM)
        size_t toRead = (pcmSize - totalRead > CHUNK_SIZE) ? CHUNK_SIZE : (pcmSize - totalRead);
        size_t bytesRead = f.read(chunkBuffer, toRead);
        
        if (bytesRead == 0) {
            break;  // EOF or error
        }
        
        // Convert mono to stereo
        size_t samples = bytesRead / 2;  // 16-bit samples
        int16_t* monoData = (int16_t*)chunkBuffer;
        int16_t* stereoBuffer = (int16_t*)malloc(samples * 4);
        
        if (!stereoBuffer) {
            Serial.println("❌ Failed to allocate stereo buffer");
            free(chunkBuffer);
            f.close();
            return false;
        }
        
        // Duplicate mono samples to stereo
        for (size_t i = 0; i < samples; i++) {
            stereoBuffer[i * 2] = monoData[i];      // Left
            stereoBuffer[i * 2 + 1] = monoData[i];  // Right
        }
        
        // Write to I2S (blocking)
        size_t bytesWritten = 0;
        i2s_write(I2S_PORT, stereoBuffer, samples * 4, &bytesWritten, portMAX_DELAY);
        
        free(stereoBuffer);
        totalRead += bytesRead;
    }
    
    free(chunkBuffer);
    f.close();
    
    Serial.printf("   ✅ Asset playback complete (%u bytes)\n", totalRead);
    return true;
}

/**
 * @brief Play boot chime
 */
void audio_play_boot_sound() {
    if (SPIFFS.exists("/boot.wav")) {
        audio_play_asset("/boot.wav");
    } else {
        Serial.println("ℹ️  Boot sound not found (/boot.wav)");
    }
}

/**
 * @brief Play UI click sound
 */
void audio_play_click_sound() {
    if (SPIFFS.exists("/click_sound.wav")) {
        audio_play_asset("/click_sound.wav");
    } else {
        Serial.println("ℹ️  Click sound not found (/click_sound.wav)");
    }
}
