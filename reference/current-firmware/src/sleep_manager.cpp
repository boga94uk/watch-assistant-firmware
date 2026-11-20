#include "sleep_manager.h"
#include "imu_manager.h"
#include "rtc_manager.h"
#include "display/display_manager.h"
#include "upload_queue.h"
#include <Arduino.h>

// ============================================
// PRIVATE STATE
// ============================================
static Arduino_GFX *gfx = nullptr;
static bool is_sleeping = false;
static int saved_brightness = 70;

// Sleep timing
static const unsigned long INACTIVITY_TIMEOUT = 30000;  // 30 seconds
static unsigned long last_inactivity_check = 0;

// Shake detection state
static unsigned long last_shake_check = 0;
static float last_acc_magnitude = 1.0f;  // ~1g at rest
static int shake_detect_count = 0;
static unsigned long last_shake_time = 0;

// Shake detection tuning
static const float SHAKE_THRESHOLD = 0.3f;        // 0.3g acceleration change
static const int SHAKE_REQUIRED_COUNT = 2;        // Need 2 movements
static const unsigned long SHAKE_WINDOW_MS = 500; // Within 500ms
static const unsigned long SHAKE_CHECK_INTERVAL = 100; // Check every 100ms

// Clock update timing
static unsigned long last_clock_update = 0;

// ============================================
// PUBLIC API
// ============================================

void sleep_manager_init(Arduino_GFX *gfx_ptr, int initial_brightness) {
    gfx = gfx_ptr;
    saved_brightness = initial_brightness;
    is_sleeping = false;
    
    Serial.println("💤 SLEEP MANAGER: Initialized");
}

void sleep_manager_update() {
    // Check for inactivity timeout (every second)
    if (millis() - last_inactivity_check >= 1000) {
        last_inactivity_check = millis();
        
        // Auto-sleep after timeout (from IDLE or QUEUED state)
        app_state_t current_state = state_machine_get();
        if (!is_sleeping && 
            state_machine_is_inactive(INACTIVITY_TIMEOUT) && 
            (current_state == STATE_IDLE || current_state == STATE_QUEUED)) {
            
            Serial.println("💤 30s inactivity timeout - entering SLEEP");
            sleep_manager_enter_sleep();
        }
        
        // Update sleep screen clock (every second)
        if (is_sleeping && display_manager_get_state() == DISPLAY_STATE_SLEEP) {
            // Get RTC time and format
            String datetime = getCurrentDateTime();
            String time_str, date_str;
            
            if (datetime.length() >= 19) {  // "YYYY-MM-DD HH:MM:SS"
                date_str = datetime.substring(0, 10);   // "YYYY-MM-DD"
                time_str = datetime.substring(11, 19);  // "HH:MM:SS" (with seconds!)
            } else {
                time_str = "--:--:--";
                date_str = "----";
            }
            
            // Update clock display
            display_manager_update_sleep_time(time_str.c_str(), date_str.c_str());
        }
    }
    
    // Shake detection (100ms polling for responsiveness)
    if (is_sleeping && millis() - last_shake_check >= SHAKE_CHECK_INTERVAL) {
        last_shake_check = millis();
        
        IMUdata acc;
        if (IMU::getAccelerometer(acc)) {
            // Calculate total acceleration magnitude
            float magnitude = sqrt(acc.x*acc.x + acc.y*acc.y + acc.z*acc.z);
            
            // Calculate change in acceleration (jerk detection)
            float delta = abs(magnitude - last_acc_magnitude);
            
            // Detect sudden movement (shake)
            if (delta > SHAKE_THRESHOLD) {
                shake_detect_count++;
                
                // Require multiple detections within time window (reduces false positives)
                if (shake_detect_count >= SHAKE_REQUIRED_COUNT && 
                    (millis() - last_shake_time) < SHAKE_WINDOW_MS) {
                    
                    Serial.println("👋 Shake detected - waking from sleep");
                    
                    // Wake up - check queue status to decide state
                    is_sleeping = false;
                    if (gfx) {
                        gfx->Display_Brightness(saved_brightness * 255 / 100);
                    }
                    
                    // Wake to QUEUED if files exist, otherwise IDLE
                    if (queue_size() > 0) {
                        state_machine_set(STATE_QUEUED);
                    } else {
                        state_machine_set(STATE_IDLE);
                    }
                    state_machine_update_activity();
                    
                    // Reset detection
                    shake_detect_count = 0;
                    last_shake_time = 0;
                } else {
                    last_shake_time = millis();
                }
            } else {
                // Reset if no movement for time window
                if (millis() - last_shake_time > SHAKE_WINDOW_MS) {
                    shake_detect_count = 0;
                }
            }
            
            last_acc_magnitude = magnitude;
        }
    }
}

bool sleep_manager_is_sleeping() {
    return is_sleeping;
}

bool sleep_manager_handle_button(button_id_t button, button_event_t event) {
    if (!is_sleeping) {
        return false;  // Not in sleep mode
    }
    
    // Wake on any button event
    if (event == BUTTON_EVENT_CLICK || 
        event == BUTTON_EVENT_HOLD_POWER || 
        event == BUTTON_EVENT_HOLD_RECORD) {
        Serial.println("   → Waking from sleep (button press)");
        
        is_sleeping = false;
        if (gfx) {
            gfx->Display_Brightness(saved_brightness * 255 / 100);
        }
        
        // Wake to QUEUED if files exist, otherwise IDLE
        if (queue_size() > 0) {
            state_machine_set(STATE_QUEUED);
        } else {
            state_machine_set(STATE_IDLE);
        }
        
        // For HOLD_RECORD, don't consume the event - let it pass through to start recording
        if (event == BUTTON_EVENT_HOLD_RECORD && button == BUTTON_TOP) {
            return false;  // Don't consume - allow recording to start
        }
        
        return true;  // Consume other events
    }
    
    return false;
}

void sleep_manager_enter_sleep() {
    if (is_sleeping) {
        return;  // Already sleeping
    }
    
    // Only allow sleep from IDLE or QUEUED states (prevent sleeping during recording, settings, etc.)
    app_state_t current_state = state_machine_get();
    if (current_state != STATE_IDLE && current_state != STATE_QUEUED) {
        Serial.printf("⚠️  Cannot enter sleep from state: %s (only from IDLE or QUEUED)\n", 
                     state_machine_get_name(current_state));
        return;
    }
    
    is_sleeping = true;
    if (gfx) {
        gfx->Display_Brightness(38);  // 15% brightness (38/255)
    }
    state_machine_set(STATE_SLEEP);
    display_manager_set_state(DISPLAY_STATE_SLEEP);
    
    Serial.println("💤 Entered sleep mode");
}

void sleep_manager_set_brightness(int brightness) {
    saved_brightness = brightness;
    
    // If not sleeping, apply immediately
    if (!is_sleeping && gfx) {
        gfx->Display_Brightness(saved_brightness * 255 / 100);
    }
}

int sleep_manager_get_brightness() {
    return saved_brightness;
}
