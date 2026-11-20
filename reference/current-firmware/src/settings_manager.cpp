#include "settings_manager.h"
#include "nvs_manager.h"
#include "audio/audio_manager.h"
#include "audio/audio_assets.h"  // UI feedback sounds
#include "sleep_manager.h"
#include "state_machine.h"
#include <Arduino.h>

// ============================================
// PRIVATE STATE
// ============================================
static Arduino_GFX *gfx = nullptr;
static int saved_brightness = 70;
static int saved_volume = 50;
static int saved_mic_gain = 4;
static String saved_device_secret = "0000000000000000000000000000000000000000000000000000000000000000";  // Default
static int saved_timezone = 0;  // Default UTC

// ============================================
// PUBLIC API
// ============================================

void settings_manager_init(Arduino_GFX *gfx_ptr) {
    gfx = gfx_ptr;
    Serial.println("⚙️  SETTINGS MANAGER: Initialized");
}

bool settings_manager_load_from_nvs() {
    if (NVS::loadSettings(&saved_brightness, &saved_volume, &saved_mic_gain)) {
        // Apply loaded settings to hardware
        if (gfx) {
            gfx->Display_Brightness((uint8_t)(saved_brightness * 255 / 100));
        }
        audio_set_volume(saved_volume);
        audio_set_mic_gain(saved_mic_gain);
        
        // Update sleep manager with brightness
        sleep_manager_set_brightness(saved_brightness);
        
        // Load device secret (optional)
        char secret_buf[65];
        if (NVS::loadDeviceSecret(secret_buf)) {
            saved_device_secret = String(secret_buf);
        }
        
        // Load timezone (optional)
        int tz;
        if (NVS::loadTimezone(&tz)) {
            saved_timezone = tz;
        }
        
        Serial.printf("💾 Settings loaded: Brightness %d%%, Volume %d%%, Mic Gain %d\n",
                     saved_brightness, saved_volume, saved_mic_gain);
        return true;
    } else {
        // First boot - use defaults
        Serial.println("💾 No saved settings - using defaults");
        saved_brightness = 70;
        saved_volume = 50;
        saved_mic_gain = 4;
        
        // Save defaults
        NVS::saveSettings(saved_brightness, saved_volume, saved_mic_gain);
        
        // Apply defaults
        if (gfx) {
            gfx->Display_Brightness((uint8_t)(saved_brightness * 255 / 100));
        }
        audio_set_volume(saved_volume);
        audio_set_mic_gain(saved_mic_gain);
        
        // Update sleep manager with brightness
        sleep_manager_set_brightness(saved_brightness);
        
        return false;
    }
}

bool settings_manager_handle_event(int event_data) {
    int event_type = event_data / 100;  // 0=CLOSE, 1=BRIGHTNESS, 2=VOLUME, 3=MIC_GAIN
    int value = event_data % 100;
    
    if (event_type == 0) {
        // CLOSE button clicked
        Serial.println("   ✖ Settings closed");
        state_machine_set(STATE_IDLE);
        return true;
        
    } else if (event_type == 1) {
        // BRIGHTNESS changed
        settings_manager_set_brightness(value, true);
        Serial.printf("   💡 Brightness: %d%%\n", value);
        return true;
        
    } else if (event_type == 2) {
        // VOLUME changed
        settings_manager_set_volume(value, true);
        Serial.printf("   🔊 Volume: %d%%\n", value);
        return true;
        
    } else if (event_type == 3) {
        // MIC_GAIN changed
        settings_manager_set_mic_gain(value, true);
        Serial.printf("   🎙️ Mic Gain: %d\n", value);
        return true;
    }
    
    return false;
}

int settings_manager_get_brightness() {
    return saved_brightness;
}

int settings_manager_get_volume() {
    return saved_volume;
}

int settings_manager_get_mic_gain() {
    return saved_mic_gain;
}

String settings_manager_get_device_secret() {
    return saved_device_secret;
}

void settings_manager_set_device_secret(const char* secret, bool save_to_nvs) {
    if (secret && strlen(secret) == 64) {
        saved_device_secret = String(secret);
        if (save_to_nvs) {
            NVS::saveDeviceSecret(secret);
            Serial.printf("🔐 Device secret updated\n");
        }
    } else {
        Serial.println("❌ Invalid device secret (must be 64 hex chars)");
    }
}

int settings_manager_get_timezone() {
    return saved_timezone;
}

void settings_manager_set_timezone(int timezone_hours, bool save_to_nvs) {
    if (timezone_hours >= -12 && timezone_hours <= 14) {
        saved_timezone = timezone_hours;
        if (save_to_nvs) {
            NVS::saveTimezone(timezone_hours);
            Serial.printf("🌍 Timezone: UTC%+d\n", timezone_hours);
        }
    } else {
        Serial.println("❌ Invalid timezone (must be -12 to +14)");
    }
}

void settings_manager_set_brightness(int brightness, bool save_to_nvs) {
    saved_brightness = brightness;
    
    // Apply to hardware immediately
    if (gfx && !sleep_manager_is_sleeping()) {
        gfx->Display_Brightness(brightness * 255 / 100);
    }
    
    // Update sleep manager
    sleep_manager_set_brightness(brightness);
    
    // Save to NVS
    if (save_to_nvs) {
        NVS::saveSettings(brightness, -1, -1);
    }
}

void settings_manager_set_volume(int volume, bool save_to_nvs) {
    saved_volume = volume;
    
    // Apply to hardware immediately
    audio_set_volume(volume);
    
    // Play UI feedback sound (non-blocking)
    audio_play_click_sound();
    
    // Save to NVS
    if (save_to_nvs) {
        NVS::saveSettings(-1, volume, -1);
    }
}

void settings_manager_set_mic_gain(int mic_gain, bool save_to_nvs) {
    saved_mic_gain = mic_gain;
    
    // Apply to hardware immediately
    audio_set_mic_gain(mic_gain);
    
    // Save to NVS
    if (save_to_nvs) {
        NVS::saveSettings(-1, -1, mic_gain);
    }
}
