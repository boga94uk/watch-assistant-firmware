#include "button_manager.h"
#include <Arduino.h>

// Pin definitions
#define BUTTON_TOP_PIN 0
#define BUTTON_BOTTOM_EXPANDER_PIN 4

// Timing constants (milliseconds)
#define DEBOUNCE_MS 50
#define HOLD_RECORD_MS 500       // 0.5s for record hold
#define HOLD_POWER_MS 5000       // 5s for power off
#define CLICK_TIMEOUT_MS 1000    // Max time between clicks for triple-click (allow slower clicks)

// Button state structure
typedef struct {
    bool current_state;              // Current raw reading
    bool last_stable_state;          // Last debounced state
    unsigned long state_change_time; // When state last changed
    unsigned long press_start_time;  // When button was pressed
    unsigned long release_time;      // When button was released
    int click_count;                 // Number of clicks in sequence
    unsigned long first_click_time;  // Time of first click in sequence
    bool hold_record_fired;          // Has 0.5s hold event fired?
    bool hold_power_fired;           // Has 5s hold event fired?
    bool is_holding;                 // Currently in hold state (not clicking)
} button_state_t;

// Global state
static button_state_t top_button = {HIGH, HIGH, 0, 0, 0, 0, 0, false, false, false};
static button_state_t bottom_button = {HIGH, HIGH, 0, 0, 0, 0, 0, false, false, false};
static button_callback_t event_callback = nullptr;
static ESP_IOExpander *expander = nullptr;

bool button_manager_init(button_callback_t callback, ESP_IOExpander *io_exp) {
    event_callback = callback;
    expander = io_exp;
    
    // Initialize top button (GPIO0)
    pinMode(BUTTON_TOP_PIN, INPUT_PULLUP);
    Serial.println("✅ Button Manager: Top button (GPIO0) initialized");
    
    // Bottom button is configured by the IO expander during display init
    if (expander) {
        Serial.println("✅ Button Manager: Bottom button (Expander Pin 4) ready");
    } else {
        Serial.println("⚠️  Button Manager: No IO Expander provided, bottom button disabled");
    }
    
    return true;
}

static void update_button(button_id_t id, button_state_t *btn, bool raw_state) {
    unsigned long now = millis();
    
    // Update current state
    if (raw_state != btn->current_state) {
        btn->current_state = raw_state;
        btn->state_change_time = now;
        return;  // Wait for debounce period
    }
    
    // Check if debounce period has elapsed
    if (now - btn->state_change_time < DEBOUNCE_MS) {
        return;  // Still in debounce period
    }
    
    // Stable state detected - check if different from last known state
    if (btn->current_state != btn->last_stable_state) {
        btn->last_stable_state = btn->current_state;
        
        if (btn->current_state == LOW) {
            // ===== BUTTON PRESSED =====
            btn->press_start_time = now;
            btn->hold_record_fired = false;
            btn->hold_power_fired = false;
            btn->is_holding = false;
        } else {
            // ===== BUTTON RELEASED =====
            btn->release_time = now;
            unsigned long press_duration = now - btn->press_start_time;
            
            if (btn->is_holding) {
                // Released after hold - fire RELEASE event and don't count as click
                if (event_callback) {
                    event_callback(id, BUTTON_EVENT_RELEASE);
                }
                btn->is_holding = false;
                btn->click_count = 0;  // Reset click counter
            } else {
                // Quick press/release - count as click
                btn->click_count++;
                
                if (btn->click_count == 1) {
                    btn->first_click_time = now;
                } else if (btn->click_count == 3) {
                    // Triple click detected!
                    if (event_callback && id == BUTTON_TOP) {
                        event_callback(id, BUTTON_EVENT_TRIPLE_CLICK);
                    }
                    btn->click_count = 0;  // Reset
                }
            }
        }
    }
    
    // Check for hold events (only while pressed)
    if (btn->last_stable_state == LOW) {
        unsigned long hold_time = now - btn->press_start_time;
        
        // Check 5s hold first (for power off on bottom button)
        if (!btn->hold_power_fired && hold_time >= HOLD_POWER_MS) {
            btn->hold_power_fired = true;
            btn->is_holding = true;
            if (event_callback && id == BUTTON_BOTTOM) {
                event_callback(id, BUTTON_EVENT_HOLD_POWER);
            }
        }
        // Check 0.5s hold (for recording on top button)
        else if (!btn->hold_record_fired && hold_time >= HOLD_RECORD_MS) {
            btn->hold_record_fired = true;
            btn->is_holding = true;
            if (event_callback && id == BUTTON_TOP) {
                event_callback(id, BUTTON_EVENT_HOLD_RECORD);
            }
        }
    }
    
    // Check for single click timeout
    if (btn->click_count > 0 && btn->click_count < 3) {
        if (now - btn->first_click_time > CLICK_TIMEOUT_MS) {
            // Timeout expired, fire single click
            if (event_callback) {
                event_callback(id, BUTTON_EVENT_CLICK);
            }
            btn->click_count = 0;  // Reset
        }
    }
}

bool button_manager_update() {
    // Update top button (direct GPIO)
    bool top_raw = digitalRead(BUTTON_TOP_PIN);
    update_button(BUTTON_TOP, &top_button, top_raw);
    
    // Update bottom button (via IO expander)
    // NOTE: This button has REVERSED logic! HIGH=pressed, LOW=released
    // We invert it here so the rest of the code can treat it normally (LOW=pressed)
    if (expander) {
        bool bottom_raw = expander->digitalRead(BUTTON_BOTTOM_EXPANDER_PIN);
        bool bottom_inverted = !bottom_raw;  // Invert: HIGH->LOW (pressed), LOW->HIGH (released)
        update_button(BUTTON_BOTTOM, &bottom_button, bottom_inverted);
    }
    
    return true;
}

bool button_is_pressed(button_id_t button) {
    if (button == BUTTON_TOP) {
        return top_button.last_stable_state == LOW;
    } else {
        return bottom_button.last_stable_state == LOW;
    }
}
