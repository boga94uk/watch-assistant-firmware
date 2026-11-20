#pragma once

#include <ESP_IOExpander_Library.h>

// Button identifiers
typedef enum {
    BUTTON_TOP,      // GPIO0 - Top button (Record/Action)
    BUTTON_BOTTOM    // Expander Pin 4 - Bottom button (Cancel/Settings)
} button_id_t;

// Button events
typedef enum {
    BUTTON_EVENT_CLICK,       // Single click (press + release)
    BUTTON_EVENT_TRIPLE_CLICK,// Triple click (3 clicks within 600ms)
    BUTTON_EVENT_HOLD_RECORD, // Hold >0.5s (for recording)
    BUTTON_EVENT_HOLD_POWER,  // Hold >5s (for power off)
    BUTTON_EVENT_RELEASE      // Button released after hold (to stop recording)
} button_event_t;

// Event callback function type
typedef void (*button_callback_t)(button_id_t button, button_event_t event);

/**
 * Initialize button manager
 * @param callback Function to call when button events occur
 * @param io_exp Pointer to IO Expander for bottom button (can be NULL if only using top button)
 * @return true if initialization successful
 */
bool button_manager_init(button_callback_t callback, ESP_IOExpander *io_exp = nullptr);

/**
 * Update button states - call this in loop()
 * Processes debouncing and hold detection
 * @return true if successful
 */
bool button_manager_update();

/**
 * Check if a button is currently pressed
 * @param button Which button to check
 * @return true if button is pressed (LOW state)
 */
bool button_is_pressed(button_id_t button);
